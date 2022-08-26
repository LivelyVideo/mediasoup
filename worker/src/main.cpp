#define MS_CLASS "mediasoup-worker"
// #define MS_LOG_DEV_LEVEL 3
#include "DepLibUV.hpp"
#include "MediaSoupErrors.hpp"
#include "lib.hpp"
#include <cstdlib> // std::_Exit(), std::genenv()
#include <string.h>
#include "Channel/ChannelSocket.hpp"

static constexpr int ConsumerChannelFd{ 3 };
static constexpr int ProducerChannelFd{ 4 };
static constexpr int PayloadConsumerChannelFd{ 5 };
static constexpr int PayloadProducerChannelFd{ 6 };
// Binary length for a 4194304 bytes payload.
static constexpr size_t MessageMaxLength{ 4194308 };
static constexpr size_t PayloadMaxLength{ 4194304 };

ChannelReadFreeFn channelReadFn (
		uint8_t** message,
		uint32_t* messageLen,
		size_t* messageCtx,
		const void* handle,
		ChannelReadCtx ctx)
{
	// Create a ConsumerSocket object and provide a
	// name for the socket

	const char* name = "/tmp/msTest.sock";

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
	fprintf(stderr, "ENTERED channelWriteFn 1: %s\n", message);

	// Write the message to the pipe
	if (messageLen == 0)
			return;
	fprintf(stderr, "ENTERED channelWriteFn 2: \n");

	  //assert(status == 0);
	  int r;

	  uv_write_t *wreq = (uv_write_t *)malloc(sizeof(uv_write_t));
	  //const char *request = "{ \"action\":\"labels\" }";
	  const uv_buf_t buf = uv_buf_init(strdup((char *)message), messageLen);
	  uv_write((uv_write_t *)wreq, reinterpret_cast<uv_stream_t*> (ctx), &buf, 1, after_write);

	return;
}

int main(int argc, char* argv[])
{
	// Ensure we are called by our Node library.
	if (!std::getenv("MEDIASOUP_VERSION"))
	{
		MS_ERROR_STD("you don't seem to be my real father!");

		std::_Exit(EXIT_FAILURE);
	}

	std::string version = std::getenv("MEDIASOUP_VERSION");

	auto statusCode = mediasoup_worker_run(
	  argc,
	  argv,
	  version.c_str(),
	  ConsumerChannelFd,
	  ProducerChannelFd,
	  PayloadConsumerChannelFd,
	  PayloadProducerChannelFd,
	  channelReadFn,
	  nullptr,
	  channelWriteFn,
	  nullptr,
	  nullptr,
	  nullptr,
	  nullptr,
	  nullptr);

	switch (statusCode)
	{
		case 0:
			std::_Exit(EXIT_SUCCESS);
		case 1:
			std::_Exit(EXIT_FAILURE);
		case 42:
			std::_Exit(42);
	}
}
