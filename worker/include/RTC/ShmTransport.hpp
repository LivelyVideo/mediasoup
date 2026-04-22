#ifndef MS_RTC_SHM_TRANSPORT_HPP
#define MS_RTC_SHM_TRANSPORT_HPP

#include <nlohmann/json.hpp>
#include "DepLibSfuShm.hpp"
#include "FBS/transport.h"
#include "FBS/shmTransport.h"
#include "RTC/Transport.hpp"
#include "RTC/TransportTuple.hpp"
#include "RTC/UdpSocket.hpp"
#include "RTC/RTCP/FeedbackPsRemb.hpp"
#include "handles/TimerHandle.hpp"
#include <string>

using json = nlohmann::json;

namespace RTC
{
	class ShmTransport : public RTC::Transport,
											 public RTC::UdpSocket::Listener
	{
	private:
		struct ListenIp
		{
			std::string ip;
			std::string announcedIp;
		};

	public:
		ShmTransport(RTC::Shared* shared, const std::string& id, RTC::Transport::Listener* listener, const FBS::Transport::Options* options);
		~ShmTransport() override;

	public:
		void HandleRequest(Channel::ChannelRequest* request) override;
		void HandleNotification(Channel::ChannelNotification* notification) override;
		DepLibSfuShm::ShmCtx* ShmCtx() { return &this->shmCtx; }
		flatbuffers::Offset<FBS::ShmTransport::DumpResponse> FillBuffer(flatbuffers::FlatBufferBuilder& builder) const;
		flatbuffers::Offset<FBS::ShmTransport::GetStatsResponse> FillBufferStats(flatbuffers::FlatBufferBuilder& builder);

	private:
		bool IsConnected() const override;
		void SendRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet, RTC::Transport::onSendCallback* cb = nullptr) override;
		void SendRtcpPacket(RTC::RTCP::Packet* packet) override;
		void SendSctpData(const uint8_t* data, size_t len) override;
		void SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) override;
		void SendMessage(
		  RTC::DataConsumer* dataConsumer,
		  const uint8_t* msg,
		  size_t len,
		  uint32_t ppid,
		  onQueuedCallback* cb = nullptr) override;
		void RecvStreamClosed(uint32_t ssrc) override;
		void SendStreamClosed(uint32_t ssrc) override;

		/* Pure virtual methods inherited from RTC::Consumer::Listener. */
	public:
		void OnConsumerNeedBitrateChange(RTC::Consumer* consumer) override;

		/* Pure virtual methods inherited from TimerHandle::Listener. */
	public:
		void OnTimer(TimerHandle* timer) override;

	/* Pure virtual methods inherited from RTC::UdpSocket::Listener. */
	public:
		void OnUdpSocketPacketReceived(
		  RTC::UdpSocket* socket, const uint8_t* data, size_t len, const struct sockaddr* remoteAddr) override;
		void OnPacketReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len);
		void OnRtpDataReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len);
		void OnRtcpDataReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len);
		void OnSctpDataReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len);

	public:
		std::string ShmName() const { return this->shmCtx.StreamName().c_str();}

	public:
		std::string appData;

	private:
		TimerHandle* shmNoConsumeTimer{ nullptr }; // Timer to monitor 
		void   OnNoConsume();                // Close shm if a consumer has not been created within 60 seconds after a call to transport's ctor 

	/* Use this function to notify shm transport that a new consumer was set up and stop a timer */
	public:
		void StopNoConsumeTimer();

	private:
		// Allocated by this.
		// Others.
		ListenIp listenIp;	

		bool rtcpMux{ false };
		bool comedia{ false };
		bool multiSource{ false };

		DepLibSfuShm::ShmCtx shmCtx; // shm writer context, needed here to begin shm initialization and correctly report transport stats
	};
} // namespace RTC

#endif
