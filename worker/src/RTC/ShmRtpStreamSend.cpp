#define MS_CLASS "RTC::ShmRtpStreamSend"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/ShmRtpStreamSend.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/SeqManager.hpp"

namespace RTC
{
	/* Static. */

	// 17: 16 bit mask + the initial sequence number.
	static constexpr size_t MaxRequestedPackets{ 17u };

	// Don't retransmit packets older than this (ms).
	static constexpr uint32_t MaxRetransmissionDelay{ 2000 };
	static constexpr uint32_t DefaultRtt{ 100 };

	/* Instance methods. */

	ShmRtpStreamSend::ShmRtpStreamSend(
	        RTC::ShmRtpStreamSend::Listener* listener,
	        RTC::RtpStream::Params& params,
	        std::string& mid): RtpStreamSend(listener, params, mid)
	{
	    MS_TRACE();
	}

	ShmRtpStreamSend::~ShmRtpStreamSend()
	{
		MS_TRACE();
	}

	void ShmRtpStreamSend::SetSharedMemoryCtx( DepLibStreamShm::ShmCtx* ctx)
	{
		this->shmCtx  = ctx;
	}

	void ShmRtpStreamSend::FillJsonStats(json& jsonObject)
	{
		MS_TRACE();

		uint64_t nowMs = DepLibUV::GetTimeMs();

		RTC::RtpStream::FillJsonStats(jsonObject);

		jsonObject["type"]        = "outbound-rtp";
		jsonObject["packetCount"] = this->transmissionCounter.GetPacketCount();
		jsonObject["byteCount"]   = this->transmissionCounter.GetBytes();
		jsonObject["bitrate"]     = this->transmissionCounter.GetBitrate(nowMs);
	}

	void ShmRtpStreamSend::SetRtx(uint8_t payloadType, uint32_t ssrc)
	{
		MS_TRACE();

		RTC::RtpStream::SetRtx(payloadType, ssrc);

		this->rtxSeq = Utils::Crypto::GetRandomUInt(0u, 0xFFFF);
	}

	bool ShmRtpStreamSend::ReceivePacket(RTC::RtpPacket* packet, uint16_t orig)
	{
		MS_TRACE();

		// Call the parent method.
		if (!RtpStream::ReceiveStreamPacket(packet))
			return false;

		// Increase transmission counter.
		this->transmissionCounter.Update(packet);

		return true;
	}

	void ShmRtpStreamSend::ReceiveNack(RTC::RTCP::FeedbackRtpNackPacket* nackPacket)
	{
		MS_TRACE();

        // If NACK is not supported, exit.
        if (!this->params.useNack)
        {
            MS_WARN_TAG(rtx, "NACK not supported");

            return;
        }

        this->nackCount++;

		for (auto it = nackPacket->Begin(); it != nackPacket->End(); ++it)
		{
			RTC::RTCP::FeedbackRtpNackItem* item = *it;

			ProcessNackBLP(item->GetPacketId(), item->GetLostPacketBitmask());
		}
	}

	void ShmRtpStreamSend::ReceiveKeyFrameRequest(RTC::RTCP::FeedbackPs::MessageType messageType)
	{
		MS_TRACE();

		switch (messageType)
		{
			case RTC::RTCP::FeedbackPs::MessageType::PLI:
				this->pliCount++;
				break;

			case RTC::RTCP::FeedbackPs::MessageType::FIR:
				this->firCount++;
				break;

			default:;
		}
	}

	void ShmRtpStreamSend::ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report)
	{
		MS_TRACE();

		/* Calculate RTT. */

		// Get the NTP representation of the current timestamp.
		uint64_t nowMs = DepLibUV::GetTimeMs();
		auto ntp       = Utils::Time::TimeMs2Ntp(nowMs);

		// Get the compact NTP representation of the current timestamp.
		uint32_t compactNtp = (ntp.seconds & 0x0000FFFF) << 16;

		compactNtp |= (ntp.fractions & 0xFFFF0000) >> 16;

		uint32_t lastSr = report->GetLastSenderReport();
		uint32_t dlsr   = report->GetDelaySinceLastSenderReport();

		// RTT in 1/2^16 second fractions.
		uint32_t rtt{ 0 };

		// If no Sender Report was received by the remote endpoint yet, ignore lastSr
		// and dlsr values in the Receiver Report.
		if (lastSr && dlsr && (compactNtp > dlsr + lastSr))
			rtt = compactNtp - dlsr - lastSr;

		// RTT in milliseconds.
		SetRTT(static_cast<float>(rtt >> 16) * 1000);
		SetRTT (GetRTT() + (static_cast<float>(rtt & 0x0000FFFF) / 65536) * 1000);

		if (GetRTT() > 0.0f)
			this->hasRtt = true;

		this->packetsLost  = report->GetTotalLost();
		this->fractionLost = report->GetFractionLost();

		// Update the score with the received RR.
		RtpStreamSend::UpdateScore(report);
	}

	void ShmRtpStreamSend::ReceiveRtcpXrReceiverReferenceTime(RTC::RTCP::ReceiverReferenceTime* report)
	{
		MS_TRACE();

		this->lastRrReceivedMs = DepLibUV::GetTimeMs();
		this->lastRrTimestamp  = report->GetNtpSec() << 16;
		this->lastRrTimestamp += report->GetNtpFrac() >> 16;
	}
	
	RTC::RTCP::SenderReport* ShmRtpStreamSend::GetRtcpSenderReport(uint64_t nowMs)
	{
		MS_TRACE();

		if (this->transmissionCounter.GetPacketCount() == 0u)
			return nullptr;

		auto ntp     = Utils::Time::TimeMs2Ntp(nowMs);
		auto* report = new RTC::RTCP::SenderReport();

		// Calculate TS difference between now and maxPacketMs.
		auto diffMs = nowMs - this->maxPacketMs;
		auto diffTs = diffMs * GetClockRate() / 1000;

		report->SetSsrc(GetSsrc());
		report->SetPacketCount(this->transmissionCounter.GetPacketCount());
		report->SetOctetCount(this->transmissionCounter.GetBytes());
		report->SetNtpSec(ntp.seconds);
		report->SetNtpFrac(ntp.fractions);
		report->SetRtpTs(this->maxPacketTs + diffTs);

		// Update info about last Sender Report.
		this->lastSenderReportNtpMs = nowMs;
		this->lastSenderReportTs     = this->maxPacketTs + diffTs;

		return report;
	}

	RTC::RTCP::DelaySinceLastRr::SsrcInfo* ShmRtpStreamSend::GetRtcpXrDelaySinceLastRr(uint64_t nowMs)
	{
		MS_TRACE();

		if (this->lastRrReceivedMs == 0u)
			return nullptr;

		// Get delay in milliseconds.
		auto delayMs = static_cast<uint32_t>(nowMs - this->lastRrReceivedMs);
		// Express delay in units of 1/65536 seconds.
		uint32_t dlrr = (delayMs / 1000) << 16;

		dlrr |= uint32_t{ (delayMs % 1000) * 65536 / 1000 };

		auto* ssrcInfo = new RTC::RTCP::DelaySinceLastRr::SsrcInfo();

		ssrcInfo->SetSsrc(GetSsrc());
		ssrcInfo->SetDelaySinceLastReceiverReport(dlrr);
		ssrcInfo->SetLastReceiverReport(this->lastRrTimestamp);

		return ssrcInfo;
	}
	
	RTC::RTCP::SdesChunk* ShmRtpStreamSend::GetRtcpSdesChunk()
	{
		MS_TRACE();

		const auto& cname = GetCname();
		auto* sdesChunk   = new RTC::RTCP::SdesChunk(GetSsrc());
		auto* sdesItem =
		  new RTC::RTCP::SdesItem(RTC::RTCP::SdesItem::Type::CNAME, cname.size(), cname.c_str());

		sdesChunk->AddItem(sdesItem);

		return sdesChunk;
	}

	void ShmRtpStreamSend::Pause()
	{
		MS_TRACE();
	}

	void ShmRtpStreamSend::Resume()
	{
		MS_TRACE();
	}

	uint32_t ShmRtpStreamSend::GetBitrate(uint64_t nowMs)
	{
		MS_TRACE();

		return RtpStreamSend::GetBitrate(nowMs);
	}

	uint32_t ShmRtpStreamSend::GetSpatialLayerBitrate(uint64_t /*nowMs*/, uint8_t /*spatialLayer*/)
	{
		MS_TRACE();

		MS_ABORT("invalid method call");
	}

	uint32_t ShmRtpStreamSend::GetLayerBitrate(
	  uint64_t /*nowMs*/, uint8_t /*spatialLayer*/, uint8_t /*temporalLayer*/)
	{
		MS_TRACE();

		MS_ABORT("invalid method call");
	}

	// Go over the list of lost packets specified by
	// the sequence and the bitmask (BLP) and deliver
	// the loss packets to the client
	void ShmRtpStreamSend::ProcessNackBLP(uint16_t seq, uint16_t bitmask)
	{
		MS_TRACE();

        uint8_t buf[2048], *p;
        int rc;


        if (!this->shmCtx || !this->shmCtx->IsOpen()) return;

		uint32_t blp = ((uint32_t)bitmask << 1) + 1;

		for (int i = 0; i < 17; i++) {
		    for (;blp & 1;) {
		        p = buf;
		        rc = this->shmCtx->ReadBySeqId(seq, &p, sizeof(buf)); // this->shmCtx cannot be nullptr here
		        if (rc < 0)
		        {
		            MS_WARN_TAG(
		                    rtp,
		                    "Failed to read RTP pkt seq=%" PRIu16, seq);
		            break;
		        }

		        RTC::RtpPacket* packet{ nullptr };
                packet = RtpPacket::Parse(buf, (p - buf));
                if (packet == nullptr)
                {
                    MS_WARN_TAG(rtp, "failed to parse RTP packet for NACK. seq=%" PRIu16, seq);
                    break;
                }

                // If we use RTX and the packet has not yet been resent, encode it now.
                if (HasRtx())
                {
                    packet->RtxEncode(this->params.rtxPayloadType, this->params.rtxSsrc, ++this->rtxSeq);
                }

                // Retransmit the packet.
                static_cast<RTC::ShmRtpStreamSend::Listener*>(this->listener)
                  ->OnRtpStreamRetransmitRtpPacket(this, packet);

                // Mark the packet as retransmitted.
                RTC::RtpStream::PacketRetransmitted(packet);

                this->nackPacketCount++;

                delete packet;

                break;
		    }

		    seq++;
		    blp >>= 1;
		}
	}
} // namespace RTC
