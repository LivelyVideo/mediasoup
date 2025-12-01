#ifndef MS_RTC_RTP_STREAM_SEND_HPP
#define MS_RTC_RTP_STREAM_SEND_HPP

#include "LivelyBinLogs.hpp"
#include "RTC/RateCalculator.hpp"
#include "RTC/RtpRetransmissionBuffer.hpp"
#include "RTC/RtpStream.hpp"

namespace RTC
{
	class RtpStreamSend : public RTC::RtpStream
	{
	public:
		// Maximum retransmission buffer size for video (ms).
		const static uint32_t MaxRetransmissionDelayForVideoMs;
		// Maximum retransmission buffer size for audio (ms).
		const static uint32_t MaxRetransmissionDelayForAudioMs;

	public:
		class Listener : public RTC::RtpStream::Listener
		{
		public:
			virtual void OnRtpStreamRetransmitRtpPacket(
			  RTC::RtpStreamSend* rtpStream, RTC::RtpPacket* packet) = 0;
		};

	public:
		RtpStreamSend(
		  RTC::RtpStreamSend::Listener* listener, RTC::RtpStream::Params& params, std::string& mid);
		~RtpStreamSend() override;

		void FillStats(
		  size_t& packetsCount,
		  size_t& bytesCount,
		  size_t& framesCount,
		  uint32_t& packetsLost,
		  size_t& packetsDiscarded,
		  size_t& packetsRetransmitted,
		  size_t& packetsRepaired,
		  size_t& nackCount,
		  size_t& nackPacketCount,
		  size_t& kfCount,
		  float& rtt,
		  uint32_t& maxPacketTs) override
		{
			packetsCount         = this->transmissionCounter.GetPacketCount();
			bytesCount           = this->transmissionCounter.GetBytes();
			framesCount          = this->transmissionCounter.GetFrameCount();
			packetsLost          = this->packetsLost;
			packetsDiscarded     = this->packetsDiscarded;
			packetsRetransmitted = this->packetsRetransmitted;
			packetsRepaired      = this->packetsRepaired;
			nackCount            = this->nackCount;
			nackPacketCount      = this->nackPacketCount;
			kfCount              = this->pliCount + this->firCount;
			rtt                  = this->rtt;

			// convert RTP time to unix timestamp
			// in ms using the RTCP sender report
			uint32_t clock_rate = this->GetClockRate();
			if (!clock_rate)
			{
				maxPacketTs = 0xFFFFFFFF;
			}
			else
			{
				int delta_rtp = ((int)this->maxPacketTs - (int)this->lastSenderReportTs);
				delta_rtp     = delta_rtp * 1000 / (int)clock_rate;

				maxPacketTs = (uint32_t)((int)this->lastSenderReportNtpMs + delta_rtp);
			}
		}

		void FillJsonStats(json& jsonObject) override;
		void SetRtx(uint8_t payloadType, uint32_t ssrc) override;
		bool ReceivePacket(RTC::RtpPacket* packet, std::shared_ptr<RTC::RtpPacket>& sharedPacket);
		void ReceiveNack(RTC::RTCP::FeedbackRtpNackPacket* nackPacket);
		void ReceiveKeyFrameRequest(RTC::RTCP::FeedbackPs::MessageType messageType);
		void ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report);
		void ReceiveRtcpXrReceiverReferenceTime(RTC::RTCP::ReceiverReferenceTime* report);
		RTC::RTCP::SenderReport* GetRtcpSenderReport(
		  uint64_t nowMs, uint64_t producerNtpMs, uint32_t producerRtpTs);
		RTC::RTCP::SenderReport* GetRtcpSenderReport(uint64_t nowMs);
		RTC::RTCP::DelaySinceLastRr::SsrcInfo* GetRtcpXrDelaySinceLastRr(uint64_t nowMs);
		RTC::RTCP::SdesChunk* GetRtcpSdesChunk();
		void Pause() override;
		void Resume() override;
		uint32_t GetBitrate(uint64_t nowMs) override
		{
			return this->transmissionCounter.GetBitrate(nowMs);
		}
		uint32_t GetBitrate(uint64_t nowMs, uint8_t spatialLayer, uint8_t temporalLayer) override;
		uint32_t GetSpatialLayerBitrate(uint64_t nowMs, uint8_t spatialLayer) override;
		uint32_t GetLayerBitrate(uint64_t nowMs, uint8_t spatialLayer, uint8_t temporalLayer) override;

	private:
		void StorePacket(RTC::RtpPacket* packet, std::shared_ptr<RTC::RtpPacket>& sharedPacket);
		void FillRetransmissionContainer(uint16_t seq, uint16_t bitmask);
		void UpdateScore(RTC::RTCP::ReceiverReport* report);

		/* Pure virtual methods inherited from RTC::RtpStream. */
	public:
		void UserOnSequenceNumberReset() override;

	private:
		// Packets lost at last interval for score calculation.
		uint32_t lostPriorScore{ 0u };
		// Packets sent at last interval for score calculation.
		uint32_t sentPriorScore{ 0u };
		std::string mid;
		uint16_t rtxSeq{ 0u };
		RTC::RtpDataCounter transmissionCounter;
		RTC::RtpRetransmissionBuffer* retransmissionBuffer{ nullptr };
		// The middle 32 bits out of 64 in the NTP timestamp received in the most
		// recent receiver reference timestamp.
		uint32_t lastRrTimestamp{ 0u };
		// Wallclock time representing the most recent receiver reference timestamp
		// arrival.
		uint64_t lastRrReceivedMs{ 0u };
	};
} // namespace RTC

#endif
