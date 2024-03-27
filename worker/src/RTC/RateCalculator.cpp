#define MS_CLASS "RTC::RateCalculator"
// #define MS_LOG_DEV_LEVEL 3

#include "RTC/RateCalculator.hpp"
#include "RTC/SeqManager.hpp"
#include "Logger.hpp"
#include <cmath> // std::trunc()

namespace RTC
{
	void RateCalculator::Update(size_t size, uint64_t nowMs)
	{
		MS_TRACE();

		// Ignore too old data. Should never happen.
		if (nowMs < this->oldestItemStartTime)
			return;

		// Increase bytes.
		this->bytes += size;

		RemoveOldData(nowMs);

		// If the elapsed time from the newest item start time is greater than the
		// item size (in milliseconds), increase the item index.
		if (this->newestItemIndex < 0 || nowMs - this->newestItemStartTime >= this->itemSizeMs)
		{
			this->newestItemIndex++;
			this->newestItemStartTime = nowMs;
			if (this->newestItemIndex >= this->windowItems)
				this->newestItemIndex = 0;

			// Modified by Amir Pauker on 02/05/2024
			// See https://github.com/versatica/mediasoup/issues/1316
			// See also https://lively-video.atlassian.net/browse/VR-221
			MS_ASSERT(
			  this->newestItemIndex != this->oldestItemIndex || this->oldestItemIndex == -1 || this->newestItemIndex,
			  "newest index overlaps with the oldest one");

			if (this->oldestItemIndex == this->newestItemIndex) {
			    MS_WARN_TAG(rtp, "please update versatica issue 1316. "
			            "oldestItemIndex=%d nowMs=%" PRIu64 " "
			            "newestItemStartTime=%" PRIu64 " itemSizeMs=%zu windowItems=%" PRIu16,
			            this->oldestItemIndex, nowMs, this->newestItemStartTime, this->itemSizeMs,
			            this->windowItems);
			}

			// Set the newest item.
			BufferItem& item = this->buffer[this->newestItemIndex];
			item.count       = size;
			item.time        = nowMs;
		}
		else
		{
			// Update the newest item.
			BufferItem& item = this->buffer[this->newestItemIndex];
			item.count += size;
		}

		// Set the oldest item index and time, if not set.
		if (this->oldestItemIndex < 0)
		{
			this->oldestItemIndex     = this->newestItemIndex;
			this->oldestItemStartTime = nowMs;
		}

		this->totalCount += size;

		// Reset lastRate and lastTime so GetRate() will calculate rate again even
		// if called with same now in the same loop iteration.
		this->lastRate = 0;
		this->lastTime = 0;
	}

	uint32_t RateCalculator::GetRate(uint64_t nowMs)
	{
		MS_TRACE();

		if (nowMs == this->lastTime)
			return this->lastRate;

		RemoveOldData(nowMs);

		const float scale = this->scale / this->windowSizeMs;

		this->lastTime = nowMs;
		this->lastRate = static_cast<uint32_t>(std::trunc(this->totalCount * scale + 0.5f));

		return this->lastRate;
	}

	inline void RateCalculator::RemoveOldData(uint64_t nowMs)
	{
		MS_TRACE();

		// No item set.
		if (this->newestItemIndex < 0 || this->oldestItemIndex < 0)
			return;

		const uint64_t newOldestTime = nowMs - this->windowSizeMs;

		// Oldest item already removed.
		if (newOldestTime < this->oldestItemStartTime)
			return;

		// A whole window size time has elapsed since last entry. Reset the buffer.
		if (newOldestTime >= this->newestItemStartTime)
		{
			Reset();

			return;
		}

		while (newOldestTime >= this->oldestItemStartTime)
		{
			BufferItem& oldestItem = this->buffer[this->oldestItemIndex];
			this->totalCount -= oldestItem.count;
			oldestItem.count = 0u;
			oldestItem.time  = 0u;

			if (++this->oldestItemIndex >= this->windowItems)
				this->oldestItemIndex = 0;

			const BufferItem& newOldestItem = this->buffer[this->oldestItemIndex];
			this->oldestItemStartTime       = newOldestItem.time;
		}
	}

	void RtpDataCounter::Update(RTC::RtpPacket* packet, bool parseNAL)
	{
		const uint64_t nowMs = DepLibUV::GetTimeMs();

		this->packets++;
		this->rate.Update(packet->GetSize(), nowMs);

		//update frame cnt
		uint32_t ts = packet->GetTimestamp();
		if (ts == this->last_ts)
			return;
		
		if (this->last_ts == 0u || RTC::SeqManager<uint32_t>::IsSeqHigherThan(ts, this->last_ts)) // first frame or newer pkt
		{
			this->frames++;
			this->last_ts = ts;
		}
		else if( parseNAL && RTC::SeqManager<uint32_t>::IsSeqLowerThan(ts, this->last_ts)) // video: if older pkt arrived, and it is either single or aggregate, let's increment
		{
			uint8_t const* cdata   = packet->GetPayload();
			uint8_t nal = cdata ? *(cdata) & 0x1F : 0u;

			if (nal >= 1 && nal <= 24)
				this->frames++;
		}
	}
} // namespace RTC
