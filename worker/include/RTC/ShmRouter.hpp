#ifndef MS_RTC_SHM_ROUTER_HPP
#define MS_RTC_SHM_ROUTER_HPP

#include "common.hpp"
#include "DepLibStreamShm.hpp"
#include "Channel/ChannelRequest.hpp"
#include "PayloadChannel/PayloadChannelNotification.hpp"
#include "PayloadChannel/PayloadChannelRequest.hpp"
#include "RTC/Router.hpp"
#include "RTC/Consumer.hpp"
#include "RTC/DataConsumer.hpp"
#include "RTC/DataProducer.hpp"
#include "RTC/Producer.hpp"
#include "RTC/RtpObserver.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RtpStream.hpp"
#include "RTC/Shared.hpp"
#include "RTC/Transport.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <absl/container/flat_hash_map.h>
#include <unordered_set>

using json = nlohmann::json;

namespace RTC
{
	class ShmRouter : public RTC::Router, 
								public Timer::Listener
	{
	public:
		explicit ShmRouter(RTC::Shared* shared, const std::string& id, Router::Listener* listener);
		~ShmRouter();

		/* Pure virtual methods inherited from Timer::Listener. */
	public:
		void OnTimer(Timer* timer) override;

		/* Pure virtual methods inherited from RTC::Transport::Listener. */
	public:
		void OnTransportNewConsumer(RTC::Transport* transport, RTC::Consumer* consumer, std::string& producerId) override;
		void OnTransportConsumerClosed(RTC::Transport* transport, RTC::Consumer* consumer) override;
		void OnTransportConsumerProducerClosed(RTC::Transport* transport, RTC::Consumer* consumer) override;
		void OnTransportConsumerKeyFrameRequested(
		          RTC::Transport* transport, RTC::Consumer* consumer, uint32_t mappedSsrc) override;

	protected:
		class ConsumersSet
		{
		public:
		    DepLibStreamShm::ShmCtx shmCtx;                 // shared memory context for reading the input source elementary stream
		    int chn;                                        // the index of the elementary stream inside the shared memory
		    std::unordered_set<RTC::Consumer*> consumers;   // set of consumers which all share the same input source elementary stream
		    uint64_t lastReadTime;                          // timestamp in milliseconds based on server clock of last successful read
		    ConsumersSet()
		    {
		        lastReadTime = DepLibUV::GetTimeMs();
		    }
		};

	private:
		Timer* timer{ nullptr };
		absl::flat_hash_map<std::string, ConsumersSet*> mapProducerIdConsumers;

		int OpenSharedMemory(DepLibStreamShm::ShmCtx* shmCtx, std::string& producerId);
		void CloseConsumersSet(RTC::ShmRouter::ConsumersSet *consumersSet, std::string& producerId);
	};
} // namespace RTC

#endif
