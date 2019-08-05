#ifndef MS_RTC_RTP_STREAM_SEND_HPP
#define MS_RTC_RTP_STREAM_SEND_HPP

#include "RTC/RTCP/ReceiverReport.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RtpStream.hpp"
#include <list>
#include <vector>

namespace RTC
{
	class RtpStreamSend : public RtpStream
	{
	public:
		struct StorageItem
		{
			// Cloned packet.
			RTC::RtpPacket* packet{ nullptr };
			// Memory to hold the cloned packet (with extra space for RTX encoding).
			uint8_t store[RTC::MtuSize + 100];
			// Last time this packet was resent.
			uint64_t resentAtTime{ 0 };
			// Number of times this packet was resent.
			uint8_t sentTimes{ 0 };
		};

	public:
		RtpStreamSend(RTC::RtpStream::Params& params, size_t bufferSize);
		~RtpStreamSend() override;

		Json::Value GetStats() override;
		bool ReceivePacket(RTC::RtpPacket* packet) override;
		void ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report);
		void RequestRtpRetransmission(
		  uint16_t seq, uint16_t bitmask, std::vector<RTC::RtpPacket*>& container);
		RTC::RTCP::SenderReport* GetRtcpSenderReport(uint64_t now);
		void SetRtx(uint8_t payloadType, uint32_t ssrc);
		bool HasRtx() const;
		void RtxEncode(RtpPacket* packet);
		void ClearRetransmissionBuffer();
		bool IsHealthy() const;

	private:
		void StorePacket(RTC::RtpPacket* packet);
		void ResetStorageItem(StorageItem* storageItem);
		void UpdateBufferStartIdx();

		/* Pure virtual methods inherited from RtpStream. */
	protected:
		void CheckStatus() override;

	private:
		std::vector<StorageItem*> buffer;
		uint16_t bufferStartIdx{ 0 };
		size_t bufferSize{ 0 };
		std::vector<StorageItem> storage;

		RTC::RtpDataCounter transmissionCounter;

		// Stats.
		float rtt{ 0 };

	private:
		// Retransmittion related.
		bool hasRtx{ false };
		uint8_t rtxPayloadType{ 0 };
		uint32_t rtxSsrc{ 0 };
		uint16_t rtxSeq{ 0 };
	};

	inline bool RtpStreamSend::HasRtx() const
	{
		return this->hasRtx;
	}

	inline void RtpStreamSend::CheckStatus()
	{
		return;
	}
} // namespace RTC

#endif
