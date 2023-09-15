#define MS_CLASS "RTC::ShmConsumer"
#define MS_LOG_DEV_LEVEL 3

#include "RTC/ShmConsumer.hpp"
#include "DepLibStreamShm.hpp"
#include "DepLibUV.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "RTC/Codecs/Tools.hpp"

namespace RTC
{
	/* Instance methods. */

	ShmConsumer::ShmConsumer(
	  RTC::Shared* shared,
	  const std::string& id,
	  const std::string& producerId,
	  RTC::Consumer::Listener* listener,
	  json& data)
	  : RTC::Consumer::Consumer(shared, id, producerId, listener, data, RTC::RtpParameters::Type::SIMPLE)
	{
		MS_TRACE();

		// Ensure there is a single encoding.
		if (this->consumableRtpEncodings.size() != 1u)
			MS_THROW_TYPE_ERROR("invalid consumableRtpEncodings with size != 1");

		auto& encoding         = this->rtpParameters.encodings[0];
		const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);

		this->keyFrameSupported = RTC::Codecs::Tools::CanBeKeyFrame(mediaCodec->mimeType);

		// Create RtpStreamSend instance for sending a single stream to the remote.
		CreateRtpStream();

		// Create the encoding context for Opus.
		if (
		  mediaCodec->mimeType.type == RTC::RtpCodecMimeType::Type::AUDIO &&
		  (mediaCodec->mimeType.subtype == RTC::RtpCodecMimeType::Subtype::OPUS ||
		   mediaCodec->mimeType.subtype == RTC::RtpCodecMimeType::Subtype::MULTIOPUS))
		{
			RTC::Codecs::EncodingContext::Params params;

			this->encodingContext.reset(
			  RTC::Codecs::Tools::GetEncodingContext(mediaCodec->mimeType, params));

			auto jsonIgnoreDtx = data.find("ignoreDtx");

			if (jsonIgnoreDtx != data.end() && jsonIgnoreDtx->is_boolean())
			{
				auto ignoreDtx = jsonIgnoreDtx->get<bool>();

				this->encodingContext->SetIgnoreDtx(ignoreDtx);
			}
		}

		// NOTE: This may throw.
		this->shared->channelMessageRegistrator->RegisterHandler(
		  this->id,
		  /*channelRequestHandler*/ this,
		  /*payloadChannelRequestHandler*/ nullptr,
		  /*payloadChannelNotificationHandler*/ nullptr);
	}

	ShmConsumer::~ShmConsumer()
	{
		MS_TRACE();

		this->shared->channelMessageRegistrator->UnregisterHandler(this->id);

		delete this->rtpStream;
	}

	void ShmConsumer::SetSharedMemoryCtx(DepLibStreamShm::ShmCtx* shmCtx)
	{
		this->rtpStream->SetSharedMemoryCtx(shmCtx);
	}

	void ShmConsumer::FillJson(json& jsonObject) const
	{
		MS_TRACE();

		// Call the parent method.
		RTC::Consumer::FillJson(jsonObject);

		// Add rtpStream.
		this->rtpStream->FillJson(jsonObject["rtpStream"]);
	}

	void ShmConsumer::FillJsonStats(json& jsonArray) const
	{
		MS_TRACE();

		// Add stats of our send stream.
		jsonArray.emplace_back(json::value_t::object);
		this->rtpStream->FillJsonStats(jsonArray[0]);

		// Add stats of our recv stream.
		if (this->producerRtpStream)
		{
			jsonArray.emplace_back(json::value_t::object);
			this->producerRtpStream->FillJsonStats(jsonArray[1]);
		}
	}

	void ShmConsumer::FillJsonScore(json& jsonObject) const
	{
		MS_TRACE();

		jsonObject["score"]         = this->rtpStream->GetScore();
		jsonObject["producerScore"] = 0;
	}

	void ShmConsumer::HandleRequest(Channel::ChannelRequest* request)
	{
		MS_TRACE();

		switch (request->methodId)
		{
			case Channel::ChannelRequest::MethodId::CONSUMER_REQUEST_KEY_FRAME:
			{
				// if (IsActive())
				RequestKeyFrame();

				request->Accept();

				break;
			}

			case Channel::ChannelRequest::MethodId::CONSUMER_SET_PREFERRED_LAYERS:
			{
				// Do nothing.

				request->Accept();

				break;
			}

			default:
			{
				// Pass it to the parent class.
				RTC::Consumer::HandleRequest(request);
			}
		}
	}

	void ShmConsumer::ProducerRtpStream(RTC::RtpStreamRecv* rtpStream, uint32_t /*mappedSsrc*/)
	{
		MS_TRACE();

		this->producerRtpStream = rtpStream;
	}

	void ShmConsumer::ProducerNewRtpStream(RTC::RtpStreamRecv* rtpStream, uint32_t /*mappedSsrc*/)
	{
		MS_TRACE();

		this->producerRtpStream = rtpStream;

		// Emit the score event.
		EmitScore();
	}

	void ShmConsumer::ProducerRtpStreamScore(
	  RTC::RtpStreamRecv* /*rtpStream*/, uint8_t /*score*/, uint8_t /*previousScore*/)
	{
		MS_TRACE();

		// Emit the score event.
		EmitScore();
	}

	void ShmConsumer::ProducerRtcpSenderReport(RTC::RtpStreamRecv* /*rtpStream*/, bool /*first*/)
	{
		MS_TRACE();

		// Do nothing.
	}

	// FYI
	// BWE = bandwidth estimation
	// https://stackoverflow.com/questions/44517546/webrtc-goog-remb-and-transport-cc-sdp-lines
	// To summarize: goog-remb and transport-cc are both congestion control mechanisms,
	// goog-remb being an older method and transport-cc being a newer method.

	// Shm simple consumer doesn't play the bandwidth estimate game
	// ------------------------------------------------------------------------

	uint8_t ShmConsumer::GetBitratePriority() const
	{
		MS_TRACE();
		return 0u;
	}
	uint32_t ShmConsumer::IncreaseLayer(uint32_t bitrate, bool /*considerLoss*/)
	{
		MS_TRACE();
		return 0u;
	}
	void ShmConsumer::ApplyLayers()
	{
		MS_TRACE();
	}
	uint32_t ShmConsumer::GetDesiredBitrate() const
	{
		MS_TRACE();
		return 0u;
	}
	// ------------------------------------------------------------------------

	void ShmConsumer::SendRtpPacket(RTC::RtpPacket* packet, std::shared_ptr<RTC::RtpPacket>& sharedPacket)
	{
		MS_TRACE();

		// if (!IsActive())
		//	return;

		auto payloadType = packet->GetPayloadType();

		// NOTE: This may happen if this Consumer supports just some codecs of those
		// in the corresponding Producer.
		if (!this->supportedCodecPayloadTypes[payloadType])
		{
			MS_DEBUG_DEV("payload type not supported [payloadType:%" PRIu8 "]", payloadType);

			packet->logger.Dropped(RtcLogger::RtpPacket::DropReason::UNSUPPORTED_PAYLOAD_TYPE);

			return;
		}

		bool marker;

		// Process the payload if needed. Drop packet if necessary.
		if (this->encodingContext && !packet->ProcessPayload(this->encodingContext.get(), marker))
		{
			MS_DEBUG_DEV(
			  "discarding packet [ssrc:%" PRIu32 ", seq:%" PRIu16 ", ts:%" PRIu32 "]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber(),
			  packet->GetTimestamp());

			this->rtpSeqManager.Drop(packet->GetSequenceNumber());

			packet->logger.Dropped(RtcLogger::RtpPacket::DropReason::DROPPED_BY_CODEC);

			return;
		}

		// If we need to sync, support key frames and this is not a key frame, ignore
		// the packet.
		if (this->syncRequired && this->keyFrameSupported && !packet->IsKeyFrame())
		{
			packet->logger.Dropped(RtcLogger::RtpPacket::DropReason::NOT_A_KEYFRAME);

			return;
		}

		// Whether this is the first packet after re-sync.
		const bool isSyncPacket = this->syncRequired;

		// Sync sequence number and timestamp if required.
		if (isSyncPacket)
		{
			if (packet->IsKeyFrame())
				MS_DEBUG_TAG(rtp, "sync key frame received");

			this->syncRequired = false;
		}

		// Save original packet fields.
		auto origSsrc = packet->GetSsrc();

		// Rewrite packet.
		packet->SetSsrc(this->rtpParameters.encodings[0].ssrc);

		if (isSyncPacket)
		{
			MS_DEBUG_TAG(
			  rtp,
			  "sending sync packet [ssrc:%" PRIu32 ", seq:%" PRIu16 ", ts:%" PRIu32 "]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber(),
			  packet->GetTimestamp());
		}

		// Process the packet.
		if (this->rtpStream->ReceivePacket(packet, sharedPacket))
		{
			// Send the packet.
			this->listener->OnConsumerSendRtpPacket(this, packet);
		}
		else
		{
			MS_WARN_TAG(
			  rtp,
			  "failed to send packet [ssrc:%" PRIu32 ", seq:%" PRIu16 ", ts:%" PRIu32 "]",
			  packet->GetSsrc(),
			  packet->GetSequenceNumber(),
			  packet->GetTimestamp());
		}

		// Restore packet fields.
		packet->SetSsrc(origSsrc);
	}

	bool ShmConsumer::GetRtcp(RTC::RTCP::CompoundPacket* packet, uint64_t nowMs)
	{
		MS_TRACE();

		if (static_cast<float>((nowMs - this->lastRtcpSentTime) * 1.15) < this->maxRtcpInterval)
			return true;

		auto* senderReport = this->rtpStream->GetRtcpSenderReport(nowMs);

		if (!senderReport)
			return true;

		// Build SDES chunk for this sender.
		auto* sdesChunk = this->rtpStream->GetRtcpSdesChunk();

		RTC::RTCP::DelaySinceLastRr* delaySinceLastRrReport{ nullptr };

		auto* dlrr = this->rtpStream->GetRtcpXrDelaySinceLastRr(nowMs);

		if (dlrr)
		{
			delaySinceLastRrReport = new RTC::RTCP::DelaySinceLastRr();
			delaySinceLastRrReport->AddSsrcInfo(dlrr);
		}

		// RTCP Compound packet buffer cannot hold the data.
		if (!packet->Add(senderReport, sdesChunk, delaySinceLastRrReport))
			return false;

		this->lastRtcpSentTime = nowMs;

		return true;
	}

	void ShmConsumer::NeedWorstRemoteFractionLost(uint32_t /*mappedSsrc*/, uint8_t& worstRemoteFractionLost)
	{
		MS_TRACE();

		if (!IsActive())
			return;

		auto fractionLost = this->rtpStream->GetFractionLost();

		// If our fraction lost is worse than the given one, update it.
		if (fractionLost > worstRemoteFractionLost)
			worstRemoteFractionLost = fractionLost;
	}

	void ShmConsumer::ReceiveNack(RTC::RTCP::FeedbackRtpNackPacket* nackPacket)
	{
		MS_TRACE();

		if (!IsActive())
			return;

		// May emit 'trace' event.
		// EmitTraceEventNackType();

		this->rtpStream->ReceiveNack(nackPacket);
	}

	void ShmConsumer::ReceiveKeyFrameRequest(RTC::RTCP::FeedbackPs::MessageType messageType, uint32_t ssrc)
	{
		MS_TRACE();

		this->rtpStream->ReceiveKeyFrameRequest(messageType);
		// if (IsActive())
		RequestKeyFrame();
	}

	void ShmConsumer::ReceiveRtcpReceiverReport(RTC::RTCP::ReceiverReport* report)
	{
		MS_TRACE();

		this->rtpStream->ReceiveRtcpReceiverReport(report);
	}

	void ShmConsumer::ReceiveRtcpXrReceiverReferenceTime(RTC::RTCP::ReceiverReferenceTime* report)
	{
		MS_TRACE();

		this->rtpStream->ReceiveRtcpXrReceiverReferenceTime(report);
	}

	uint32_t ShmConsumer::GetTransmissionRate(uint64_t nowMs)
	{
		MS_TRACE();

		if (!IsActive())
			return 0u;

		return this->rtpStream->GetBitrate(nowMs);
	}

	float ShmConsumer::GetRtt() const
	{
		MS_TRACE();

		return this->rtpStream->GetRtt();
	}

	void ShmConsumer::UserOnTransportConnected()
	{
		MS_TRACE();

		this->syncRequired = true;

		// if (IsActive())
		RequestKeyFrame();
	}

	void ShmConsumer::UserOnTransportDisconnected()
	{
		MS_TRACE();

		this->rtpStream->Pause();
	}

	void ShmConsumer::UserOnPaused()
	{
		MS_TRACE();

		this->rtpStream->Pause();

		if (this->externallyManagedBitrate && this->kind == RTC::Media::Kind::VIDEO)
			this->listener->OnConsumerNeedZeroBitrate(this);
	}

	void ShmConsumer::UserOnResumed()
	{
		MS_TRACE();

		this->syncRequired = true;

		// TODO: check IsActive
		// if (IsActive())
		RequestKeyFrame();
	}

	void ShmConsumer::CreateRtpStream()
	{
		MS_TRACE();

		auto& encoding         = this->rtpParameters.encodings[0];
		const auto* mediaCodec = this->rtpParameters.GetCodecForEncoding(encoding);

		MS_DEBUG_TAG(
		  rtp, "[ssrc:%" PRIu32 ", payloadType:%" PRIu8 "]", encoding.ssrc, mediaCodec->payloadType);

		// Set stream params.
		RTC::RtpStream::Params params;

		params.ssrc        = encoding.ssrc;
		params.payloadType = mediaCodec->payloadType;
		params.mimeType    = mediaCodec->mimeType;
		params.clockRate   = mediaCodec->clockRate;
		params.cname       = this->rtpParameters.rtcp.cname;

		// Check in band FEC in codec parameters.
		if (mediaCodec->parameters.HasInteger("useinbandfec") && mediaCodec->parameters.GetInteger("useinbandfec") == 1)
		{
			MS_DEBUG_TAG(rtp, "in band FEC enabled");

			params.useInBandFec = true;
		}

		// Check DTX in codec parameters.
		if (mediaCodec->parameters.HasInteger("usedtx") && mediaCodec->parameters.GetInteger("usedtx") == 1)
		{
			MS_DEBUG_TAG(rtp, "DTX enabled");

			params.useDtx = true;
		}

		// Check DTX in the encoding.
		if (encoding.dtx)
		{
			MS_DEBUG_TAG(rtp, "DTX enabled");

			params.useDtx = true;
		}

		for (const auto& fb : mediaCodec->rtcpFeedback)
		{
			if (!params.useNack && fb.type == "nack" && fb.parameter.empty())
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "NACK supported");

				params.useNack = true;
			}
			else if (!params.usePli && fb.type == "nack" && fb.parameter == "pli")
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "PLI supported");

				params.usePli = true;
			}
			else if (!params.useFir && fb.type == "ccm" && fb.parameter == "fir")
			{
				MS_DEBUG_2TAGS(rtp, rtcp, "FIR supported");

				params.useFir = true;
			}
		}

		this->rtpStream = new RTC::RtpStreamSend(this, params, this->rtpParameters.mid);
		this->rtpStreams.push_back(this->rtpStream);

		// If the Consumer is paused, tell the RtpStreamSend.
		if (IsPaused() || IsProducerPaused())
			this->rtpStream->Pause();

		const auto* rtxCodec = this->rtpParameters.GetRtxCodecForEncoding(encoding);

		if (rtxCodec && encoding.hasRtx)
			this->rtpStream->SetRtx(rtxCodec->payloadType, encoding.rtx.ssrc);
	}

	void ShmConsumer::RequestKeyFrame()
	{
		MS_TRACE();

		if (this->kind != RTC::Media::Kind::VIDEO)
			return;

		auto mappedSsrc = this->consumableRtpEncodings[0].ssrc;

		this->listener->OnConsumerKeyFrameRequested(this, mappedSsrc);
	}

	inline void ShmConsumer::EmitScore() const
	{
		MS_TRACE();

		json data = json::object();

		FillJsonScore(data);

		this->shared->channelNotifier->Emit(this->id, "score", data);
	}

	inline void ShmConsumer::OnRtpStreamScore(
	  RTC::RtpStream* /*rtpStream*/, uint8_t /*score*/, uint8_t /*previousScore*/)
	{
		MS_TRACE();

		// Emit the score event.
		EmitScore();
	}

	inline void ShmConsumer::OnRtpStreamRetransmitRtpPacket(
	  RTC::RtpStreamSend* rtpStream, RTC::RtpPacket* packet)
	{
		MS_TRACE();

		this->listener->OnConsumerRetransmitRtpPacket(this, packet);

		// May emit 'trace' event.
		// EmitTraceEventRtpAndKeyFrameTypes(packet, this->rtpStream->HasRtx());
	}
} // namespace RTC
