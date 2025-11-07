#define MS_CLASS "RTC::ShmTransport"

#include "RTC/ShmTransport.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Utils.hpp"
#include "Lively.hpp"
#include "LivelyAppDataToJson.hpp"

namespace RTC
{
	/* Instance methods. */

	ShmTransport::ShmTransport(RTC::Shared* shared, const std::string& id, RTC::Transport::Listener* listener, const FBS::Transport::Options* options)
	  : RTC::Transport::Transport(shared, id, listener, options)
	{
		MS_TRACE();
		/*
			{ 
				"listenIp": '127.0.0.1',
				"shm": {
				    "clientReferrer": <the namespace (also called clientReferrer or project id)>,
					"name": <public key>,
					"queueAge": 100,
					"testNack": 0,
					"reverseIt": 0,
					"shmAppData": "..."
				},
				appData: {
					callId: "...",
					streamName: "...",
					peerId: "...",
					mirrorId: "...",
				},
				"log": {
					"name": /var/log/sg/nginx/test_sfu_shm.log",
					"level": 9,
					"stdio": 1
				}
			}
		*/
		MS_DEBUG_TAG_LIVELYAPP(xcode, this->appData, "ShmTransport ctor[transportId:%s]", this->id.c_str());

		// Parse shm configuration from FlatBuffers
		auto* shmOptions = options->shm();
		if (!shmOptions)
			MS_THROW_TYPE_ERROR("missing shm options in ShmTransport");

        // NOTE: RND-6440 in order to support true multi-tenant environment
        // we use namespace in shm. not ideal but we use three different
        // terms for the same thing )-: namespace, clientReferrer and
        // project id

		// Read shm.clientReferrer, shm.name
		std::string clientReferrer;
		std::string shm;

		if (!shmOptions->clientReferrer())
			MS_THROW_TYPE_ERROR("missing shm.clientReferrer in ShmTransport");
		clientReferrer.assign(shmOptions->clientReferrer()->str());

		if (!shmOptions->name())
			MS_THROW_TYPE_ERROR("missing shm.name in ShmTransport");
		shm.assign(shmOptions->name()->str());

		// Read shm.queueAge in ms (default 100)
		auto queueAge = shmOptions->queueAge();

		// Read shm.testNack in ms, default is 0 which means disabled NACK testing
		auto testNack = shmOptions->testNack();

		// Perf testing: use forward or reverse iterator to place incoming chunks into video buffer
		bool useReverse = (shmOptions->reverseIt() != 0);

		// Read shmAppData
		std::string shmAppData;
		if (shmOptions->shmAppData()) {
			shmAppData.assign(shmOptions->shmAppData()->str());
		}

		// ngxshm log name and level
		auto* logOptions = options->log();
		if (!logOptions)
			MS_THROW_TYPE_ERROR("missing log options in ShmTransport");

		if (!logOptions->name())
			MS_THROW_TYPE_ERROR("missing log.name in ShmTransport");

		std::string logname;
		logname.assign(logOptions->name()->str());

		auto loglevel = logOptions->level(); // default is 9 in schema

		// Parse listenIp from FlatBuffers
		auto* listenIpOptions = options->listenIp();
		if (!listenIpOptions)
			MS_THROW_TYPE_ERROR("missing listenIp in ShmTransport");

		if (!listenIpOptions->ip())
			MS_THROW_TYPE_ERROR("missing listen_ip.ip in ShmTransport");

		this->listenIp.ip.assign(listenIpOptions->ip()->str());

		// This may throw.
		Utils::IP::NormalizeIp(this->listenIp.ip);

		if (listenIpOptions->announcedIp())
		{
			this->listenIp.announcedIp.assign(listenIpOptions->announcedIp()->str());
		}
		// NOTE: This may throw.
		this->shared->channelMessageRegistrator->RegisterHandler(
		  this->id,
		  /*channelRequestHandler*/ this,
		  /*channelNotificationHandler*/ this);

		this->shmCtx.InitializeShmWriterCtx(
		        clientReferrer,
		        shm,
		        queueAge,
		        useReverse,
		        testNack,
		        logname /* + "." + shm + "." + this->id */,
		        loglevel,
		        shmAppData);

		this->shmNoConsumeTimer = new TimerHandle(this);
		this->shmNoConsumeTimer->Start(60000);
	}


	ShmTransport::~ShmTransport()
	{
		MS_TRACE();

		this->shared->channelMessageRegistrator->UnregisterHandler(this->id);

		MS_DEBUG_TAG_LIVELYAPP(xcode, this->appData, "shm[%s] ShmTransport dtor[transportId:%s]", this->shmCtx.StreamName().c_str(), this->id.c_str());
		this->shmCtx.CloseShmWriterCtx();
		delete this->shmNoConsumeTimer;
	}


	inline void ShmTransport::OnTimer(TimerHandle* timer)
	{
		MS_TRACE();

		if (timer == this->shmNoConsumeTimer)
			this->OnNoConsume();
		else
			// Call the base
			RTC::Transport::OnTimer(timer);
	}


	void ShmTransport::StopNoConsumeTimer()
	{
		MS_TRACE();

		this->shmNoConsumeTimer->Stop();
	}

	void ShmTransport::OnNoConsume()
	{
		MS_TRACE();

		MS_WARN_TAG_LIVELYAPP(xcode, this->appData, "shm[%s] idle timeout: no consumers created in ShmTransport [transportId:%s]", this->shmCtx.StreamName().c_str(), this->id.c_str());
	
		// Close shm writer and let go of shm.
		this->shmCtx.CloseShmWriterCtx();

		// TBD: ShmTransport object will live until someone calls a dtor.
		// Stop the time so that nothing ever happens here.
		this->shmNoConsumeTimer->Stop();
	}


	flatbuffers::Offset<FBS::ShmTransport::DumpResponse> ShmTransport::FillBuffer(flatbuffers::FlatBufferBuilder& builder) const
	{
		MS_TRACE();

		// Get base Transport dump
		auto baseDump = RTC::Transport::FillBuffer(builder);

		// Convert ShmWriterStatus to FlatBuffers enum
		// Note: DepLibSfuShm only has: SHM_WRT_UNDEFINED, SHM_WRT_READY, SHM_WRT_CLOSED
		FBS::ShmTransport::ShmWriterStatus shmStatus;
		switch (this->shmCtx.Status())
		{
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_READY:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::READY;
				break;
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_CLOSED:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::CLOSED;
				break;
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_UNDEFINED:
			default:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::NOTSET;
				break;
		}

		// Create SHM-specific dump with all Lively data
		auto shmDump = FBS::ShmTransport::CreateDumpResponseDirect(
		  builder,
		  baseDump,
		  this->shmCtx.StreamName().c_str(),
		  this->shmCtx.LogName().c_str(),
		  shmStatus,
		  this->shmCtx.MaxQueuePktDelayMs(),
		  this->shmCtx.TestNackMs(),
		  this->listenIp.ip.c_str(),
		  this->listenIp.announcedIp.empty() ? nullptr : this->listenIp.announcedIp.c_str(),
		  this->rtcpMux,
		  this->comedia,
		  this->multiSource);

		return shmDump;
	}

	flatbuffers::Offset<FBS::ShmTransport::GetStatsResponse> ShmTransport::FillBufferStats(flatbuffers::FlatBufferBuilder& builder)
	{
		MS_TRACE();

		// Get base Transport stats
		auto baseStats = RTC::Transport::FillBufferStats(builder);

		// Convert ShmWriterStatus to FlatBuffers enum
		// Note: DepLibSfuShm only has: SHM_WRT_UNDEFINED, SHM_WRT_READY, SHM_WRT_CLOSED
		FBS::ShmTransport::ShmWriterStatus shmStatus;
		switch (this->shmCtx.Status())
		{
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_READY:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::READY;
				break;
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_CLOSED:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::CLOSED;
				break;
			case DepLibSfuShm::ShmWriterStatus::SHM_WRT_UNDEFINED:
			default:
				shmStatus = FBS::ShmTransport::ShmWriterStatus::NOTSET;
				break;
		}

		// Create SHM-specific stats
		// Note: video_queue_size is not accessible (private member), set to 0 for now
		auto shmStats = FBS::ShmTransport::CreateGetStatsResponseDirect(
		  builder,
		  baseStats,
		  this->shmCtx.StreamName().c_str(),
		  shmStatus,
		  0, // video_queue_size - TODO: expose this from ShmCtx if needed
		  this->shmCtx.MaxQueuePktDelayMs());

		return shmStats;
	}

	void ShmTransport::SendStreamClosed(uint32_t /*ssrc*/)
	{
		MS_TRACE();

		// Do nothing.
	}

	void ShmTransport::RecvStreamClosed(uint32_t /*ssrc*/)
	{
		MS_TRACE();

		// Do nothing.
	}

	void ShmTransport::SendMessage(
	  RTC::DataConsumer* /*dataConsumer*/, const uint8_t* /*msg*/, size_t /*len*/, uint32_t /*ppid*/, onQueuedCallback* /*cb*/)
	{
		MS_TRACE();

		// Do nothing.
	}

	inline void ShmTransport::OnPacketReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len)
	{
		MS_TRACE();

		// Increase receive transmission.
		RTC::Transport::DataReceived(len);

		// Check if it's RTCP.
		if (RTC::RTCP::Packet::IsRtcp(data, len))
		{
			OnRtcpDataReceived(tuple, data, len);
		}
		// Check if it's RTP.
		else if (RTC::RtpPacket::IsRtp(data, len))
		{
			OnRtpDataReceived(tuple, data, len);
		}
		// Check if it's SCTP.
		else if (RTC::SctpAssociation::IsSctp(data, len))
		{
			OnSctpDataReceived(tuple, data, len);
		}
		else
		{
			MS_WARN_DEV("ignoring received packet of unknown type");
		}
	}

	inline void ShmTransport::OnRtpDataReceived(RTC::TransportTuple* tuple, const uint8_t* data, size_t len)
	{
		MS_TRACE();

		if (!IsConnected())
		{
			return;
		}

		RTC::RtpPacket* packet = RTC::RtpPacket::Parse(data, len);

		if (packet == nullptr)
		{
			MS_WARN_TAG_LIVELYAPP(rtp, this->appData, "received data is not a valid RTP packet");

			return;
		}

		// Pass the packet to the parent transport.
		RTC::Transport::ReceiveRtpPacket(packet);
	}

	inline void ShmTransport::OnRtcpDataReceived(
	  RTC::TransportTuple* tuple, const uint8_t* data, size_t len)
	{
		MS_TRACE();

		if (!IsConnected())
			return;

		RTC::RTCP::Packet* packet = RTC::RTCP::Packet::Parse(data, len);

		if (packet == nullptr)
		{
			MS_WARN_TAG_LIVELYAPP(rtcp, this->appData, "received data is not a valid RTCP compound or single packet");

			return;
		}

		// Pass the packet to the parent transport.
		RTC::Transport::ReceiveRtcpPacket(packet);
	}

	inline void ShmTransport::OnSctpDataReceived(
	  RTC::TransportTuple* tuple, const uint8_t* data, size_t len)
	{
		MS_TRACE();

		if (!IsConnected())
			return;

		// Pass it to the parent transport.
		RTC::Transport::ReceiveSctpData(data, len);
	}


	void ShmTransport::HandleRequest(Channel::ChannelRequest* request)
	{
		MS_TRACE();

		switch (request->method)
		{
			case Channel::ChannelRequest::Method::TRANSPORT_CONNECT:
			{
				if (this->IsConnected())
				{
					MS_THROW_ERROR("transport_connect() already called");
				}
				request->Accept();

				// Tell the parent class.
				RTC::Transport::Connected();

				break;
			}

			case Channel::ChannelRequest::Method::TRANSPORT_CONSUME_STREAM_META:
			{
				const auto* body = request->data->body_as<FBS::Transport::ConsumeStreamMetaRequest>();

				// Extract meta and shm from FlatBuffers request
				std::string metadata(body->meta()->c_str());
				std::string shm(body->shm()->c_str());

				MS_DEBUG_TAG_LIVELYAPP(xcode, this->appData, "shm[%s] received stream metadata [meta:%s, shm:%s]",
					this->shmCtx.StreamName().c_str(), metadata.c_str(), shm.c_str());

				// Write the stream metadata
				if (0 == this->shmCtx.WriteStreamMeta(metadata, shm))
				{
					request->Accept();
				}
				else
				{
					request->Error("ShmTransport::WriteStreamMeta failed");
				}

				break;
			}

			default:
			{
				// Pass it to the parent class.
				RTC::Transport::HandleRequest(request);
			}
		}
	}

	void ShmTransport::HandleNotification(Channel::ChannelNotification* notification)
	{
		MS_TRACE();

		// Pass it to the parent class.
		RTC::Transport::HandleNotification(notification);
	}


	inline bool ShmTransport::IsConnected() const
	{
		return true;
	}


	void ShmTransport::SendRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* /* cb */)
	{
		MS_TRACE();

		if (!IsConnected())
			return;
	
		// Increase send transmission. Consumer writes RTP packets to shm, nothing else to do here.
		RTC::Transport::DataSent(packet->GetSize());
	}


	void ShmTransport::SendRtcpPacket(RTC::RTCP::Packet* packet)
	{
		MS_TRACE();

		if (!IsConnected())
			return;

		// Increase send transmission.
		RTC::Transport::DataSent(packet->GetSize());
	}


	void ShmTransport::SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet)
	{
		MS_TRACE();

		if (!IsConnected())
			return;

		// Increase send transmission.
		RTC::Transport::DataSent(packet->GetSize());
	}



	void ShmTransport::SendSctpData(const uint8_t* data, size_t len)
	{
		MS_TRACE();

		if (!IsConnected())
			return;

		// Increase send transmission.
		RTC::Transport::DataSent(len);
	}


	inline void ShmTransport::OnConsumerNeedBitrateChange(RTC::Consumer* /*consumer*/)
	{
		MS_TRACE();

		// Do nothing.
	}


	inline void ShmTransport::OnUdpSocketPacketReceived(
	  RTC::UdpSocket* socket, const uint8_t* data, size_t len, const struct sockaddr* remoteAddr)
	{
		MS_TRACE();

		RTC::TransportTuple tuple(socket, remoteAddr);

		OnPacketReceived(&tuple, data, len);
	}
} // namespace RTC
