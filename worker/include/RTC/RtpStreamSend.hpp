#ifndef MS_RTC_RTP_STREAM_SEND_HPP
#define MS_RTC_RTP_STREAM_SEND_HPP

#include "RTC/RTCP/ReceiverReport.hpp"
#include "RTC/RTCP/SenderReport.hpp"
#include "RTC/RtpStream.hpp"
#include "RTC/SeqManager.hpp"
#include "Logger.hpp"
#include <list>
#include <vector>

namespace RTC
{
	class RtpStreamSend : public RtpStream
	{
	private:
		struct StorageItem
		{
			uint8_t store[RTC::MtuSize];
		};

	private:
		struct BufferItem
		{
			uint16_t seq{ 0 }; // RTP seq.
			uint64_t resentAtTime{ 0 };
			RTC::RtpPacket* packet{ nullptr };
		};

  private:
	  class Buffer
		{
		private:
			std::vector<BufferItem> vctr; // array can hold up to maxsize of BufferItems plus always has one empty slot for easier inserts
			uint8_t start{ 0 }; // index where data begins
			size_t cursize{ 0 }; // number of items currently in array. While inserting a new packet, we may see cursize == maxsize + 1 until trim_front() is called
			size_t maxsize{ 0 }; //maximum number of items that can be stored in this Buffer

		public:
			Buffer(size_t bufferSize) : vctr(bufferSize + 1), start(0), cursize(0), maxsize(bufferSize) {}

			inline bool empty() const { return vctr.empty() || cursize == 0; }
			inline size_t datasize() const { return vctr.empty() ? 0 : cursize; }
			inline void clear() { vctr.clear(); start = cursize = maxsize = 0; }

			RtpStreamSend::BufferItem& first();
			RtpStreamSend::BufferItem& last();
			bool push_back (const RtpStreamSend::BufferItem& val);
			void trim_front();
			size_t ordered_insert_by_seq( const RtpStreamSend::BufferItem& val); // checks bufferItem.seq and inserts data into a buffer. returns index of just inserted item.
			
			RtpStreamSend::BufferItem& operator[] (size_t index);
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

		/* Pure virtual methods inherited from RtpStream. */
	protected:
		void CheckStatus() override;

	private:
		// Passed by argument.
		std::vector<StorageItem> storage;
		Buffer buffer;
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
