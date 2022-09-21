ChannelReadFreeFn channelReadFn (uint8_t** message,
		uint32_t* messageLen,
		size_t* messageCtx,
		const void* handle,
		ChannelReadCtx ctx);

void after_write(uv_write_t *req, int status);

void channelWriteFn (const uint8_t*  message,
		uint32_t  messageLen,
		ChannelWriteCtx  ctx );

