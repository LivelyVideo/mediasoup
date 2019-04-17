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

	// Don't retransmit packets older than this (ms).
	static constexpr uint32_t MaxRetransmissionDelay{ 2000 };
	static constexpr uint32_t DefaultRtt{ 100 };

  /* Buffer methods */
		RtpStreamSend::BufferItem& RtpStreamSend::Buffer::first()
	{ 
		MS_ASSERT(vctr.size() > 0, "Read first() from empty Buffer");
		return vctr[start];
	}
	
	RtpStreamSend::BufferItem& RtpStreamSend::Buffer::last() 
	{
		MS_ASSERT(vctr.size() > 0, "Read last() from empty Buffer");
		return vctr[(start + cursize) % vctr.size()]; 
	}

	RtpStreamSend::BufferItem& RtpStreamSend::Buffer::operator[] (size_t index)
	{
		MS_ASSERT(index >= maxsize, "index out of vector maxsize capacity");
		auto idx = (start + index) % vctr.size();
		return vctr[idx];
	}

	bool RtpStreamSend::Buffer::push_back(const RtpStreamSend::BufferItem& val)
	{
		// can't insert, no room in array
		if (cursize >= maxsize + 1)
			return false;

		(*this)[cursize] = val;
		cursize++;

		return true;
	}

	// Delete the first item in array.
	// This function is only used to remove the oldest packet in case buffer has exceeded max capacity, but we don't check for that condition here, it's not important
	void RtpStreamSend::Buffer::trim_front()
	{
		if (empty())
			return;
		
		(*this)[start].packet = nullptr;
		start = (start + 1) % vctr.size();
		cursize--;
	}

 // checks bufferItem.seq and inserts data into a buffer. returns false if buffer went 1 item above maxcapacity and needs to be trimmed back. asserts if buffer is already above maxcapacity (should not happen)
	size_t RtpStreamSend::Buffer::ordered_insert_by_seq( const RtpStreamSend::BufferItem& val )
	{
		MS_ASSERT(cursize <= maxsize, "Buffer exceeded max capacity, must trim it prior to inserting new items");

		// This var will point to a location of just inserted buffer item
		auto idx = cursize - 1;
		auto packetSeq = val.seq;

		// First, insert new packet in buffer array unless already stored.
		// Later we will check if buffer array went beyond max capacity and in that case remove the oldest packet
		for (; idx >=0; idx--)
		{
			auto currentSeq = (*this)[idx].seq;

			// Packet is already stored. TODO: need to return smth special insted of idx value to tell the caller there is no need to copy packet data into storage
			if (packetSeq == currentSeq) {			
				// rollback: shift all items moved in anticipation of insertion op back to the left
				// j points to the location of a "hole" slot
				for (auto j = idx + 1; j < cursize; j++ ) {
					(*this)[j] = (*this)[j + 1];
					(*this)[j + 1].packet = nullptr; // now "hole" has moved one step to the right
				}
				return idx;
			}

			if (SeqManager<uint16_t>::IsSeqHigherThan(packetSeq, currentSeq))
			{
				// insert here.
				(*this)[idx] = val;

				break;
			}

			// Now move current buffer item into an empty slot on the right.
			// Then either we insert a new packet to the left of it, or iterate further
			(*this)[idx + 1] = (*this)[idx];
			(*this)[idx].packet = nullptr;
		}

		return idx;
	}


	/* Instance methods. */

	RtpStreamSend::RtpStreamSend(RTC::RtpStream::Params& params, size_t bufferSize)
	  : RtpStream::RtpStream(params), storage(bufferSize), buffer(bufferSize)
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

		// 17: 16 bit mask + the initial sequence number.
		static constexpr size_t MaxRequestedPackets{ 17 };

		// Ensure the container's first element is 0.
		container[0] = nullptr;

		// If NACK is not supported, exit.
		if (!this->params.useNack)
		{
			MS_WARN_TAG(rtx, "NACK not negotiated");

			return;
		}

		// If the buffer is empty just return.
		if (this->buffer.empty())
			return;

		uint16_t firstSeq = seq;
		uint16_t lastSeq  = firstSeq + MaxRequestedPackets - 1;

		// Number of requested packets cannot be greater than the container size - 1.
		MS_ASSERT(container.size() - 1 >= MaxRequestedPackets, "RtpPacket container is too small");

		uint16_t bufferFirstSeq = this->buffer.first().seq;
		uint16_t bufferLastSeq  = this->buffer.last().seq;

		// Requested packet range not found.
		if (
		  SeqManager<uint16_t>::IsSeqHigherThan(firstSeq, bufferLastSeq) ||
		  SeqManager<uint16_t>::IsSeqLowerThan(lastSeq, bufferFirstSeq))
		{
			MS_WARN_TAG(
			  rtx,
			  "requested packet range not in the buffer [seq:%" PRIu16 ", bufferFirstseq:%" PRIu16
			  ", bufferLastseq:%" PRIu16 "]",
			  seq,
			  bufferFirstSeq,
			  bufferLastSeq);

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
				size_t idx = 0;
				for (; idx < this->buffer.datasize(); idx++)
				{
					auto currentSeq = this->buffer[idx].seq;

					// Found.
					if (currentSeq == seq)
					{
						auto currentPacket = this->buffer[idx].packet;
						// Calculate how the elapsed time between the max timestampt seen and
						// the requested packet's timestampt (in ms).
						uint32_t diffTs = this->maxPacketTs - currentPacket->GetTimestamp();
						uint32_t diffMs = diffTs * 1000 / this->params.clockRate;

						// Just provide the packet if no older than MaxRetransmissionDelay ms.
						if (diffMs > MaxRetransmissionDelay)
						{
							if (!tooOldPacketFound)
							{
								// TODO: May we ask for a key frame in this case?

								MS_WARN_TAG(
								  rtx,
								  "ignoring retransmission for too old packet "
								  "[seq:%" PRIu16 ", max age:%" PRIu32 "ms, packet age:%" PRIu32 "ms]",
								  currentPacket->GetSequenceNumber(),
								  MaxRetransmissionDelay,
								  diffMs);

								tooOldPacketFound = true;
							}

							break;
						}

						// Don't resent the packet if it was resent in the last RTT ms.
						auto resentAtTime = this->buffer[idx].resentAtTime;

						if ((resentAtTime != 0u) && now - resentAtTime <= static_cast<uint64_t>(rtt))
						{
							MS_DEBUG_TAG(
							  rtx,
							  "ignoring retransmission for a packet already resent in the last RTT ms "
							  "[seq:%" PRIu16 ", rtt:%" PRIu32 "]",
							  currentPacket->GetSequenceNumber(),
							  rtt);

							break;
						}

						// Store the packet in the container and then increment its index.
						container[containerIdx++] = currentPacket;

						// Save when this packet was resent.
						this->buffer[idx].resentAtTime = now;

						sent = true;
						if (isFirstPacket)
							firstPacketSent = true;

						break;
					}

					// It can not be after this packet.
					if (SeqManager<uint16_t>::IsSeqHigherThan(currentSeq, seq))
						break;
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

		// Delete cloned packets.
		for (size_t idx = 0; idx < this->buffer.datasize(); idx++)
		{
			delete this->buffer[idx].packet;
		}

		// Clear list.
		this->buffer.clear();
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

		// Sum the packet seq number and the number of 16 bits cycles.
		auto packetSeq = packet->GetSequenceNumber();
		BufferItem bufferItem;

		bufferItem.seq = packetSeq;

		// If empty do it easy.
		if (this->buffer.empty())
		{
			auto store = this->storage[0].store;

			bufferItem.packet = packet->Clone(store);
			this->buffer.push_back(bufferItem);

			return;
		}

		// Otherwise, do the stuff.
		size_t newIdx = this->buffer.ordered_insert_by_seq(bufferItem);

		uint8_t* store{ nullptr };
		if (this->buffer.datasize() <= this->storage.size())
		{
			store = this->storage[this->buffer.datasize() - 1].store;
		}
		else
		{
			// Otherwise remove the first packet of the buffer and replace its storage area.
			MS_ASSERT(this->buffer.datasize() - 1  == this->storage.size(), "When buffer beyond max capacity storage should be exactly at full capacity");
			auto firstPacket = this->buffer.first().packet;

			// Store points to the store used by the first packet.
			store = const_cast<uint8_t*>(firstPacket->GetData());
			// Free the first packet.
			delete firstPacket;

			// Remove the first element in the list.
			this->buffer.trim_front();
		}

		// Update the new buffer item so it points to the cloned packed.
		this->buffer[newIdx].packet = packet->Clone(store);
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
