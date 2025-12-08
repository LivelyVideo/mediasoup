#define MS_CLASS "Logger"
// #define MS_LOG_DEV_LEVEL 3

#include "Logger.hpp"
#include "RTC/Shared.hpp"
#include "FBS/log.h"
#include <uv.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

/* Class variables. */

const uint64_t Logger::Pid{ static_cast<uint64_t>(uv_os_getpid()) };
thread_local Channel::ChannelSocket* Logger::channel{ nullptr };
thread_local char Logger::buffer[Logger::BufferSize];
// Lively-specific: for appData logging support
thread_local std::string Logger::appdataBuffer = "";
std::string Logger::levelPrefix;
uint64_t Logger::pid{ static_cast<uint64_t>(uv_os_getpid()) };

// Lively binary logging variables
static RTC::Shared* shared{ nullptr };
static bool openLogFile{ false };
static std::string logfilename;
static FILE* logfd{ nullptr };
static std::string backupBuffer;

/* Class methods. */

void Logger::ClassInit(Channel::ChannelSocket* channel)
{
	Logger::channel = channel;

	MS_TRACE();
}

/* Lively binary logging implementations */

bool Logger::MSlogopen(const FBS::Request::Request* request, RTC::Shared* sharedPtr)
{
	MS_TRACE();

	shared = sharedPtr;

	// Extract log file name from request body
	// TODO: Parse FBS request body for log filename when FBS schema is added
	// For now, use default filename
	logfilename = "/var/log/sfu/mediasoup.log";

	logfd = std::fopen(logfilename.c_str(), "a");
	if (logfd)
	{
		openLogFile = true;
		return true;
	}

	// Failed to open
	MS_WARN_DEV("Failed to open log file: %s, error: %s", logfilename.c_str(), strerror(errno));
	logfd = nullptr;
	return false;
}

void Logger::MSlogrotate()
{
	MS_TRACE_STD();

	if (openLogFile)
	{
		// Close and reopen to refresh fd
		MSlogclose();

		logfd = std::fopen(logfilename.c_str(), "a");
		if (logfd)
		{
			return;
		}
	}

	MS_WARN_DEV("Failed to rotate log file: %s, error: %s", logfilename.c_str(), strerror(errno));
}

void Logger::MSlogwrite(int written)
{
	// Write backed up buffer first, the one that we failed to write on a previous attempt.
	if (!backupBuffer.empty())
	{
		if (!openLogFile || !logfd ||
		    (EOF == std::fputs(backupBuffer.c_str(), logfd)) ||
		    (EOF == std::fputc('\n', logfd)))
		{
			// If failed to write previously saved log msg, send notification to Node.js
			if (shared)
			{
				auto notification = FBS::Log::CreateWriteFailedNotificationDirect(
				  shared->channelNotifier->GetBufferBuilder(),
				  "write",
				  strerror(errno),
				  logfilename.c_str(),
				  backupBuffer.c_str());

				shared->channelNotifier->Emit(
				  std::to_string(pid),
				  FBS::Notification::Event::LOGGER_WRITE_FAILED,
				  FBS::Notification::Body::Log_WriteFailedNotification,
				  notification);
			}

			// Back up a new log msg, try writing it out next time
			backupBuffer.assign(Logger::buffer, written);
			return;
		}

		backupBuffer.clear();
	}

	// Write a new log message. If failed, save it for another time
	if (!openLogFile || !logfd ||
	    (EOF == std::fputs(Logger::buffer, logfd)) ||
	    (EOF == std::fputs("\n", logfd)))
	{
		backupBuffer.assign(Logger::buffer, written);

		// Try refreshing file descriptor and hope file write succeeds next time
		if (!logfilename.empty())
			MSlogrotate();
	}
}

void Logger::MSlogclose()
{
	MS_TRACE_STD();

	if (logfd)
	{
		std::fclose(logfd);
		logfd = nullptr;
	}
}
