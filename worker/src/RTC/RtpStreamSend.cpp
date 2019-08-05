#define MS_CLASS "RTC::RtpStreamSend"
// #define MS_LOG_DEV

#include "RTC/RtpStreamSend.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "RTC/SeqManager.hpp"

namespace RTC
{
	/* Static. */
	// 17: 16 bit mask + the initial sequence number.
	static constexpr size_t MaxRequestedPackets{ 17 };
	static std::vector<RTC::RtpStreamSend::StorageItem*> RetransmissionContainer(MaxRequestedPackets + 1);
	// Don't retransmit packets older than this (ms).
	static constexpr uint32_t MaxRetransmissionDelay{ 2000 };
	static constexpr uint32_t DefaultRtt{ 100 };

	/* Instance methods. */

	RtpStreamSend::RtpStreamSend(RTC::RtpStream::Params& params, size_t bufferSize)
	  : RtpStream::RtpStream(params), buffer(bufferSize > 0 ? 65536 : 0, nullptr),
	    storage(bufferSize)
	{
		MS_TRACE();
	}

	RtpStreamSend::~RtpStreamSend()
	{
		MS_TRACE();

		// Clear the RTP buffer.
		ClearRetransmissionBuffer();
	}

	Json::Value RtpStreamSend::GetStats()
	{
		static const std::string Type = "outbound-rtp";
		static const Json::StaticString JsonStringType{ "type" };
		static const Json::StaticString JsonStringRtt{ "roundTripTime" };

		Json::Value json = RtpStream::GetStats();

		json[JsonStringType] = Type;
		json[JsonStringRtt]  = Json::UInt{ static_cast<uint32_t>(this->rtt) };

		return json;
	}

	bool RtpStreamSend::ReceivePacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		// Call the parent method.
		if (!RtpStream::ReceivePacket(packet))
			return false;

		// If bufferSize was given, store the packet into the buffer.
		if (!this->storage.empty())
			StorePacket(packet);

		// Increase transmission counter.
		this->transmissionCounter.Update(packet);

		return true;
	}

	void RtpStreamSend::ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report)
	{
		MS_TRACE();

		/* Calculate RTT. */

		// Get the NTP representation of the current timestamp.
		uint64_t now = DepLibUV::GetTime();
		auto ntp     = Utils::Time::TimeMs2Ntp(now);

		// Get the compact NTP representation of the current timestamp.
		uint32_t compactNtp = (ntp.seconds & 0x0000FFFF) << 16;
		compactNtp |= (ntp.fractions & 0xFFFF0000) >> 16;

		uint32_t lastSr = report->GetLastSenderReport();
		uint32_t dlsr   = report->GetDelaySinceLastSenderReport();

		// RTT in 1/2^16 second fractions.
		uint32_t rtt = 0;

		if (compactNtp > dlsr + lastSr)
			rtt = compactNtp - dlsr - lastSr;
		else
			rtt = 0;

		// RTT in milliseconds.
		this->rtt = (rtt >> 16) * 1000;
		this->rtt += (static_cast<float>(rtt & 0x0000FFFF) / 65536) * 1000;

		this->packetsLost  = report->GetTotalLost();
		this->fractionLost = report->GetFractionLost();
	}

	// This method looks for the requested RTP packets and inserts them into the
	// given container (and set to null the next container position).
	void RtpStreamSend::RequestRtpRetransmission(
	  uint16_t seq, uint16_t bitmask, std::vector<RTC::RtpPacket*>& container)
	{
		MS_TRACE();

		// Ensure the container's first element is 0.
		container[0] = nullptr;

		// If NACK is not supported, exit.
		if (!this->params.useNack)
		{
			MS_WARN_TAG(rtx, "NACK not supported");

			return;
		}

		// Look for each requested packet.
		uint64_t now = DepLibUV::GetTime();
		uint16_t rtt = (this->rtt != 0u ? this->rtt : DefaultRtt);
		bool requested{ true };
		size_t containerIdx{ 0 };

		// Some variables for debugging.
		uint16_t origBitmask = bitmask;
		uint16_t sentBitmask{ 0b0000000000000000 };
		bool isFirstPacket{ true };
		bool firstPacketSent{ false };
		uint8_t bitmaskCounter{ 0 };
		bool tooOldPacketFound{ false };

		while (requested || bitmask != 0)
		{
			bool sent = false;

			if (requested)
			{
				auto* storageItem = this->buffer[seq];
				RTC::RtpPacket* packet{ nullptr };
				uint32_t diffMs;

				// Calculate how the elapsed time between the max timestampt seen and
				// the requested packet's timestampt (in ms).
				if (storageItem)
				{
					packet = storageItem->packet;

					uint32_t diffTs = this->maxPacketTs - packet->GetTimestamp();
					diffMs = diffTs * 1000 / this->params.clockRate;
				}

				if (!storageItem)
				{
					// Do nothing.
				}
				// Don't resend the packet if older than MaxRetransmissionDelay ms.
				else if (diffMs > MaxRetransmissionDelay)
				{
					if (!tooOldPacketFound)
					{
						MS_WARN_TAG(
							rtx,
							"ignoring retransmission for too old packet "
							"[seq:%" PRIu16 ", max age:%" PRIu32 "ms, packet age:%" PRIu32 "ms]",
							packet->GetSequenceNumber(),
							MaxRetransmissionDelay,
							diffMs);

						tooOldPacketFound = true;
					}
				}
				// Don't resent the packet if it was resent in the last RTT ms.
				// clang-format off
				else if (
					storageItem->resentAtTime != 0u &&
					now - storageItem->resentAtTime <= static_cast<uint64_t>(rtt)
				)
				// clang-format on
				{
					MS_DEBUG_TAG(
						rtx,
						"ignoring retransmission for a packet already resent in the last RTT ms "
						"[seq:%" PRIu16 ", rtt:%" PRIu32 "]",
						packet->GetSequenceNumber(),
						rtt);
				}
				// Store the packet in the container and then increment its index.
				else {
					container[containerIdx++] = packet;

					// Save when this packet was resent.
					storageItem->resentAtTime = now;

					// Increase the number of times this packet was sent.
					storageItem->sentTimes++;

					sent = true;

					if (isFirstPacket)
						firstPacketSent = true;
				}
			}
			

			requested = (bitmask & 1) != 0;
			bitmask >>= 1;
			++seq;

			if (!isFirstPacket)
			{
				sentBitmask |= (sent ? 1 : 0) << bitmaskCounter;
				++bitmaskCounter;
			}
			else
			{
				isFirstPacket = false;
			}
		}

		// If not all the requested packets was sent, log it.
		if (!firstPacketSent || origBitmask != sentBitmask)
		{
			MS_DEBUG_TAG(
			  rtx,
			  "could not resend all packets [seq:%" PRIu16
			  ", first:%s, "
			  "bitmask:" MS_UINT16_TO_BINARY_PATTERN ", sent bitmask:" MS_UINT16_TO_BINARY_PATTERN "]",
			  seq,
			  firstPacketSent ? "yes" : "no",
			  MS_UINT16_TO_BINARY(origBitmask),
			  MS_UINT16_TO_BINARY(sentBitmask));
		}
		else
		{
			MS_DEBUG_TAG(
			  rtx,
			  "all packets resent [seq:%" PRIu16 ", bitmask:" MS_UINT16_TO_BINARY_PATTERN "]",
			  seq,
			  MS_UINT16_TO_BINARY(origBitmask));
		}

		// Set the next container element to null.
		container[containerIdx] = nullptr;
	}

	RTC::RTCP::SenderReport* RtpStreamSend::GetRtcpSenderReport(uint64_t now)
	{
		MS_TRACE();

		if (this->transmissionCounter.GetPacketCount() == 0u)
			return nullptr;

		auto ntp    = Utils::Time::TimeMs2Ntp(now);
		auto report = new RTC::RTCP::SenderReport();

		report->SetPacketCount(this->transmissionCounter.GetPacketCount());
		report->SetOctetCount(this->transmissionCounter.GetBytes());
		report->SetRtpTs(this->maxPacketTs);
		report->SetNtpSec(ntp.seconds);
		report->SetNtpFrac(ntp.fractions);

		return report;
	}

	void RtpStreamSend::ClearRetransmissionBuffer()
	{
		MS_TRACE();

		if (this->storage.empty())
			return;

		for (uint32_t idx{ 0 }; idx < this->buffer.size(); ++idx)
		{
			auto* storageItem = this->buffer[idx];

			if (!storageItem)
			{
				// TODO
				MS_ASSERT(this->buffer[idx] == nullptr, "key should be NULL");

				continue;
			}

			// Reset (free RTP packet) the storage item.
			ResetStorageItem(storageItem);

			// Unfill the buffer item.
			this->buffer[idx] = nullptr;
		}

		// Reset buffer.
		this->bufferStartIdx = 0;
		this->bufferSize     = 0;
	}

	inline void RtpStreamSend::ResetStorageItem(StorageItem* storageItem)
	{
		MS_TRACE();

		MS_ASSERT(storageItem, "storageItem cannot be nullptr");

		delete storageItem->packet;

		storageItem->packet       = nullptr;
		storageItem->resentAtTime = 0;
		storageItem->sentTimes    = 0;
	}

	/**
	 * Iterates the buffer starting from the current start idx + 1 until next
	 * used one. It takes into account that the buffer is circular.
	 */
	inline void RtpStreamSend::UpdateBufferStartIdx()
	{
		uint16_t seq = this->bufferStartIdx + 1;

		for (uint32_t idx{ 0 }; idx < this->buffer.size(); ++idx, ++seq)
		{
			auto* storageItem = this->buffer[seq];

			if (storageItem)
			{
				this->bufferStartIdx = seq;

				break;
			}
		}
	}

	inline void RtpStreamSend::StorePacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		if (packet->GetSize() > RTC::MtuSize)
		{
			MS_WARN_TAG(
			  rtp,
			  "packet too big [ssrc:%" PRIu32 ", seq:%" PRIu16 ", size:%zu]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber(),
			  packet->GetSize());

			return;
		}

		auto seq          = packet->GetSequenceNumber();
		auto* storageItem = this->buffer[seq];

		// Buffer is empty.
		if (this->bufferSize == 0)
		{
			// Take the first storage position.
			storageItem       = std::addressof(this->storage[0]);
			this->buffer[seq] = storageItem;

			// Increase buffer size and set start index.
			this->bufferSize++;
			this->bufferStartIdx = seq;
		}
		// The buffer item is already used. Check whether we should replace its
		// storage with the new packet or just ignore it (if duplicated packet).
		else if (storageItem)
		{
			auto* storedPacket = storageItem->packet;

			if (packet->GetTimestamp() == storedPacket->GetTimestamp())
				return;

			// Reset the storage item.
			ResetStorageItem(storageItem);

			// If this was the item referenced by the buffer start index, move it to
			// the next one.
			if (this->bufferStartIdx == seq)
				UpdateBufferStartIdx();
		}
		// Buffer not yet full, add an entry.
		else if (this->bufferSize < this->storage.size())
		{
			// Take the next storage position.
			storageItem       = std::addressof(this->storage[this->bufferSize]);
			this->buffer[seq] = storageItem;

			// Increase buffer size.
			this->bufferSize++;
		}
		// Buffer full, remove oldest entry and add new one.
		else
		{
			auto* firstStorageItem = this->buffer[this->bufferStartIdx];

			// Reset the first storage item.
			ResetStorageItem(firstStorageItem);

			// Unfill the buffer start item.
			this->buffer[this->bufferStartIdx] = nullptr;

			// Move the buffer start index.
			UpdateBufferStartIdx();

			// Take the freed storage item.
			storageItem       = firstStorageItem;
			this->buffer[seq] = storageItem;
		}

		// Clone the packet into the retrieved storage item.
		storageItem->packet = packet->Clone(storageItem->store);
	}

	void RtpStreamSend::SetRtx(uint8_t payloadType, uint32_t ssrc)
	{
		MS_TRACE();

		this->hasRtx         = true;
		this->rtxPayloadType = payloadType;
		this->rtxSsrc        = ssrc;
		this->rtxSeq         = Utils::Crypto::GetRandomUInt(0u, 0xFFFF);
	}

	void RtpStreamSend::RtxEncode(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		MS_ASSERT(this->hasRtx, "RTX not enabled on this stream");

		packet->RtxEncode(this->rtxPayloadType, this->rtxSsrc, ++this->rtxSeq);
	}

} // namespace RTC
