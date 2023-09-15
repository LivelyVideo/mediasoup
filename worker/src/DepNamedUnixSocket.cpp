/*
 * DepNamedUnixSocket.cpp
 *
 *  Created on: Jan 6, 2023
 *      Author: Amir Pauker
 */
#define MS_CLASS "DepNamedUnixSocket"

#include <cstdlib>
#include <string.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "Logger.hpp"

#include "DepNamedUnixSocket.hpp"

static constexpr size_t MsgLenFieldSize = sizeof(uint64_t);
static constexpr uint64_t HealthCheckWord = 0xFEFEFEFE00000000;

struct DepNamedUnixSocket::Buffer DepNamedUnixSocket::inbuf;
Channel::ChannelSocket* DepNamedUnixSocket::chnnelSocket = nullptr;
uv_pipe_t* DepNamedUnixSocket::uvListenerHandler = nullptr;
uv_pipe_t* DepNamedUnixSocket::uvClient = nullptr;
std::string DepNamedUnixSocket::socketName;
std::string DepNamedUnixSocket::socketDir;

// used by the uv_read function when reading from the unix socket
void DepNamedUnixSocket::OnAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    MS_TRACE_STD();

    if (DepNamedUnixSocket::inbuf.last >= DepNamedUnixSocket::inbuf.end) {
        MS_ABORT("channel socket read buffer is full");
    }

    // provide uv_read the remaining space in the buffer
    buf->len = DepNamedUnixSocket::inbuf.end - DepNamedUnixSocket::inbuf.last;
    buf->base = reinterpret_cast<char*>(DepNamedUnixSocket::inbuf.last);
}

// deliver a message to the ChannelSocket as soon as we
// have a complete message in the buffer. The function
// may deliver multiple messages in one call
void DepNamedUnixSocket::EmitMessages()
{
    MS_TRACE_STD();

    // the in buffer may contain more than one message
    while (true)
    {
        size_t readLen = DepNamedUnixSocket::inbuf.last - DepNamedUnixSocket::inbuf.buf;

        // messages format is as follow:
        // <4 bytes sync marker 0xFFFFFFFF><4 bytes length><data>
        if (readLen < MsgLenFieldSize)
        {
            break;
        }

        // get the message length
        uint64_t msgLen = *(reinterpret_cast<uint64_t*>(DepNamedUnixSocket::inbuf.buf));

        // special sync word that is used as a socket health check
        if (msgLen == HealthCheckWord)
        {
            // let the client know that we are a live by
            // emitting the same word in response
            DepNamedUnixSocket::channelWritePackedMsgFn(
                    reinterpret_cast<const uint8_t*>(&HealthCheckWord), sizeof(HealthCheckWord), nullptr);

            // after we consumed the message, move whatever left
            // to the beginning of the buffer in order to free
            // space for new messages
            size_t left = readLen - MsgLenFieldSize;
            if (left) {
                std::memmove(DepNamedUnixSocket::inbuf.buf,
                        DepNamedUnixSocket::inbuf.buf + MsgLenFieldSize,
                        left);
            }
            DepNamedUnixSocket::inbuf.last = DepNamedUnixSocket::inbuf.buf + left;
            continue;
        }

        // make sure we see the sync marker 0xFFFFFFFF
        // before the message length
        if ((msgLen & 0xFFFFFFFF00000000) != 0xFFFFFFFF00000000)
        {
            MS_ERROR_STD("channel socket out of sync. %s. 0x%016X",
                    DepNamedUnixSocket::socketName.c_str(), static_cast<unsigned int>(msgLen));

            // close the socket and tell the client to
            // reconnect. this will re-sync the read
            uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvClient),
                    static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));

            DepNamedUnixSocket::uvClient = nullptr;
        }

        msgLen = msgLen & 0xFFFFFFFF;

        if (readLen < MsgLenFieldSize + static_cast<size_t>(msgLen))
        {
            // Incomplete data.
            break;
        }

        // consume this message
        // IMPORTANT NOTE: at the moment the function OnConsumerSocketMessage
        // accept null for the consumer socket pointer. In case this won't be
        // the case in the future, then this function will have to trigger a
        // call to DepNamedUnixSocket::channelReadFn using by calling
        // ChannelSocket::CallbackRead()
        DepNamedUnixSocket::chnnelSocket->OnConsumerSocketMessage(nullptr,
                reinterpret_cast<char*>(DepNamedUnixSocket::inbuf.buf+MsgLenFieldSize),
                msgLen);

        // after we consumed the message, move whatever left
        // to the beginning of the buffer in order to free
        // space for new messages
        size_t left = readLen - msgLen - MsgLenFieldSize;
        if (left) {
            std::memmove(DepNamedUnixSocket::inbuf.buf,
                    DepNamedUnixSocket::inbuf.buf + (msgLen + MsgLenFieldSize),
                    left);
        }
        DepNamedUnixSocket::inbuf.last = DepNamedUnixSocket::inbuf.buf + left;
    }
}

// called by libuv when there is data
// available in the Unix socket buffer
void DepNamedUnixSocket::OnRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    MS_TRACE_STD();
    if (nread == 0)
        return;

    // Data received.
    if (nread > 0)
    {
        // update the buffer last written position
        DepNamedUnixSocket::inbuf.last += static_cast<size_t>(nread);

        // Notify the ChannelSocket.
        DepNamedUnixSocket::EmitMessages();
    }
    // Peer disconnected or an error occurred.
    if (nread < 0)
    {
        if (nread == UV_EOF || nread == UV_ECONNRESET)
        {
            MS_ERROR_STD("client disconnected from named socket %s", DepNamedUnixSocket::socketName.c_str());
        }
        else
        {
            MS_ERROR_STD("fail to read from named unix socket %s. %s",
                    DepNamedUnixSocket::socketName.c_str(), uv_strerror(nread));
        }

        uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvClient),
                static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));

        DepNamedUnixSocket::uvClient = nullptr;
    }
}

// in case an immediate write failed to deliver a complete
// buffer to the client we use uv_write uv_write calls this
// callback to free allocated buffers
void DepNamedUnixSocket::OnWrite(uv_write_t* req, int status)
{
    delete reinterpret_cast<char*>(req->data);
    delete req;
}

// called by libuv when a client connects to the listening socket
void DepNamedUnixSocket::OnClientConnect(uv_stream_t *handle, int status)
{
    MS_TRACE();

    assert(status == 0);
    int err;

    // the ChannelSocket only supports one connection
    // at a time. This is because responses has to be
    // delivered to the same client that sent the req
    if (DepNamedUnixSocket::uvClient) {
        MS_ERROR_STD("fail to establish connection with worker over "
                "named unix socket %s. client is already connected",
                DepNamedUnixSocket::socketName.c_str());

        // we must accept then close
        uv_pipe_t *tmp = new uv_pipe_t;
        if (!tmp) MS_ABORT("out of memory");
        uv_pipe_init(handle->loop, tmp, 0);
        err = uv_accept(handle, (uv_stream_t *)tmp);
        if (err != 0) MS_ABORT("uv_accept() failed: %s", uv_strerror(err));
        uv_close(reinterpret_cast<uv_handle_t*>(tmp),
                        static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));

        return;
    }

    DepNamedUnixSocket::uvClient = new uv_pipe_t;

    if (!DepNamedUnixSocket::uvClient) {
        MS_ABORT("out of memory");
        return;
    }

    // input buffer must be reset
    DepNamedUnixSocket::inbuf.last = DepNamedUnixSocket::inbuf.buf;

    uv_pipe_init(handle->loop, DepNamedUnixSocket::uvClient, 0);
    err = uv_accept(handle, (uv_stream_t *)DepNamedUnixSocket::uvClient);
    if (err != 0)
        MS_ABORT("uv_accept() failed: %s", uv_strerror(err));

    err = uv_read_start(reinterpret_cast<uv_stream_t*> (DepNamedUnixSocket::uvClient),
                  static_cast<uv_alloc_cb>(DepNamedUnixSocket::OnAlloc),
                  static_cast<uv_read_cb>(DepNamedUnixSocket::OnRead));

    if (err != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvClient),
                static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));

        MS_ERROR_STD("fail to start reading from named unix socket %s. %s",
                DepNamedUnixSocket::socketName.c_str(), uv_strerror(err));

        DepNamedUnixSocket::uvClient = nullptr;
        return;
    }
}

// return true if this library is enabled otherwise
// the traditional pipe sockets will be used
bool DepNamedUnixSocket::IsEnabled()
{
    return !!std::getenv("MEDIASOUP_NAMED_UNIX_SOCKET_ENABLED");
}

std::string DepNamedUnixSocket::GetSocketDir()
{
    return DepNamedUnixSocket::socketDir;
}

std::string DepNamedUnixSocket::GetSocketFullName()
{
    return (DepNamedUnixSocket::socketDir + "/" + DepNamedUnixSocket::socketName);
}

// allocate and open the named unix socket and
// associate it with the given ChannelSocket
void DepNamedUnixSocket::ClassInit(Channel::ChannelSocket* chnnelSocket, uv_loop_t* main_loop)
{
    MS_TRACE();

    if (!DepNamedUnixSocket::IsEnabled()) {
        return;
    }
    
    if (DepNamedUnixSocket::inbuf.buf) {
        MS_ABORT("DepNamedUnixSocket::ClassInit called twice");
    }

    // initializing input buffer
    DepNamedUnixSocket::inbuf.buf = new uint8_t[DepNamedUnixSocket::MessageMaxLen];
    if (!DepNamedUnixSocket::inbuf.buf) {
        MS_ERROR_STD("out of memory");
        return;
    }

    DepNamedUnixSocket::inbuf.end  = DepNamedUnixSocket::inbuf.buf + DepNamedUnixSocket::MessageMaxLen;
    DepNamedUnixSocket::inbuf.last = DepNamedUnixSocket::inbuf.buf;

    // create Unix domain name socket. the name of the
    // socket must include the worker's PID. This is
    // done in order to allow the client to route requests
    // to the right worker without having to maintain an
    // internal state

    // Check to see if the user set the directory in the environment variables.
    DepNamedUnixSocket::socketDir = "" ;
    if(!std::getenv("MEDIASOUP_SOCKET_DIR"))
    {
        // default /tmp dir
        DepNamedUnixSocket::socketDir = "/tmp";
    }
    else
    {
        DepNamedUnixSocket::socketDir = std::getenv("MEDIASOUP_SOCKET_DIR");
    }

    // Check to see if the directory already
    // exists and if not try to create it
    DIR* d = opendir(DepNamedUnixSocket::socketDir.c_str());
    if (d) {
        closedir(d);
    } else if (ENOENT == errno) {
        if (mkdir(DepNamedUnixSocket::socketDir.c_str(), 0777) < 0) {
            MS_ABORT("fail to create domain name sockets directory %s. %s",
                    DepNamedUnixSocket::socketDir.c_str(), strerror(errno));
        }
    } else {
        MS_ABORT("fail to open domain name sockets directory. %s", DepNamedUnixSocket::socketDir.c_str());
    }

    // Create the socket name.
    // IMPORTANT NOTE: the name format must be aligned with
    // the expected name format by the proxy that communicate
    // with the worker over the domain name socket
    pid_t pid = getpid();
    DepNamedUnixSocket::socketName = "";

    if(!std::getenv("MEDIASOUP_SOCKET_NAME_PREFIX"))
    {
        // default
        DepNamedUnixSocket::socketName.append("mediasoup_unix_socket_");
    }
    else
    {
        DepNamedUnixSocket::socketName.append(std::getenv("MEDIASOUP_SOCKET_NAME_PREFIX"));
    }
    DepNamedUnixSocket::socketName.append(std::to_string(pid));

    // create the listening socket
    std::string socketFullName = DepNamedUnixSocket::socketDir + "/" + DepNamedUnixSocket::socketName;
    DepNamedUnixSocket::uvListenerHandler = new uv_pipe_t;
    if (!DepNamedUnixSocket::uvListenerHandler)
    {
        MS_ABORT("out of memory");
    }

    uv_pipe_init(main_loop, DepNamedUnixSocket::uvListenerHandler, 0);

    // unlink any existing file with that path before binding
    unlink(socketFullName.c_str());

    int err = uv_pipe_bind(DepNamedUnixSocket::uvListenerHandler,
            socketFullName.c_str());

    if (err != 0)
    {
        MS_ABORT("fail to bind Unix socket %s. %s",
                socketFullName.c_str(), uv_strerror(err));
    }

    err = uv_listen(reinterpret_cast<uv_stream_t*>(DepNamedUnixSocket::uvListenerHandler),
            1, DepNamedUnixSocket::OnClientConnect);

    if (err != 0)
    {
        MS_ABORT("fail to listen on Unix socket %s. %s",
                socketFullName.c_str(), uv_strerror(err));
    }

    DepNamedUnixSocket::chnnelSocket = chnnelSocket;

    MS_DUMP("Successfully setup Unix domain name socket %s", socketFullName.c_str());
}


void DepNamedUnixSocket::ClassDestroy()
{
    if (DepNamedUnixSocket::uvClient) {
        uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvClient),
                        static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));
        DepNamedUnixSocket::uvClient = nullptr;
    }

    if (DepNamedUnixSocket::uvListenerHandler) {
        uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvListenerHandler),
                        static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));
        DepNamedUnixSocket::uvListenerHandler = nullptr;
    }

    MS_DUMP("DepNamedUnixSocket::ClassDestroy");
}

void DepNamedUnixSocket::ClassReinitializeSocket(uv_fs_event_t* handle, const char* filename, int events, int status)
{
    MS_TRACE_STD();
    if (status < 0) 
    {
        // If there is an error in the socket file event, then make sure the file exists, if not reinitialize the socket.
	    MS_DEBUG_DEV("Error in socket file event. Reinitializing the socket if needed.");
    }
    
    // Check to see if this socket file exists. If it does, then we don't need to do anything.
    struct stat buffer;
    std::string socketFullName = DepNamedUnixSocket::socketDir + "/" + DepNamedUnixSocket::socketName;
    int file_exists = stat(socketFullName.c_str(), &buffer);

    if (file_exists != 0) 
    {
        // Re-initialize the socket.     
        if (DepNamedUnixSocket::uvListenerHandler) {
            uv_close(reinterpret_cast<uv_handle_t*>(DepNamedUnixSocket::uvListenerHandler),
                static_cast<uv_close_cb>(DepNamedUnixSocket::OnClose));
            DepNamedUnixSocket::uvListenerHandler = nullptr;
        }
        // Clear the input buffer.
        if (DepNamedUnixSocket::inbuf.buf) {
            delete[] DepNamedUnixSocket::inbuf.buf;
            DepNamedUnixSocket::inbuf.buf = nullptr;
        }

        Channel::ChannelSocket* channel = static_cast<Channel::ChannelSocket*>(handle->data);
        DepNamedUnixSocket::ClassInit(channel, DepLibUV::GetLoop());
    }
}

ChannelReadFreeFn DepNamedUnixSocket::channelReadFn(
        uint8_t** msg,
        uint32_t* msgLen,
        size_t* ctx,
        const void* handle,
        ChannelReadCtx rdCtx)
{
    return nullptr;
}

static void writeToLog(const uint8_t* msg, uint32_t msgLen)
{
    static int    logFD = -1;
    static long   logMaxSize;
    static char   logPath[1024+sizeof("/ms-ingress-1234567890.log")];
    static int    logCnt;
    char          tempLogPath[sizeof(logPath)+sizeof(".1678727830000000")];
    char          *logPathEnv, *logMaxSizeEnv;
    const char    *workerSet;
    struct stat   st;
    int           n;


    if (logFD < -1) return;


    if (!logPath[0]) {
        logPathEnv = std::getenv("MEDIASOUP_WORKER_LOG_DIR");
        if (!logPathEnv) {
            goto fail;
        } else {
            workerSet = std::getenv("MEDIASOUP_WORKER_SET");
            if (!workerSet) {
                workerSet = "default";
            }

            n = snprintf(logPath, sizeof(logPath), "%s/ms-%s-%d.log",
                    logPathEnv, workerSet, getpid());

            if (n < 0 || n + 1 >= (int)sizeof(logPath)) {
                MS_ERROR_STD("failed to open log file path %s too long. workerSet=%s",
                        logPathEnv, workerSet);
                goto fail;
            }
        }
    }

    if (logMaxSize == 0) {
        logMaxSizeEnv = std::getenv("MEDIASOUP_WORKER_LOG_MAX_SIZE");
        if (logMaxSizeEnv) {
            errno = 0;
            logMaxSize = strtol(logMaxSizeEnv, NULL, 10);
            if (errno != 0) {
                MS_ERROR_STD("failed to convert log file max size %s. %s",
                        logMaxSizeEnv, strerror(errno));
                logMaxSize = 100*1024*1024;
            }
        } else {
            logMaxSize = 100*1024*1024;
        }
    }

    if (logFD == -1) {
        logFD = open(logPath, O_TRUNC|O_CREAT|O_WRONLY);
        if (logFD < 0) {
            MS_ERROR_STD("failed to open log file %s. %s", logPath, strerror(errno));
            goto fail;
        }
    } else if (logFD > 0 && (++logCnt % 100) == 0) {
        // check the current file size and rotate if needed
        if (fstat(logFD, &st) < 0) {
            MS_ERROR_STD("failed to stat file. %s", strerror(errno));
            fsync(logFD);
            close(logFD);
            goto fail;
        } else if (st.st_size > logMaxSize) {
            fsync(logFD);
            close(logFD);
            snprintf(tempLogPath, sizeof(tempLogPath), "%s.%ld", logPath, st.st_mtime);
            if (rename(logPath, tempLogPath) < 0) {
                MS_ERROR_STD("failed to rename log file. old=%s new=%s. %s",
                        logPath, tempLogPath, strerror(errno));
            }

            logFD = open(logPath, O_TRUNC|O_CREAT|O_WRONLY);
            if (logFD < 0) {
                MS_ERROR_STD("failed to open log file %s. %s", logPath, strerror(errno));
                goto fail;
            }
        }
    }

    if (write(logFD, msg, msgLen) < 0) {
        MS_ERROR_STD("failed to write to log %s. %s", logPath, strerror(errno));
    }
    if (write(logFD, "\n", 1) < 0) {
        MS_ERROR_STD("failed to write to log %s. %s", logPath, strerror(errno));
    }

    return;

fail:
    logFD = -2;
}

// write the payload to the underlying socket. the payload
// is assumed to be unpacked i.e. doesn't have the internal
// protocol prefix word
void DepNamedUnixSocket::channelWriteFn(
        const uint8_t* msg,
        uint32_t msgLen,
        ChannelWriteCtx wrCtx)
{
    MS_TRACE_STD();
    if (msgLen == 0)
        return;

    if (*msg != '{') {
        writeToLog(msg, msgLen);
        return;
    }

    if (msgLen + MsgLenFieldSize > DepNamedUnixSocket::MessageMaxLen) {
        MS_ERROR("msg too long. %" PRIu32, msgLen);
        return;
    }

    if (!DepNamedUnixSocket::uvClient) {
        std::string errMsg;
        errMsg.append(reinterpret_cast<const char*>(msg), (size_t)msgLen);
        MS_ERROR("fail to write. client socket is closed. msg: %s",
                errMsg.c_str());
        return;
    }

    // we are forced to allocate here since the ChannelSocket is not
    // adding the MsgLenFieldSize bytes prefix that signal the length
    // TODO: add reusable buffers

    uv_write_t* req = new uv_write_t;
    char* pendingData = new char[msgLen + MsgLenFieldSize];
    if (!req || !pendingData) {
        MS_ABORT("out of memory");
    }
    *(reinterpret_cast<uint64_t*>(pendingData)) = 0xFFFFFFFF00000000 + msgLen;
    req->data = pendingData;

    std::memcpy(pendingData + MsgLenFieldSize, msg, msgLen);

    uv_buf_t buffer = uv_buf_init(pendingData, msgLen + MsgLenFieldSize);

    int err = uv_write(
      req,
      reinterpret_cast<uv_stream_t*>(DepNamedUnixSocket::uvClient),
      &buffer,
      1,
      static_cast<uv_write_cb>(DepNamedUnixSocket::OnWrite));

    if (err != 0)
    {
        MS_ERROR("uv_write() failed: %s", uv_strerror(err));

        // Delete the UvSendData struct.
        delete pendingData;
        delete req;
    }
}

// write the payload to the underlying socket. the payload is
// assumed to be packed i.e. it should include the internal
// protocol prefix word
void DepNamedUnixSocket::channelWritePackedMsgFn(
        const uint8_t* msg,
        uint32_t msgLen,
        ChannelWriteCtx wrCtx)
{
    MS_TRACE_STD();

    if (msgLen == 0)
        return;

    if (msgLen > DepNamedUnixSocket::MessageMaxLen) {
        MS_ERROR("msg too long. %" PRIu32, msgLen);
        return;
    }

    if (!DepNamedUnixSocket::uvClient) {
        MS_ERROR("fail to write. client socket is closed");
        return;
    }

    // First try uv_try_write(). In case it can not directly send all the given data
    // then build a uv_req_t and use uv_write().

    uv_buf_t buffer = uv_buf_init(reinterpret_cast<char*>(const_cast<uint8_t*>(msg)), msgLen);
    int written     = uv_try_write(reinterpret_cast<uv_stream_t*>(DepNamedUnixSocket::uvClient), &buffer, 1);

    // All the data was written. Done.
    if (written == static_cast<int>(msgLen))
    {
        return;
    }
    // Cannot write any data at first time. Use uv_write().
    else if (written == UV_EAGAIN || written == UV_ENOSYS)
    {
        // Set written to 0 so pendingLen can be properly calculated.
        written = 0;
    }
    // Any other error.
    else if (written < 0)
    {
        MS_ERROR_STD("uv_try_write() failed, trying uv_write(): %s", uv_strerror(written));

        // Set written to 0 so pendingLen can be properly calculated.
        written = 0;
    }

    size_t pendingLen = msgLen - written;
    uv_write_t* req = new uv_write_t;
    char* pendingData = new char[pendingLen];
    if (!req || !pendingData) {
        MS_ABORT("out of memory");
    }
    req->data = pendingData;

    std::memcpy(pendingData, msg + written, pendingLen);

    buffer = uv_buf_init(pendingData, pendingLen);

    int err = uv_write(
      req,
      reinterpret_cast<uv_stream_t*>(DepNamedUnixSocket::uvClient),
      &buffer,
      1,
      static_cast<uv_write_cb>(DepNamedUnixSocket::OnWrite));

    if (err != 0)
    {
        MS_ERROR_STD("uv_write() failed: %s", uv_strerror(err));

        // Delete the UvSendData struct.
        delete pendingData;
        delete req;
    }
}

PayloadChannelReadFreeFn DepNamedUnixSocket::payloadChannelReadFn(
        uint8_t** msg,
        uint32_t* msgLen,
        size_t* ctx,
        uint8_t** payload,
        uint32_t* payloadLen,
        size_t* payloadCapacity,
        const void* handle,
        PayloadChannelReadCtx rdCtx)
{
    return nullptr;
}
void DepNamedUnixSocket::payloadChannelWriteFn(
        const uint8_t* msg,
        uint32_t msgLen,
        const uint8_t* payload,
        uint32_t payloadLen,
        ChannelWriteCtx wrCtx)
{

}


/*
 * // for mediasoup-shm we create the router as soon as
// the worker is created and before any client connection
// is made available. This function parses the response
// and in case it is accept for request id 0 it assume that
// this is the create router response and simply ignores
// the response without logging any error
void DepNamedUnixSocket::handleWriteNoUvClientError(const uint8_t* msg, uint32_t msgLen)
{
#if !MEDIASOUP_SHM_ENABLED
    MS_ERROR_STD("DepNamedUnixSocket::channelWriteFn. fail to write. client socket is closed");
    return;
#endif

    json jsonResp;

    try
    {
        jsonResp = json::parse(
                reinterpret_cast<char*>(msg),
                reinterpret_cast<char*>(msg) + msgLen);
    }
    catch (const json::parse_error& error)
    {
        MS_ERROR_STD("DepNamedUnixSocket::channelWriteFn. fail to write. client socket is closed");
        MS_ERROR_STD("then failed to parse channel response. err=%s", error.what());
        return;
    }

    // expected input:
    // {
    //   "id": 0,
    //   "accepted": true,
    // }
    auto jsonIdIt = jsonResp.find("id");

    if (jsonIdIt != jsonResp.end() && jsonIdIt->is_number_unsigned())
    {
        uint32_t id = jsonIdIt->get<uint32_t>();

        if (id == 0)
        {
            auto jsonAcceptedIt = jsonResp.find("accepted");
            if (jsonAcceptedIt == jsonResp.end() || !jsonAcceptedIt->is_boolean())
            {
                MS_ABORT("failed to create default router (missing accepted)");
            }

            if (!jsonAcceptedIt->get<bool>())
            {
                MS_ABORT("failed to create default router (accepted false)");
            }

            MS_DEBUG_TAG(shm, "successfully created default router");
            return;
        }
    }
    MS_ERROR_STD("DepNamedUnixSocket::channelWriteFn. fail to write. client socket is closed");
}
*/

