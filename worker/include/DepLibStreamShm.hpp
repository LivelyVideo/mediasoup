#ifndef DEP_LIBSTREAMSHM_HPP
#define DEP_LIBSTREAMSHM_HPP

#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

extern "C"
{
#include "shm_common_config.h"
#include "shm_common.h"
#include "shm_rtp.h"
#include "shm_cyclic_buf.h"
#include "shm_mpegts_mux.h"
}

#include "MediaSoupErrors.hpp"
#include "RTC/RtpDictionaries.hpp"


namespace DepLibStreamShm
{

typedef shm_rtp_stream_write_info     WriteInfo;
typedef shm_rtp_stream_chn_init_t     ChnInit;
typedef shm_rtp_stream_header_ext_t   HeaderExt;

// this object includes configuration used by DepLibStreamShm
// for for writers and readers. we don't use the conventional
// MediaSoup settings in order to avoid complicate merge in
// case of an update
class ShmCtxConf
{
private:
	char        path[1024];    // path to configuration file
	struct stat confFileStat;  // stat struct of the configuration file
public:
	ChnInit  chnInit[SHM_STREAM_MAX_CHANNELS]; // channels configuration
	uint64_t idleReadTimeoutMs;                // read timeout in milliseconds
	uint64_t nackEpochMs;                      // missing packets older than cur time - epoch will not be requested

public:
	ShmCtxConf();
	void SetDefaults();
	void Load();
};

class ShmCtx
{
	private:
		shm_stream_ctx_t              shm_ctx;          // shared memory context
		shm_rtp_stream_wr_ctx_t       ctx;              // context that is used by RTP write function to track packet loss
		shm_rtp_stream_rd_ctx_t       rd_ctx;           // unordered read context from one channel
		shm_seq                       chn_app_data_seq; // the sequence number of the channel app data. it is used for detecting channel reset
		shm_rtp_stream_chn_app_data_t chn_app_data;     // the source channel rtp channel meta-data
		RTC::RtpCodecMimeType         chnMimeType;      // the translation of the shm codec to MediaSoup mime type
		uint64_t                      last_hb;          // the last writer's heartbear timestamp


		static ShmCtxConf          conf;       // shared memory configuration loaded from a file

		shm_stream_ctx_t* GetShmCtx() {return &this->shm_ctx;}
	public:
		enum ReturnCodes {
			RC_OK              = SHM_OK,
			RC_ERR             = SHM_ERR,
			RC_RESET           = SHM_RESET,
			RC_AGAIN           = SHM_AGAIN,
			RC_DECLINED        = SHM_DECLINED,
		};

		// corresponds to the return codes from shm_rtp_stream_write_packet
		enum WriteReturnCodes
		{
			RESET           = 0x00,
			NEW             = 0x01,
			OLD             = 0x02,
			RETRANSMIT      = 0x04
		};

	public:
		ShmCtx();
		~ShmCtx();


		int IsOpen() { return (shm_stream_is_reader_ready(GetShmCtx()) || shm_stream_is_writer_ready(GetShmCtx())); }
		int IsClosing() { return shm_stream_is_closing(GetShmCtx()); }
		int IsProducerPaused() { return shm_stream_is_channel_paused(GetShmCtx(), this->rd_ctx.chn); }
		int LoadChannelAppData();
		const RTC::RtpCodecMimeType& GetMimeType() {return this->chnMimeType;}
		int OpenReader(std::string const& name, std::string const& channelId);
		int OpenWriter(std::string const& name);
		int Write(int channel, const uint8_t *buf, size_t size, HeaderExt *headerExt, WriteInfo *out);
		int SetChannelOpaqueData(int channel, const uint8_t *buf, size_t size);
		int SenderReport(int channel, uint32_t lastSrNtpSec, uint32_t lastSrNtpFrac, uint32_t lastSrRtpTm);
		int ReportGaps(int channel, uint64_t rtt, uint32_t range, uint32_t *pidBlp, size_t *len, size_t *pktCnt);
		int ReadNext(uint8_t **buf, size_t size);
		void SkipHead();
		int ReadBySeqId(uint16_t seq, uint8_t **buf, size_t size);
		int HasChannelBeenReset();
		int FindChannelBySsrc(uint32_t ssrc);
		int FindChannelById(std::string const& id);
		int AddNewChannel(uint32_t ssrc, const RTC::RtpCodecParameters& mediaCodec, uint8_t mappedPt, uint32_t kbps, const std::string &rid);
		int MapSsrcToChannel(uint32_t ssrc, int channel);
		int ChannelConsumerHeartbeat(int channel);
		int RequestKeyframe(int channel);
		uint64_t CheckKeyframeRequest(int channel);
		static uint64_t GetIdleReadTimeoutMs() {return conf.idleReadTimeoutMs;}
		static uint64_t GetNackEpochMs() {return conf.nackEpochMs;}
		void Close();
		void CloseReader();
		void ResetChannel(int channel);
		static void UpdateTime() {shm_update_time();}
		static uint64_t GetCurrentTime() {return shm_get_current_time();}
		static char* GetCurrentTimeStr() {return shm_get_current_time_str();}
		const char* GetStreamName() {return shm_get_stream_name(&this->shm_ctx);}
	public:
		static void ParseStreamKey(std::string const& key, std::string &streamKey, std::string &channelId);
};
}

#endif // DEP_LIBSTREAMSHM_HPP
