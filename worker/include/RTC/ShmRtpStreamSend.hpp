#ifndef MS_RTC_SHM_RTP_STREAM_SEND_HPP
#define MS_RTC_SHM_RTP_STREAM_SEND_HPP

#include "RTC/RateCalculator.hpp"
#include "RTC/RtpStream.hpp"
#include "RTC/RtpStreamSend.hpp"
#include <vector>
#include "DepLibStreamShm.hpp"

namespace RTC
{
	class ShmRtpStreamSend : public RTC::RtpStreamSend
	{
	public:
		ShmRtpStreamSend(
		  RTC::RtpStreamSend::Listener* listener,
		  RTC::RtpStream::Params& params,
		  std::string& mid);

		~ShmRtpStreamSend() override;

		void FillJsonStats(json& jsonObject) override;
		void SetRtx(uint8_t payloadType, uint32_t ssrc) override;
		bool ReceivePacket(RTC::RtpPacket* packet, uint16_t orig);
		void ReceiveNack(RTC::RTCP::FeedbackRtpNackPacket* nackPacket);
		void ReceiveKeyFrameRequest(RTC::RTCP::FeedbackPs::MessageType messageType);
		void ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report);
		void ReceiveRtcpXrReceiverReferenceTime(RTC::RTCP::ReceiverReferenceTime* report);
		RTC::RTCP::DelaySinceLastRr::SsrcInfo* GetRtcpXrDelaySinceLastRr(uint64_t nowMs);
		RTC::RTCP::SenderReport* GetRtcpSenderReport(uint64_t nowMs);
		RTC::RTCP::SdesChunk* GetRtcpSdesChunk();

		void Pause();
		void Resume();

		uint32_t GetBitrate(uint64_t nowMs) override;
		uint32_t GetSpatialLayerBitrate(uint64_t nowMs, uint8_t spatialLayer) override;
		uint32_t GetLayerBitrate(uint64_t nowMs, uint8_t spatialLayer, uint8_t temporalLayer) override;

		void SetSharedMemoryCtx( DepLibStreamShm::ShmCtx* ctx);

	protected:
        void SetRTT(uint16_t t) {this->rtt = t;}
        uint16_t GetRTT() const { return this->rtt; }

	private:
		void ProcessNackBLP(uint16_t seq, uint16_t bitmask);

		uint16_t rtxSeq{ 0u };
		
	private:
		DepLibStreamShm::ShmCtx *shmCtx{ nullptr };
	};
} // namespace RTC

#endif
