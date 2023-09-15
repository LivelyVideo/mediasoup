#define MS_CLASS "DepLibStreamShm"
#define MS_LOG_DEV

#include <time.h>
#include <chrono>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "DepLibStreamShm.hpp"
#include "Logger.hpp"

using json = nlohmann::json;

extern "C" {
static void log_fnc(
		int verbose,
		const char *strid,
		int level,
		const char *msg,
		const char *info,
		const char *func_name,
		int line, ...) {

	char buf[2048];

	// follow SHM_LOG_LEVEL_XXX in shm_common_config.h
	static char const *const log_levels[] = { "stderr", "emerg", "alert",
			"crit", "err", "warn", "notice", "info", "debug" };

	va_list args;

    switch (Settings::configuration.logLevel) {
    case LogLevel::LOG_NONE:
        return;
    case LogLevel::LOG_ERROR:
        if (level > SHM_LOG_LEVEL_ERR) return;
        break;
    case LogLevel::LOG_WARN:
        if (level > SHM_LOG_LEVEL_INFO) return;
        break;
    case LogLevel::LOG_DEBUG:
        break;
    }

	va_start(args, line);
	vsnprintf(buf, sizeof(buf), info, args);
	va_end(args);

    int loggerWritten = std::snprintf(
            Logger::buffer,
            Logger::bufferSize,
            "D - %s - %s:%d - [%s] - %s - %s %s",
            Logger::GetCurrentTimeStr(),
            func_name, line, log_levels[level], strid, msg, buf);
    Logger::channel->SendLog(Logger::buffer, static_cast<uint32_t>(loggerWritten));
}

shm_log_func_pt const shm_common_log = log_fnc;
}

namespace DepLibStreamShm {

// loads the configuration. The constructor first tries
// to load a configuration file pointed to be environment
// variable. In case it fails it will use default values
ShmCtxConf::ShmCtxConf()
{
	char *confFileEnv;

	idleReadTimeoutMs = 0;   // no timeout
	nackEpochMs       = 200; // default 200 milliseconds

	path[0] = 0;
	memset(chnInit, 0, sizeof(chnInit));
	memset(&confFileStat, 0, sizeof(confFileStat));

	confFileEnv = getenv("MS_SHM_STREAM_CONFIG_FILE");
	if (!confFileEnv) {
		printf("MS_SHM_STREAM_CONFIG_FILE not set\n");
	} else {
		if (strlen(confFileEnv) >= sizeof(path)) {
			printf("value of env MS_SHM_STREAM_CONFIG_FILE too long. value=%s\n", confFileEnv);
		} else {
			strcpy(path, confFileEnv);
		}
	}
}

void ShmCtxConf::SetDefaults()
{
	// configuration is already set
	if (chnInit[0].buf_len_ms && chnInit[0].kbps) {
		return;
	}

	chnInit[0].buf_len_ms = 2000; // 2 seconds
	chnInit[0].kbps       = 5000; // video

	// for audio we set the buffer duration longer
	// since the algorithm calculate the index length
	// assuming each audio frame fills an MTU i.e.
	// index length = kpbs / 8 * buf length / MTU
	// if buf len = 2000 we get 128/8*2000/1200 = 16
	chnInit[1].buf_len_ms = 40000; // 4 seconds
	chnInit[1].kbps       = 128;  // audio
}

// loads the configuration file (if one specified)
void ShmCtxConf::Load()
{
	struct stat fileStat;
	int         rc, fd;
	char        *buf;

	buf = nullptr;
	fd = -1;

	// in case configuration file wasn't set then set defaults
	if (!path[0]) {
	    MS_DEBUG_TAG(shm, "config file pointed by env MS_SHM_STREAM_CONFIG_FILE was not loaded");
		SetDefaults();
		return;
	}

	rc = stat(path, &fileStat);
	if (rc < 0) {
		MS_ERROR("fail to stat shm config file. file=%s err=%s", path, strerror(errno));
		goto fail;
	}

	// file has some data and its modification time is different
	// than last read time then re-load the file content
	if (fileStat.st_size && fileStat.st_mtime != confFileStat.st_mtime) {
		buf = (char*)malloc(fileStat.st_size);
		if (!buf) {
			MS_ERROR("fail to read shm config file (out of memory). file=%s err=%s",
					path, strerror(errno));
			goto fail;
		}

		fd = open(path, O_RDONLY);
		if (fd < 0) {
			MS_ERROR("fail to open shm config file. file=%s err=%s", path, strerror(errno));
			goto fail;
		}

		rc = read(fd, buf, fileStat.st_size);
		close(fd);
		fd = -1;

		// make sure we don't try to read it over and over again
		confFileStat = fileStat;

		if (rc < 0) {
			MS_ERROR("fail to read shm config file. file=%s err=%s", path, strerror(errno));
			goto fail;
		}

		if ((off_t)rc != fileStat.st_size) {
			MS_ERROR("fail to read shm config file. file=%s sz=%jd rc=%d",
					path, (intmax_t)fileStat.st_size, rc);
			goto fail;
		}

		json jsonConf;

		try
		{
			jsonConf = json::parse(buf, buf + fileStat.st_size);
		}
		catch (const json::parse_error& error)
		{
			MS_ERROR_STD("failed to parse shm config. file=%s err=%s", path, error.what());
			goto fail;
		}

		// expected input:
		// {
		//   "channels": [
		//     {"kbps": <kbps>, "bufLenMs": <buffer length in milliseconds>},
		//     ...
		//   ],
		//   "idleReadTimeoutMs": <timeout in milliseconds, zero (default) means no timeout>
		//   "nackEpochMs": <max interval in milliseconds for requesting missing packets>
		// }
		auto jsonIdleReadTimeoutMsIt = jsonConf.find("idleReadTimeoutMs");
        if (jsonIdleReadTimeoutMsIt != jsonConf.end()) {
            if (!(*jsonIdleReadTimeoutMsIt).is_number_unsigned()) {
                MS_ERROR("failed to process shm config. "
                        "'idleReadTimeoutMs' must be unsigned integers. file=%s", path);
                goto fail;
            }
            idleReadTimeoutMs = jsonIdleReadTimeoutMsIt->get<uint64_t>();
        }

        auto jsonNackEpochMsIt = jsonConf.find("nackEpochMs");
        if (jsonNackEpochMsIt != jsonConf.end()) {
            if (!(*jsonNackEpochMsIt).is_number_unsigned()) {
                MS_ERROR("failed to process shm config. "
                        "'nackEpochMs' must be unsigned integers. file=%s", path);
                goto fail;
            }
            nackEpochMs = jsonNackEpochMsIt->get<uint64_t>();
        }

		auto jsonChnIt = jsonConf.find("channels");

		if (jsonChnIt == jsonConf.end() || !jsonChnIt->is_array()) {
			MS_ERROR("failed to process shm config. missing channels attribute. file=%s", path);
			goto fail;
		}

		int i = 0;
		for (const auto& chn : *jsonChnIt)
		{
			if (i == SHM_STREAM_MAX_CHANNELS) {
				MS_WARN_TAG(shm, "config contain too many channels. file=%s max=%d",
						path, SHM_STREAM_MAX_CHANNELS);
				break;
			}

			if (!chn.is_object()) {
				MS_ERROR("failed to process shm config. "
						"channels array contain non-object element. file=%s", path);
				goto fail;
			}

			auto jsonKbpsIt = chn.find("kbps");
			auto jsonLenIt  = chn.find("bufLenMs");

			if (jsonKbpsIt == chn.end() || jsonLenIt == chn.end()) {
				MS_ERROR("failed to process shm config. "
						"channel conf expected to have 'kbps' and 'bufLenMs' properties. file=%s", path);
				goto fail;
			}

			if (!(*jsonKbpsIt).is_number_unsigned() || !(*jsonLenIt).is_number_unsigned()) {
				MS_ERROR("failed to process shm config. "
						"'kbps' and 'bufLenMs' should be unsigned integers. file=%s", path);
				goto fail;
			}

			chnInit[i].kbps = jsonKbpsIt->get<uint32_t>();
			chnInit[i].buf_len_ms = jsonLenIt->get<uint32_t>();

			MS_DEBUG_TAG(shm, "successfully loaded channel config. "
					"i=%d kbps=%" PRIu32 " bufLen=%" PRIu32 "\n",i, chnInit[i].kbps, chnInit[i].buf_len_ms);

			i++;
		}

		free(buf);
		buf = nullptr;
	}

	return;

fail:
	if (fd >= 0) close(fd);
	if (buf) free(buf);
	SetDefaults();
}


// configuration is declared as static
// variable in order to set it once
ShmCtxConf ShmCtx::conf;


ShmCtx::ShmCtx()
{
    std::memset(&this->shm_ctx, 0, sizeof(this->shm_ctx));
	std::memset(&this->ctx, 0, sizeof(this->ctx));
	std::memset(&this->rd_ctx, 0, sizeof(this->rd_ctx));
    std::memset(&this->chn_app_data, 0, sizeof(this->chn_app_data));

	this->chn_app_data_seq = SHM_UNINIT_SEQ;
	this->last_hb = 0;

	this->chnMimeType.type = RTC::RtpCodecMimeType::Type::UNSET;
	this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::UNSET;
}


ShmCtx::~ShmCtx()
{
	MS_TRACE();
}

int ShmCtx::LoadChannelAppData()
{
    int      rc;
    shm_seq  seq;

    if (this->chn_app_data_seq != SHM_UNINIT_SEQ) {
        return SHM_OK;
    }

    rc = shm_rtp_stream_read_channel_app_data(
            GetShmCtx(), this->rd_ctx.chn, &this->chn_app_data, 0, &seq);

    if (rc < 0) {
        return rc;
    }

    switch (this->chn_app_data.codec) {
    case SHM_STREAM_CODEC_OPUS:
        this->chnMimeType.type = RTC::RtpCodecMimeType::Type::AUDIO;
        this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::OPUS;
        this->chnMimeType.UpdateMimeType();
        break;

    case SHM_STREAM_CODEC_H264:
        this->chnMimeType.type = RTC::RtpCodecMimeType::Type::VIDEO;
        this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::H264;
        this->chnMimeType.UpdateMimeType();
        break;

    case SHM_STREAM_CODEC_VP8:
        this->chnMimeType.type = RTC::RtpCodecMimeType::Type::VIDEO;
        this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::VP8;
        this->chnMimeType.UpdateMimeType();
        break;

    case SHM_STREAM_CODEC_VP9:
        this->chnMimeType.type = RTC::RtpCodecMimeType::Type::VIDEO;
        this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::VP9;
        this->chnMimeType.UpdateMimeType();
        break;

    case SHM_STREAM_CODEC_H265:
        this->chnMimeType.type = RTC::RtpCodecMimeType::Type::VIDEO;
        this->chnMimeType.subtype = RTC::RtpCodecMimeType::Subtype::H265;
        this->chnMimeType.UpdateMimeType();
        break;

    default:
        MS_DEBUG_TAG(shm, "unsupported codec in app data. stream: %s chn: %d codec%" PRIu16 " seq=%" PRIu64,
                this->GetStreamName(), this->rd_ctx.chn, this->chn_app_data.codec, seq);
        return SHM_ERR;
    }

    MS_DEBUG_TAG(shm, "channel app data updated. stream: %s chn: %d mime-type: %s prev_sq=%" PRIu64 " sq=%" PRIu64,
            this->GetStreamName(), this->rd_ctx.chn, this->chnMimeType.ToString().c_str(),
            this->chn_app_data_seq, seq);


    this->chn_app_data_seq = seq;

    return SHM_OK;
}

int ShmCtx::OpenReader(std::string const& streamKey, std::string const& channelId)
{
	int        chn;

	chn = shm_stream_open_reader(GetShmCtx(), streamKey.c_str(), SHM_STREAM_PT_SRC_RTP);

	if (chn < 0) {
	    // TODO: temporary hack to try both source RTP
	    // and encoded RTP. The stream type indicator
	    // should be passed from the client as enum
	    chn = shm_stream_open_reader(GetShmCtx(), streamKey.c_str(), SHM_STREAM_PT_ENC_PRIMARY_RTP);

	    if (chn < 0) return chn;
	}

	// find the requested channel
	if (channelId.empty()) {
		chn = shm_rtp_stream_find_first_channel(GetShmCtx(), 0);
	} else {
		chn = shm_rtp_stream_find_channel_by_id(GetShmCtx(), channelId.c_str());
	}

	if (chn < 0) {
		shm_stream_reader_close(GetShmCtx());
		return chn;
	}

	shm_rtp_stream_init_unordered_rd_ctx(GetShmCtx(), &this->rd_ctx, chn);

	///////////////////////////
	// TODO: !!! DELETE ME !!!
	//shm_stream_set_verbose_signal(GetShmCtx(), 1);
    ///////////////////////////

	return chn;
}

int ShmCtx::OpenWriter(std::string const& name)
{
	shm_rtp_stream_init_t init;
	int                   rc, i;

	shm_update_time();

	if (name.length() > sizeof(init.name)) {
		MS_WARN_TAG(shm, "fail to open shm. name too long. %zu", name.length());
		return SHM_ERR;
	}

	memset(&init, 0, sizeof(init));
	strcpy(init.name, name.c_str());

	init.payload_type = SHM_STREAM_PT_SRC_RTP;

	// load the shared memory configuration from file
	// TODO: check if there is a better way to do it which
	// is more aligned with MediaSoup way of doing things
	conf.Load();

	for (i = 0; i < SHM_STREAM_MAX_CHANNELS; i++) {
		// end of configured channels
		if (!conf.chnInit[i].buf_len_ms || !conf.chnInit[i].kbps) {
			break;
		}

		init.chn[i] = conf.chnInit[i];
	}

	memset(&this->ctx, 0, sizeof(this->ctx));

	rc = shm_rtp_stream_init_writer(GetShmCtx(), &this->ctx, &init);

	if (rc < 0) {
		// the shm might be already open by another producer
		// try to get a reference from the shm registry
		return shm_stream_open_reader(GetShmCtx(), init.name, SHM_STREAM_PT_SRC_RTP);
	}

	////////////////////////////////
	// TODO: !!! DELETEME !!!
	//this->ctx.shm_ctx.verbose = 1;
	////////////////////////////////

	return rc;
}

int ShmCtx::FindChannelBySsrc(uint32_t ssrc)
{
	return shm_rtp_stream_find_channel_by_ssrc(GetShmCtx(), ssrc, 1);
}

int ShmCtx::FindChannelById(std::string const& id)
{
	return shm_rtp_stream_find_channel_by_id(GetShmCtx(), id.c_str());
}

int ShmCtx::AddNewChannel(uint32_t ssrc, const RTC::RtpCodecParameters& mediaCodec, uint8_t mappedPt, uint32_t kbps, const std::string &rid)
{
	shm_rtp_stream_chn_conf_t conf;

	switch (mediaCodec.mimeType.subtype)
	{
	case RTC::RtpCodecMimeType::Subtype::OPUS:
	    conf.codec = SHM_STREAM_CODEC_OPUS;
		break;
	case RTC::RtpCodecMimeType::Subtype::H264:
	    conf.codec = SHM_STREAM_CODEC_H264;
		break;
	case RTC::RtpCodecMimeType::Subtype::VP8:
	    conf.codec = SHM_STREAM_CODEC_VP8;
		break;
	case RTC::RtpCodecMimeType::Subtype::VP9:
	    conf.codec = SHM_STREAM_CODEC_VP9;
		break;
	default:
		MS_WARN_TAG(shm, "unsupported codec. chn=%s", mediaCodec.mimeType.ToString().c_str());
		return SHM_ERR;
	}

	conf.rtp_pt          = mediaCodec.payloadType;
	conf.clock_rate_tens = mediaCodec.clockRate / 10;
	conf.ssrc            = ssrc;

	return shm_rtp_stream_add_new_channel(
	        GetShmCtx(),
			&conf,
			kbps,
			rid.c_str());
}

int ShmCtx::MapSsrcToChannel(uint32_t ssrc, int channel)
{
	return shm_rtp_stream_add_ssrc(GetShmCtx(), ssrc, (uint32_t)channel);
}

int ShmCtx::ReadNext(uint8_t **buf, size_t size)
{
	return shm_rtp_stream_read_unordered_packet(GetShmCtx(), &this->rd_ctx, buf, size);
}

// moves the position of the channel reader context
// to the most recently received packet
void ShmCtx::SkipHead()
{
    shm_rtp_stream_read_skip_head(GetShmCtx(), &this->rd_ctx);
}

int ShmCtx::ReadBySeqId(uint16_t seq, uint8_t **buf, size_t size)
{
	return shm_rtp_stream_read_inorder_by_seq(GetShmCtx(), this->rd_ctx.chn, this->rd_ctx.reset_seq, seq, buf, size);
}

inline int ShmCtx::HasChannelBeenReset()
{
    return shm_stream_has_channel_been_reset(GetShmCtx(), this->rd_ctx.reset_seq, this->rd_ctx.chn);
}

int ShmCtx::Write(int channel, const uint8_t *buf, size_t size, HeaderExt *headerExt, WriteInfo *out)
{
	int                         rc;
	char const                  *result = nullptr;

	rc = shm_rtp_stream_write_packet_by_server(GetShmCtx(), &this->ctx, buf, size, (uint32_t)channel, headerExt, out);
	if (rc < 0) {
		MS_DEBUG_TAG(shm, "failed to write packet. chn=%d", channel);
		return rc;
	}

	switch (rc) {
	case SHM_RTP_STREAM_WRITE_RESET:
		result = "reset";
		break;

	case SHM_RTP_STREAM_WRITE_RETRANSMIT:
		result = "retransmit";
		break;

	case SHM_RTP_STREAM_WRITE_OLD:
		result = "old";
		break;

	case SHM_RTP_STREAM_WRITE_NEW:
		if (out->gap_size) {
			result = "gap";
		}
	}

	if (result) {
		MS_DEBUG_TAG(shm, "%s chn %d sq %" PRIu16, result, channel, ntohs(((uint16_t*)buf)[1]));
	}

	// the return code indicates if the packet is new, old  or retransmitted
	return rc;
}


int ShmCtx::SetChannelOpaqueData(int channel, const uint8_t *buf, size_t size)
{
    return shm_stream_update_channel_opaque_data(GetShmCtx(), channel, buf, size);
}

int ShmCtx::SenderReport(int channel, uint32_t lastSrNtpSec, uint32_t lastSrNtpFrac, uint32_t lastSrRtpTm)
{
	int                       rc;
	shm_rtp_stream_clk_ref_t  clk_ref;

	clk_ref.last_sr_tm       = shm_get_current_time();
	clk_ref.last_sr_ntp_sec  = lastSrNtpSec;
	clk_ref.last_sr_ntp_frac = lastSrNtpFrac;
	clk_ref.last_sr_rtp_tm   = lastSrRtpTm;


	rc = shm_rtp_stream_sender_report(GetShmCtx(), channel, &clk_ref);
	if (rc < 0) {
		MS_DEBUG_TAG(shm, "failed to update channel with SR info. chn=%d", channel);
	}

	return rc;
}




int ShmCtx::ReportGaps(int channel, uint64_t rtt, uint32_t range, uint32_t *pidBlp, size_t *len, size_t *pktCnt)
{
    uint64_t epoch = DepLibStreamShm::ShmCtx::GetNackEpochMs();
	return shm_rtp_stream_report_gaps(GetShmCtx(), (uint32_t)channel, rtt, epoch, range, pidBlp, len, pktCnt);
}

// signal the current timestamp in the specified channel
// this signal tells the producer that the channel has at
// least one active consumer
int ShmCtx::ChannelConsumerHeartbeat(int channel)
{
	return shm_rtp_stream_chn_consumer_heartbeat(GetShmCtx(), channel);
}

// sets the PLI signal for the specified channel to signal
// the producer to request a key frame from the source
int ShmCtx::RequestKeyframe(int chn)
{
	return shm_rtp_stream_request_keyframe(GetShmCtx(), chn);
}

uint64_t ShmCtx::CheckKeyframeRequest(int chn)
{
	return shm_rtp_stream_check_and_reset_kf_req(GetShmCtx(), chn);
}




void ShmCtx::Close()
{
	MS_TRACE();

	if (shm_stream_is_writer_ready(GetShmCtx()) && shm_stream_is_writer(GetShmCtx())) {
		MS_DEBUG_TAG(shm, "shm[%s] writer will be closed", shm_get_stream_name(GetShmCtx()));
		shm_rtp_stream_close_writer(GetShmCtx(), 0);
	} else if (shm_stream_is_reader_open(GetShmCtx())) {
		MS_DEBUG_TAG(shm, "shm[%s] reader will be closed", shm_get_stream_name(GetShmCtx()));
		shm_stream_reader_close(GetShmCtx());
	} else {
	    MS_DEBUG_TAG(shm, "shm not closing shm");
	}
}


void ShmCtx::CloseReader()
{
    MS_TRACE();

    if (shm_stream_is_reader_open(GetShmCtx())) {
        MS_DEBUG_TAG(shm, "reader will be closed. stream %s chn %d",
                shm_get_stream_name(GetShmCtx()), rd_ctx.chn);
        shm_stream_reader_close(GetShmCtx());
    }
}

void ShmCtx::ResetChannel(int channel)
{
    shm_rtp_stream_reset_channel(GetShmCtx(), channel);
}


void ShmCtx::ParseStreamKey(
		std::string const& key, std::string &streamKey, std::string &channelId)
{
	// we expect the producer id string to be in the format
	// <stream key>$$<rid>
	std::size_t pos = key.find_first_of("$$");

	if (pos != std::string::npos) {
		streamKey.assign(key.substr(0, pos));

		if (pos + 2 < key.length()) {
			channelId.assign(key.substr(pos + 2, key.length() - pos - 2));
		} else {
			channelId.assign("");
		}
	} else {
		streamKey.assign(key);
		channelId.assign("");
	}
}

}
