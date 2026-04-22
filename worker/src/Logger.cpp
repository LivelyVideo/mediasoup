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

std::string Logger::levelPrefix;
const int64_t Logger::pid{ static_cast<int64_t>(uv_os_getpid()) };
thread_local char Logger::buffer[Logger::bufferSize];
thread_local std::string Logger::backupBuffer = "";
// Lively-specific: for appData logging support
thread_local std::string Logger::appdataBuffer = "";
std::string Logger::logfilename = "";
std::FILE* Logger::logfd {nullptr};
bool Logger::openLogFile {false};
RTC::Shared* Logger::shared {nullptr};

/* Lively binary logging implementations */

bool Logger::MSlogopen(const FBS::Request::Request* request, RTC::Shared* sharedPtr)
{
	MS_TRACE();

	Logger::shared = sharedPtr;

	// Extract log file name from request body
	const auto* body = request->body_as<FBS::Log::MslogOpenRequest>();
	Logger::logfilename = body->mslogname()->str();

	Logger::logfd = std::fopen(Logger::logfilename.c_str(), "a");
	if (Logger::logfd)
	{
		Logger::openLogFile = true;
		return true;
	}

	// Failed to open
	MS_WARN_DEV("Failed to open log file: %s, error: %s", Logger::logfilename.c_str(), strerror(errno));

	if (Logger::shared)
	{
		auto notification = FBS::Log::CreateWriteFailedNotificationDirect(
		  Logger::shared->channelNotifier->GetBufferBuilder(),
		  "open",
		  strerror(errno),
		  Logger::logfilename.c_str(),
		  "");

		Logger::shared->channelNotifier->Emit(
		  std::to_string(pid),
		  FBS::Notification::Event::LOGGER_WRITE_FAILED,
		  FBS::Notification::Body::Log_WriteFailedNotification,
		  notification);
	}

	Logger::logfd = nullptr;
	return false;
}

void Logger::MSlogrotate()
{
	MS_TRACE_STD();

	if (Logger::openLogFile)
	{
		// Close and reopen to refresh fd
		MSlogclose();

		Logger::logfd = std::fopen(Logger::logfilename.c_str(), "a");
		if (Logger::logfd)
		{
			return;
		}
	}

	MS_WARN_DEV("Failed to rotate log file: %s, error: %s", Logger::logfilename.c_str(), strerror(errno));

	if (Logger::shared)
	{
		auto notification = FBS::Log::CreateWriteFailedNotificationDirect(
		  Logger::shared->channelNotifier->GetBufferBuilder(),
		  "rotate",
		  strerror(errno),
		  Logger::logfilename.c_str(),
		  "");

		Logger::shared->channelNotifier->Emit(
		  std::to_string(pid),
		  FBS::Notification::Event::LOGGER_WRITE_FAILED,
		  FBS::Notification::Body::Log_WriteFailedNotification,
		  notification);
	}
}

void Logger::MSlogwrite(int written)
{
	// Write backed up buffer first, the one that we failed to write on a previous attempt.
	if (!Logger::backupBuffer.empty())
	{
		if (!Logger::openLogFile || !Logger::logfd ||
		    (EOF == std::fputs(Logger::backupBuffer.c_str(), Logger::logfd)) ||
		    (EOF == std::fputc('\n', Logger::logfd)))
		{
			// If failed to write previously saved log msg, send notification to Node.js
			if (Logger::shared)
			{
				auto notification = FBS::Log::CreateWriteFailedNotificationDirect(
				  Logger::shared->channelNotifier->GetBufferBuilder(),
				  "write",
				  strerror(errno),
				  Logger::logfilename.c_str(),
				  Logger::backupBuffer.c_str());

				Logger::shared->channelNotifier->Emit(
				  std::to_string(pid),
				  FBS::Notification::Event::LOGGER_WRITE_FAILED,
				  FBS::Notification::Body::Log_WriteFailedNotification,
				  notification);
			}

			// Back up a new log msg, try writing it out next time
			Logger::backupBuffer.assign(Logger::buffer, written);
			return;
		}

		Logger::backupBuffer.clear();
	}

	// Write a new log message. If failed, save it for another time
	if (!Logger::openLogFile || !Logger::logfd ||
	    (EOF == std::fputs(Logger::buffer, Logger::logfd)) ||
	    (EOF == std::fputs("\"\n", Logger::logfd)))
	{
		Logger::backupBuffer.assign(Logger::buffer, written);

		// Try refreshing file descriptor and hope file write succeeds next time
		if (!Logger::logfilename.empty())
			MSlogrotate();
	}
}

void Logger::MSlogclose()
{
	MS_TRACE_STD();

	if (Logger::logfd)
	{
		std::fclose(Logger::logfd);
		Logger::logfd = nullptr;
	}
}
