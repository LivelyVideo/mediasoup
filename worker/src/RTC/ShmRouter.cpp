#define MS_CLASS "RTC::ShmRouter"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/ShmRouter.hpp"
#include "Logger.hpp"
#include "MediaSoupErrors.hpp"
#include "Utils.hpp"
#include "RTC/ActiveSpeakerObserver.hpp"
#include "RTC/AudioLevelObserver.hpp"
#include "RTC/DirectTransport.hpp"
#include "RTC/PipeTransport.hpp"
#include "RTC/PlainTransport.hpp"
#include "RTC/ShmConsumer.hpp"
#include "RTC/WebRtcTransport.hpp"
#include "RTC/Codecs/Tools.hpp"

#if MEDIASOUP_SHM_ENABLED
#define MEDIASOUP_SHM_ABORT_EVENT MS_ABORT("router event: %s", __FUNCTION__);
#else
#define MEDIASOUP_SHM_ABORT_EVENT
#endif

namespace RTC
{
	/* Instance methods. */

	ShmRouter::ShmRouter(RTC::Shared* shared, const std::string& id, Router::Listener* listener) 
		: Router(shared, id, listener)
	{
		MS_TRACE();

		this->timer = new Timer(this);
		if (!this->timer->IsActive())
		{
			this->timer->Start(1, 5);
		}
	}


	ShmRouter::~ShmRouter()
	{
		MS_TRACE();

        // for each consumer of each producer
        for (auto consumersSetIt: mapProducerIdConsumers)
        {
            auto consumersSet = consumersSetIt.second;

            if (consumersSet->shmCtx.IsOpen())
            {
                consumersSet->shmCtx.CloseReader();
            }

            consumersSet->consumers.clear();

            delete consumersSet;
        }

        mapProducerIdConsumers.clear();

		if (this->timer)
		{
			// TODO: see if we should stop it also on various Pause() conditions, or closeTransport events
			this->timer->Stop();

			if (this->timer->IsActive())
			{
				this->timer->Close();
			}
			delete this->timer;
			this->timer = nullptr;
		}
	}


	int ShmRouter::OpenSharedMemory(DepLibStreamShm::ShmCtx* shmCtx, std::string& producerId)
	{
		int rc;
		std::string streamKey, channelId;

        // we expect the producer id string to be in the format
        // <stream key>$$<rid>
		DepLibStreamShm::ShmCtx::ParseStreamKey(producerId, streamKey, channelId);

		rc = shmCtx->OpenReader(streamKey, channelId);
		if (rc < 0)
		{
			MS_WARN_TAG(shm, "failed to open shm. streamKey=%s channelId=%s",
					streamKey.c_str(), channelId.c_str());
		}
		return rc;
	}

	void ShmRouter::CloseConsumersSet(RTC::ShmRouter::ConsumersSet *consumersSet, std::string& producerId)
	{
        MS_DEBUG_TAG(shm, "closing consumers set, stream %s producer %s",
                consumersSet->shmCtx.GetStreamName(),
                producerId.c_str());

        mapProducerIdConsumers.erase(producerId);

        for (auto c: consumersSet->consumers)
        {
            c->ProducerClosed();
        }

        consumersSet->shmCtx.CloseReader();

        delete consumersSet;
	}

	void ShmRouter::OnTimer(Timer* timer)
	{
		// have one global timer
		// For all readers:
		// a reader is created with the first consumer for a particular producer
		// if has active consumers, read in a cycle, same manner.
		// if no consumers are active, just sit idle
		// when the last consumer is closed, delete a reader

		// for each consumer of each producer
		for (auto consumersSetIt : mapProducerIdConsumers)
		{
		    auto producerId   = consumersSetIt.first;
			auto consumersSet = consumersSetIt.second;

			MS_ASSERT(!consumersSet->consumers.empty(),
			            "empty consumers set in mapProducerIdConsumers, stream: %s chn: %d",
			            consumersSet->shmCtx.GetStreamName(), consumersSet->chn);

			// TODO: handle Coordination of Video Orientation

			if (consumersSet->shmCtx.IsClosing())
			{
			    CloseConsumersSet(consumersSet, producerId);
			    continue;
			}

			// check for producer idle timeout
			uint64_t idleTimeout = DepLibStreamShm::ShmCtx::GetIdleReadTimeoutMs();
			uint64_t curTime = DepLibUV::GetTimeMs();
			if (idleTimeout && consumersSet->lastReadTime + idleTimeout < curTime)
			{
			    if (!consumersSet->shmCtx.IsProducerPaused())
			    {
	                MS_WARN_TAG(shm, "idle producer. "
	                        "stream: %s producer %s",
	                        consumersSet->shmCtx.GetStreamName(), producerId.c_str());

	                CloseConsumersSet(consumersSet, producerId);
	                continue;
			    }
			}

			// the channel app data contains information such as
			// codec info which is required for packets parsing
			if (consumersSet->shmCtx.LoadChannelAppData() < 0)
			{
                MS_WARN_TAG(shm, "failed to load channel app data. "
                        "stream: %s producer %s",
                        consumersSet->shmCtx.GetStreamName(), producerId.c_str());
                continue;
			}

			// consume all available packets from the input
			// channel. If there are no active consumers e.g.
			// all consumers are paused we still consumer all
			// available packets in order to make sure we are
			// not staying behind the source
			for (int numActive = 1;;)
			{
				int          rc;
				uint8_t      buf[2048], *p;
				RtpPacket    *packet;

				p = buf;
				rc = consumersSet->shmCtx.ReadNext(&p, sizeof(buf));
				if (rc < 0)
				{
				    if (rc == DepLibStreamShm::ShmCtx::RC_RESET)
				    {
				        CloseConsumersSet(consumersSet, producerId);
				    }
					break;
				}

                // use for tracking idle producer
                consumersSet->lastReadTime = curTime;

				if (numActive) {
	                packet = RtpPacket::Parse(buf, (p - buf));
	                if (packet == nullptr)
	                {
	                    continue;
	                }

	                RTC::Codecs::Tools::ProcessRtpPacket(
	                        packet, consumersSet->shmCtx.GetMimeType());

	                numActive = 0;

	                std::shared_ptr<RTC::RtpPacket> sharedPacket;
	                for (auto c: consumersSet->consumers)
	                {
	                    if (c->IsActive()) {
	                        c->SendRtpPacket(packet, sharedPacket);
	                        numActive++;
	                    }
	                }
	                delete packet;
				}

				if (!numActive) {
				    // no active consumers, jump to the
				    // most recently received packet
				    consumersSet->shmCtx.SkipHead();
				    break;
				}
			}
		}
	}


	inline void ShmRouter::OnTransportNewConsumer(
	  RTC::Transport* /*transport*/, RTC::Consumer* consumer, std::string& producerId)
	{
		MS_TRACE();

		// find a consumers set corresponding
		// to the specified producer id
		auto consumersSetIt = this->mapProducerIdConsumers.find(producerId);

		ConsumersSet* consumersSet;

		// no such set
		if (consumersSetIt == this->mapProducerIdConsumers.end())
		{
		    consumersSet = new ConsumersSet();

		    // at this point we have to open the source
		    // shm to make sure the producer is live
            int rc = this->OpenSharedMemory(&consumersSet->shmCtx, producerId);
            if (rc < 0)
            {
                delete consumersSet;
                MS_THROW_ERROR("Producer not found [producerId:%s]", producerId.c_str());
            }
            // the channel within the source shared memory that
            // corresponds to the specified producer id
            consumersSet->chn = rc;

		    consumersSet->consumers.insert(consumer);
		    this->mapProducerIdConsumers.emplace(producerId, consumersSet);
		} else {
		    consumersSet = consumersSetIt->second;
		    consumersSet->consumers.insert(consumer);
		}

		ShmConsumer *shmConsumer = dynamic_cast<ShmConsumer*>(consumer);
		if (shmConsumer) {
		    shmConsumer->SetSharedMemoryCtx(&consumersSet->shmCtx);
		}
	}


	void ShmRouter::OnTransportConsumerClosed(RTC::Transport* /*transport*/, RTC::Consumer* consumer)
	{
		MS_TRACE();

        // find a consumers set corresponding
        // to the specified producer id
        auto consumersSetIt = this->mapProducerIdConsumers.find(consumer->producerId);

		MS_ASSERT(consumersSetIt != this->mapProducerIdConsumers.end(),
			"producer not found in mapProducerIdConsumers, producer: %s consumer: %s",
			consumer->producerId.c_str(), consumer->id.c_str());

		ConsumersSet* consumerSet = consumersSetIt->second;
		consumerSet->consumers.erase(consumer);

		// if producer has no consumers
		if (consumerSet->consumers.empty())
		{
		    MS_DEBUG_TAG(shm, "closing consumers set. producer: %s", consumer->producerId.c_str());

		    if (consumerSet->shmCtx.IsOpen()) {
		        consumerSet->shmCtx.CloseReader();
		    }
            mapProducerIdConsumers.erase(consumer->producerId);
            delete consumerSet;
		}
	}

    inline void ShmRouter::OnTransportConsumerProducerClosed(
      RTC::Transport* /*transport*/, RTC::Consumer* consumer)
    {
        MS_TRACE();
    }

    inline void ShmRouter::OnTransportConsumerKeyFrameRequested(
      RTC::Transport* /*transport*/, RTC::Consumer* consumer, uint32_t mappedSsrc)
    {
        MS_TRACE();
        auto consumersSetIt = this->mapProducerIdConsumers.find(consumer->producerId);

        if (consumersSetIt == this->mapProducerIdConsumers.end()) {
            MS_WARN_TAG(shm, "failed to request a key frame. producer not found. producer: %s",
                    consumer->producerId.c_str());
            return;
        }

        ConsumersSet* consumerSet = consumersSetIt->second;

        if (!consumerSet->shmCtx.IsOpen()) {
            MS_WARN_TAG(shm, "failed to request a key frame. shm is closed. producer: %s",
                    consumer->producerId.c_str());
            return;
        }

        consumerSet->shmCtx.RequestKeyframe(consumerSet->chn);
    }

} // namespace RTC
