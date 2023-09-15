/*
 * DepNamedUnixSocket.hpp
 *
 *  Created on: Jan 6, 2023
 *      Author: Amir Pauker
 */

#ifndef WORKER_INCLUDE_DEPNAMEDUNIXSOCKET_HPP_
#define WORKER_INCLUDE_DEPNAMEDUNIXSOCKET_HPP_

#include <string.h>
#include <uv.h>

#include "common.hpp"
#include "Channel/ChannelSocket.hpp"
#include "DepLibUV.hpp"

class DepNamedUnixSocket
{
private:
    struct Buffer {
        uint8_t* buf { nullptr };
        uint8_t* last{ nullptr };
        uint8_t* end { nullptr };
    };

    static constexpr size_t MessageMaxLen{ 1024*1024 };

    // buffer for read and write
    // from/to the client socket
    static struct Buffer inbuf;

    // the abstract socket over which messages
    // are communicated with the worker
    static Channel::ChannelSocket* chnnelSocket;

    // libuv handler for the listening socket that
    // is bind to the named domain name docket
    static uv_pipe_t* uvListenerHandler;

    // handler to a connected client socket
    static uv_pipe_t* uvClient;

    // the full name of the domain name socket
    static std::string socketName;
    static std::string socketDir;

protected:

    // passed to uv_start_read
    static void OnAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
    static void OnRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);

    // callback from uv_write
    static void OnWrite(uv_write_t* req, int status);

    static void OnClose(uv_handle_t* handle)
    {
        delete handle;
    }

    static void OnClientConnect(uv_stream_t *handle, int status);
    static void EmitMessages();

public:
    static bool IsEnabled();
    static void ClassInit(Channel::ChannelSocket* chnnelSocket, uv_loop_t* main_loop);
    static void ClassDestroy();
    // Callback function when the unix socket file is removed.
    static void ClassReinitializeSocket(uv_fs_event_t* handle, const char* filename, int events, int status);
    static std::string GetSocketDir();
    static std::string GetSocketFullName();

    static ChannelReadFreeFn channelReadFn(uint8_t** msg, uint32_t* msgLen, size_t* ctx, const void* handle, ChannelReadCtx rdCtx);
    static void channelWriteFn(const uint8_t* msg, uint32_t msgLen, ChannelWriteCtx wrCtx);
    static void channelWritePackedMsgFn(const uint8_t* msg, uint32_t msgLen, ChannelWriteCtx wrCtx);
    static PayloadChannelReadFreeFn payloadChannelReadFn(uint8_t** msg, uint32_t* msgLen, size_t* ctx, uint8_t** payload, uint32_t* payloadLen, size_t* payloadCapacity, const void* handle, PayloadChannelReadCtx rdCtx);
    static void payloadChannelWriteFn(const uint8_t* msg, uint32_t msgLen, const uint8_t* payload, uint32_t payloadLen, ChannelWriteCtx wrCtx);

};

#endif /* WORKER_INCLUDE_DEPNAMEDUNIXSOCKET_HPP_ */
