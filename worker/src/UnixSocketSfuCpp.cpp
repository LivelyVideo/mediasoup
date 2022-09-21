/*
 * UnixSocketSfuCpp.cpp
 *
 *  Created on: Sep 16, 2022
 *      Author: mythilikottalanka
 */
#define MS_CLASS "mediasoup-worker"

#include "DepLibUV.hpp"
#include "MediaSoupErrors.hpp"
#include "lib.hpp"
#include <cstdlib> // std::_Exit(), std::genenv()
#include <string.h>
#include <filesystem>
#include <sys/stat.h>
#include "Channel/ChannelSocket.hpp"
#include "UnixSocketSfuCpp.hpp"

static constexpr size_t MessageMaxLength{ 4194308 };
static constexpr size_t PayloadMaxLength{ 4194304 };
#define LEN 10

ChannelReadFreeFn channelReadFn (
		uint8_t** message,
		uint32_t* messageLen,
		size_t* messageCtx,
		const void* handle,
		ChannelReadCtx ctx)
{
	MS_DEBUG_TAG_STD(info, "ENTERED channelReadFn: %s\n", message);

	// Create a ConsumerSocket object and provide a
	// name for the socket

	// Make the socket name

	// Check to see if the user set the directory in the environment variables.
	std::string dir = "" ;
	if(!std::getenv("MEDIASOUP_SOCKET_DIR"))
	{
		// If not, use the /tmp dir
		dir = "tmp";
	}
	else
	{
		dir = std::getenv("MEDIASOUP_SOCKET_DIR");
	}

	// Check to see if the dir already exists
	std::string filePathExistsStr = "/";
	filePathExistsStr.append(dir);

	struct stat sb;
	bool filepathExists = false;
	if (stat(filePathExistsStr.c_str(), &sb) == 0)
	{
		filepathExists = true;
	}

	std::string makeDirStr = "/";
	//makeDirStr.append("/");
	makeDirStr.append(dir);

	// Make the directory if it does not exist
	if (!filepathExists)
	{
	    if(mkdir(makeDirStr.c_str(), 0777))
			MS_ERROR_STD("error  while trying to create the directory for mediasoup sockets\n");
	}

	// Now create the socket name with pid under the user specified (or /tmp) dir
	std::string socketName = GetUnixSocketName();
	std::string socketDirPath = "/";
	socketDirPath.append(dir);
	socketDirPath.append("/");
	socketDirPath.append(socketName);

	const char* name = socketDirPath.c_str();
	new Channel::ConsumerSocket(name, MessageMaxLength, &*((Channel::ChannelSocket*) ctx));
	return nullptr;
}

void after_write(uv_write_t *req, int status) {

}

void channelWriteFn (
		const uint8_t*  message,
		uint32_t  messageLen,
		ChannelWriteCtx  ctx )
{
	MS_DEBUG_TAG_STD(info, "ENTERED channelWriteFn: %s\n", message);

	// Write the message to the pipe
	if (messageLen == 0)
			return;

	//assert(status == 0);
	int r;

	uv_write_t *wreq = (uv_write_t *)malloc(sizeof(uv_write_t));
	//const char *request = "{ \"action\":\"labels\" }";
	const uv_buf_t buf = uv_buf_init(strdup((char *)message), messageLen);
	uv_write((uv_write_t *)wreq, reinterpret_cast<uv_stream_t*> (ctx), &buf, 1, after_write);

	return;
}


