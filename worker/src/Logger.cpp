#define MS_CLASS "Logger"
// #define MS_LOG_DEV_LEVEL 3

#include "Logger.hpp"
#include <uv.h>

/* Class variables. */

const uint64_t Logger::pid{ static_cast<uint64_t>(uv_os_getpid()) };
thread_local Channel::ChannelSocket* Logger::channel{ nullptr };
thread_local char Logger::buffer[Logger::bufferSize];

/* Class methods. */

void Logger::ClassInit(Channel::ChannelSocket* channel)
{
	Logger::channel = channel;

	MS_TRACE();
}

// Add by Amir Pauker 03/13/2023 to support timestamp in log entries
const char* Logger::GetCurrentTimeStr()
{
	static uint64_t current_usec;
	static uint64_t current_msec;
	static uint64_t next_day_msec;
	static char log_time[sizeof("yyyy/MM/dd HH:mm:ss,uuu")];

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
	static struct timespec last_clock_gettime = { 0, 0 };
#endif

	struct timeval tv;
	time_t timestamp;
	struct tm* gmt;
	uint64_t h, m, s, ms;

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0 && defined(_POSIX_MONOTONIC_CLOCK)
	if (!current_usec)
	{
		if (gettimeofday(&tv, NULL) < 0)
		{
			MS_ERROR("failed to update time. %s", strerror(errno));
		}
		else
		{
			if (clock_gettime(CLOCK_MONOTONIC, &last_clock_gettime) < 0)
			{
				MS_ERROR("failed to update time. %s", strerror(errno));
			}
			else
			{
				current_usec = tv.tv_sec * 1000000 + tv.tv_usec;
				current_msec = current_usec / 1000;
			}
		}
	}
	else
	{
		struct timespec tp;
		if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0)
		{
			MS_ERROR("failed to update time. %s", strerror(errno));
		}
		else
		{
			uint64_t usec = (tp.tv_sec - last_clock_gettime.tv_sec) * 1000000 +
			                (tp.tv_nsec - last_clock_gettime.tv_nsec) / 1000;

			last_clock_gettime = tp;

			current_usec += usec;
			current_msec = current_usec / 1000;
		}
	}

#else
	if (gettimeofday(&tv, NULL) < 0)
	{
		MS_ERROR("failed to update time. %s", strerror(errno));
	}
	else
	{
		current_usec = tv.tv_sec * 1000000 + tv.tv_usec;
		current_msec = current_usec / 1000;
	}
#endif

	// first update time ever or reached the next day
	if (current_msec >= next_day_msec)
	{
		timestamp = current_msec / 1000;
		gmt       = gmtime(&timestamp);

		if (gmt)
		{
			snprintf(
			  log_time,
			  sizeof(log_time),
			  "%4d-%02d-%02d %02d:%02d:%02d,%03" PRIu64,
			  gmt->tm_year + 1900,
			  gmt->tm_mon + 1,
			  gmt->tm_mday,
			  gmt->tm_hour,
			  gmt->tm_min,
			  gmt->tm_sec,
			  (current_msec % 1000));

			next_day_msec = ((current_msec / 86400000) + 1) * 86400000;
		}
		else
		{
			MS_ERROR("failed to update time. %s", strerror(errno));
		}
	}
	else
	{
		ms = current_msec % 1000;
		s  = (current_msec / 1000) % 60;
		m  = (current_msec / 60000) % 60;
		h  = (current_msec / 3600000) % 24;

		snprintf(
		  log_time + 11,
		  sizeof(log_time) - 11,
		  "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ",%03" PRIu64,
		  h,
		  m,
		  s,
		  ms);
	}

	return static_cast<const char*>(log_time);
}
