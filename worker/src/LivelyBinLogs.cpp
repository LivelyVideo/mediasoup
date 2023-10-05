#define MS_CLASS "Lively::StatsBinLog"

#include "DepLibUV.hpp"
#include "LivelyBinLogs.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include <cstring>
#include <sys/stat.h>

namespace Lively
{

constexpr uint8_t hexVal[256] = {
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 0, 0, 0, 0, 0, 0,  // '0'=0x30 .. '9'
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'A'=0x41 .. 'F'
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 'a'=0x61 .. 'f'
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr uint16_t FILENAME_LEN_MAX = 255;
constexpr uint16_t FILEPATH_LEN_MAX = 4096 / 2; //I doubt we need the full 4096 linux path limit. Save some buff allocations.
constexpr uint64_t DAY_IN_MS = 86400000ULL;

CallStatsRecord::CallStatsRecord(uint64_t type, uint16_t ssrc, uint8_t payload, char content, std::string callId, std::string obj, std::string producer)
  : type(type), call_id(callId), object_id(obj), producer_id(producer)
{
  uint64_t ts = Utils::Time::currentStdEpochMs();
  set_start_tm(ts);

  set_filled(0);

  if (type) // consumer
  {
    record.c.ssrc = ssrc;
    record.c.payload = payload;
    record.c.content = content;
    std::memset(record.c.consumer_id, 0, UUID_BYTE_LEN);
    uuidToBytes(obj, record.c.consumer_id);

    std::memset(record.c.producer_id, 0, UUID_BYTE_LEN);
    uuidToBytes(producer, record.c.producer_id);

    std::memset(record.c.samples, 0, sizeof(record.c.samples));

    MS_DEBUG_TAG(rtp, "CallStatsRecord ctor(): consumer start_tm=%" PRIu64 " ssrc=%" PRIu16 " payload=%" PRIu8 " type=%c callId=%s consumerId=%s producerId=%s",
      ts, ssrc, payload, content, call_id.c_str(), object_id.c_str(), producer_id.c_str());
  }
  else // producer
  {
    record.p.ssrc = ssrc;
    record.p.payload = payload;
    record.p.content = content;
    std::memset(record.p.samples, 0, sizeof(record.p.samples));

    MS_DEBUG_TAG(rtp, "CallStatsRecord ctor(): producer start_tm=%" PRIu64 " ssrc=%" PRIu16 " payload=%" PRIu8 " type=%c callId=%s producerId=%s",
      ts, ssrc, payload, content, call_id.c_str(), object_id.c_str());
  }
}


uint8_t* CallStatsRecord::hexStrToBytes(const char* from, int num, uint8_t* to)
{
  for (int i = 0; i < num; i++)
  {
    *to++ = (hexVal[from[i * 2] & 0xFF] << 4) + (hexVal[from[i * 2 + 1] & 0xFF]);
  }
  return to;
}


bool CallStatsRecord::uuidToBytes(std::string uuid, uint8_t *out)
{
  //00000000-0000-0000-0000-000000000000 : remove '-' and convert digit chars into hex
  char cstr[37];
  //out should be 16 bytes long
  uint8_t *p = out;
  std::memset(cstr, 0, sizeof(cstr));
  std::strcpy(cstr, uuid.c_str());
  if (std::strlen(cstr) != UUID_CHAR_LEN)
    return false;

  char *ch = std::strtok(cstr, "-");
  while (ch)
  {
    p = hexStrToBytes(ch, std::strlen(ch)/2, p);
    ch = std::strtok(NULL,"-");
  }
  return true;
}

// fd is opened file handle; if returned false, a caller should close the file, etc.
bool CallStatsRecord::fwriteRecord(std::FILE* fd)
{
  int rc;
  if (type) // consumer
  {
    rc = std::fwrite(&record.c, sizeof(ConsumerRecord), 1, fd);
  }
  else
  {
    rc = std::fwrite(&record.p, sizeof(ProducerRecord), 1, fd);
  }
  return  (rc > 0);
}

void CallStatsRecord::resetSamples(uint64_t ts)
{
  // Wipe off samples data
  if (type) // consumer
  {
    std::memset(&record.c.samples, 0, sizeof(record.c.samples));
  }
  else
  {
    std::memset(record.p.samples, 0, sizeof(record.p.samples));
  }
  set_filled(0);
  set_start_tm(ts);
}

bool CallStatsRecord::addSample(StreamStats& last, StreamStats& curr)
{
  MS_ASSERT(filled() >= 0 && filled() < maxSamples(),
            "Cannot have %" PRIu32 " >= %" PRIu32 " samples in record, quitting...",
			filled(), maxSamples());

  MS_ASSERT(last.ts != UINT64_UNSET,
            "Timestamp of a previous sample is unset, quitting...");

  MS_ASSERT(curr.ts != UINT64_UNSET,
            "Timestamp of a current sample is unset, quitting...");

  uint32_t idx = filled();
  CallStatsSample* s = type ? &(record.c.samples[0]) : &(record.p.samples[0]);

  s[idx].epoch_len = static_cast<uint16_t>(curr.ts - last.ts);
  s[idx].packets_count = static_cast<uint16_t>(curr.packetsCount - last.packetsCount);
  s[idx].bytes_count = static_cast<uint32_t>(curr.bytesCount - last.bytesCount);
  s[idx].frames_count = static_cast<uint32_t>(curr.framesCount - last.framesCount);
  s[idx].packets_lost = (curr.packetsLost > last.packetsLost) ? static_cast<uint16_t>(curr.packetsLost - last.packetsLost) : 0;
  s[idx].packets_discarded = static_cast<uint16_t>(curr.packetsDiscarded - last.packetsDiscarded);
  s[idx].packets_repaired = static_cast<uint16_t>(curr.packetsRepaired - last.packetsRepaired);
  s[idx].packets_retransmitted = static_cast<uint16_t>(curr.packetsRetransmitted - last.packetsRetransmitted);
  s[idx].nack_count = static_cast<uint16_t>(curr.nackCount - last.nackCount);
  s[idx].nack_pkt_count = static_cast<uint16_t>(curr.nackPacketCount - last.nackPacketCount);
  s[idx].kf_count = static_cast<uint16_t>(curr.kfCount - last.kfCount);
  s[idx].rtt = static_cast<uint16_t>(std::round(curr.rtt));
  s[idx].max_pts = curr.maxPacketTs;

  set_filled(idx + 1);

  return s[idx].packets_count; // if non-zero, tell caller to reset idle stats flag
}

bool CallStatsRecord::isPktCountZero() const
{
  if (filled() < maxSamples())
  {
    return false; // can be true only for full collection of samples
  }

  for (uint32_t i = 0; i < filled(); i++)
  {
    CallStatsSample s = type ? record.c.samples[i] : record.p.samples[0];
    if (s.packets_count)
    {
      return false;
    }
  }

  return true;
}


/////////////////////////
//
void CallStatsRecordCtx::AddStatsRecord(StatsBinLog* log, RTC::RtpStream* stream, bool isActive)
{
  // Write data if record is full, then continue collecting samples
  if (record.filled() == record.maxSamples())
  {
    if (nullptr != log)
    {
      log->OnLogWrite(this);
    }

    // Write a warning into ms.log when encountering series of zero incoming pkts in stats
    if (record.isPktCountZero())
    {
      if (warnIdleStats) // did not warn yet, and all samples in a record have 0 incoming pkts
      {
        MS_WARN_TAG(rtp,
        		"Zero pkts in stats record: callid=%s %s id=%s ssrc=%" PRIu32
				" start_tm=%" PRIu64" state=%s", record.call_id.c_str(),
				(record.type ? "consumer": "producer"),
				record.object_id.c_str(), record.ssrc(), record.start_tm(),
				(isActive ? "active" : "inactive"));

        warnIdleStats = false; // only write one warning for this series of 0s; allow to write it again after number of pkts > 0
      }
    }
    else {
      warnIdleStats = true;
    }

    // Wipe the data off and set record's start timestamp into the last sample's
    record.resetSamples(last.ts);
  }

  uint64_t nowMs = Utils::Time::currentStdEpochMs(); // Timestamp of a current measurement

  if (UINT64_UNSET == last.ts) // the first measurement ever during this session
  {
    stream->FillStats(last.packetsCount, last.bytesCount, last.framesCount, last.packetsLost, last.packetsDiscarded,
                      last.packetsRetransmitted, last.packetsRepaired, last.nackCount,
                      last.nackPacketCount, last.kfCount, last.rtt, last.maxPacketTs);
    last.ts = nowMs;
    record.resetSamples(nowMs);
    return; // done, wait till the next measurement
  }

  // Should have a room to add a sample
  MS_ASSERT(record.filled() >= 0 && record.filled() < record.maxSamples(),
    "Invalid record.filled=%" PRIu32, record.filled());

  curr.ts = nowMs;
  stream->FillStats(curr.packetsCount, curr.bytesCount, curr.framesCount, curr.packetsLost, curr.packetsDiscarded,
                    curr.packetsRetransmitted, curr.packetsRepaired, curr.nackCount,
                    curr.nackPacketCount, curr.kfCount, curr.rtt, curr.maxPacketTs);

  warnIdleStats = record.addSample(last, curr); // if packets count > 0, then write a warning next time we see a record with empty stats

  this->last = this->curr;
}


/////////////////////////
//
int StatsBinLog::LogOpen()
{
  int ret = 0;

  // Check that binlog directories are in place, try to restore them if not, then open fd
  if (!CreateBinlogDirsIfMissing() || !(this->fd = std::fopen(this->bin_log_file_path.c_str(), "a")))
  {
    MS_WARN_TAG(
      rtp,
      "binlog failed to open '%s'", this->bin_log_file_path.c_str()
    );
    ret = errno;
    this->fd = 0;
    initialized = false;
  }
  else
  {
    MS_DEBUG_TAG(
      rtp,
      "binlog opened '%s' [next_day_start_ts: %" PRIu64 "]",
      this->bin_log_file_path.c_str(),
      this->next_day_start_ts
    );
  }

  return ret;
}


void StatsBinLog::LogClose()
{
  if (this->fd)
  {
    std::fflush(this->fd);
    std::fclose(this->fd);
    this->fd = 0;

    MS_DEBUG_TAG(
      rtp,
      "binlog closed '%s'", this->bin_log_file_path.c_str()
    );
  }

  if (!this->initialized)
    return;

  // Do not preserve binlogs with too little data
  if (log_start_ts == UINT64_UNSET
      || log_last_ts == UINT64_UNSET
      || BINLOG_MIN_TIMESPAN > log_last_ts - log_start_ts)
  {
    std::remove(bin_log_file_path.c_str());
    MS_DEBUG_TAG(rtp, "binlog %s removed, short timespan (%" PRIu64 "-%" PRIu64 ")",
                  this->bin_log_file_path.c_str(), this->log_start_ts, log_last_ts);
    return;
  }

  // Move a closed file into "done" directory
  std::string bin_log_done_dir = Settings::configuration.logBinStatsPath + "/bin/done/";

  std::string logname; // get filename only out of full path
  std::size_t found = this->bin_log_file_path.find_last_of("/");
  if (std::string::npos == found)
  {
    MS_WARN_TAG(rtp, "Failed to extract binlog filename from %s, won't move it to %s", this->bin_log_file_path.c_str(), bin_log_done_dir.c_str());
  }
  else
  {
    char tmp[FILEPATH_LEN_MAX];
    auto logname = this->bin_log_file_path.substr(found + 1);
    uint64_t now = Utils::Time::currentStdEpochMs();
    snprintf(tmp, sizeof(tmp), "%s/bin/done/%s.%" PRIu64,
            Settings::configuration.logBinStatsPath.c_str(),
            logname.c_str(),
            now/1000);

    if (!CreateBinlogDirsIfMissing() || std::rename(this->bin_log_file_path.c_str(), tmp))
    {
      MS_WARN_TAG(rtp, "failed to move %s to %s", this->bin_log_file_path.c_str(), tmp);
    }
    else
    {
      MS_DEBUG_TAG(rtp, "moved binlog %s to %s", this->bin_log_file_path.c_str(), tmp);
    }
  }
}


int StatsBinLog::OnLogWrite(CallStatsRecordCtx* ctx)
{
  int ret = 0;
  bool signal_set = false;

  uint64_t now = Utils::Time::currentStdEpochMs();

  if (!this->initialized)
    return ret;

  // Rotate logs at the end of the day.
  // Log rotation based on their duration is disabled
  // because desired logs duration is < DAY_IN_MS anyway
  // if (now - this->log_start_ts > timespan_less_than_day_in_ms || ... to enable
  if (now > this->next_day_start_ts)
  {
    signal_set = true;
  }

  if(this->fd && ctx && (ctx->record.filled() || signal_set))
  {
    if (!ctx->record.fwriteRecord(this->fd))
    {
      ret = errno;
      std::fclose(this->fd);
      this->fd = 0;
      LogClose(); // must still call LogClose() to copy this log file into "done" directory
    }
    else
    {
      this->log_last_ts = ctx->LastTs();
    }
    std::fflush(this->fd);
  }

  if(signal_set || !this->fd)
  {
    if (this->fd)
    {
      LogClose();
    }
    if (signal_set)
    {
      UpdateLogTimestamps(now);
    }
    LogOpen();
  }

  if (!this->fd)
  {
    MS_WARN_TAG(
      rtp,
      "binlog can't write, fd=0"
    );
  }
  if (!ctx)
  {
    MS_WARN_TAG(
      rtp,
      "binlog can't write, ctx=0"
    );
  }

  return ret;
}


// If Settings::configuration.logBinStatsPath does not exist and can't be created, then disable stats collection: Settings::configuration.logBinStatsDisabled = true;
// If subdirectories creation fails then we will keep trying again because automated scripts may delete empty directories during runtime
bool StatsBinLog::CreateBinlogDirsIfMissing()
{
  std::string bin_log_dir      = Settings::configuration.logBinStatsPath + "/bin/";
  std::string bin_log_curr_dir = Settings::configuration.logBinStatsPath + "/bin/current/";
  std::string bin_log_done_dir = Settings::configuration.logBinStatsPath + "/bin/done/";

  struct stat info;
  int ret = 0;

  if (Settings::configuration.logBinStatsDisabled)
    return false;

  if( stat( Settings::configuration.logBinStatsPath.c_str(), &info ) != 0 )
  {
    if (errno == ENOENT)
    {
      ret = mkdir(bin_log_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      if (ret != 0 && errno != EEXIST)
      {
        MS_WARN_TAG(rtp, "failed to create top folder %s for binlog files management: %s, disabling stats collection", Settings::configuration.logBinStatsPath.c_str(), std::strerror(errno));
        Settings::configuration.logBinStatsDisabled = true;
        return false;
      }
    }
  }
  else
  {
    if (!S_ISDIR(info.st_mode))
    {
      MS_WARN_TAG(rtp, "found %s but it is not a directory, disabling stats collection", Settings::configuration.logBinStatsPath.c_str());
      Settings::configuration.logBinStatsDisabled = true;
      return false;
    }
  }

  if( stat( bin_log_dir.c_str(), &info ) != 0 )
  {
    if (errno == ENOENT)
    {
      ret = mkdir(bin_log_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      if (ret != 0 && errno != EEXIST)
      {
        MS_WARN_TAG(rtp, "failed to create folder %s for binlog files management: %s", bin_log_dir.c_str(), std::strerror(errno));
        return false;
      }
    }
  }
  else
  {
    if (!S_ISDIR(info.st_mode))
    {
      MS_WARN_TAG(rtp, "found %s but it is not a directory", bin_log_dir.c_str());
      return false;
    }
  }

  // Now that /var/log/sfu/bin/ exists, take care of "current" and "done" directories
  if( stat( bin_log_curr_dir.c_str(), &info ) != 0 )
  {
    if (errno == ENOENT)
    {
      ret = mkdir(bin_log_curr_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      if (ret != 0 && errno != EEXIST)
      {
        MS_WARN_TAG(rtp, "failed to create folder %s for writing binlogs: %s", bin_log_curr_dir.c_str(), std::strerror(errno));
        return false;
      }
    }
  }
  else
  {
    if (!S_ISDIR(info.st_mode))
    {
      MS_WARN_TAG(rtp, "found %s but it is not a directory", bin_log_curr_dir.c_str());
      return false;
    }
  }

  if( stat( bin_log_done_dir.c_str(), &info ) != 0 )
  {
    if (errno == ENOENT)
    {
      ret = mkdir(bin_log_done_dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); // | S_IXOTH?
      if (ret != 0 && errno != EEXIST)
      {
        MS_WARN_TAG(rtp, "failed to create folder %s for moving complete binlogs: %s", bin_log_done_dir.c_str(), std::strerror(errno));
        return false;
      }
    }
  }
  else
  {
    if (!S_ISDIR(info.st_mode))
    {
      MS_WARN_TAG(rtp, "found %s but it is not a directory", bin_log_done_dir.c_str());
      return false;
    }
  }

  return true;
}


void StatsBinLog::InitLog(std::function<std::string(uint64_t)>&& templateFunction)
{
	this->initialized = false;

	if (Settings::configuration.logBinStatsDisabled)
		return;

	this->file_name_template_function = std::move(templateFunction);
    this->bin_log_name_template = Settings::configuration.logBinStatsPath + "/bin/current/%s";

	uint64_t const now = Utils::Time::currentStdEpochMs();
	UpdateLogTimestamps(now);

	MS_DEBUG_TAG(rtp, "binlog %s", this->current_bin_log_name.c_str());

	CreateBinlogDirsIfMissing();
	if (Settings::configuration.logBinStatsDisabled)
		return;

	this->sampling_interval = CALL_STATS_BIN_LOG_SAMPLING;
	this->initialized = true;
}


void StatsBinLog::UpdateLogTimestamps(uint64_t now)
{
  char buff[FILEPATH_LEN_MAX];

  this->log_start_ts = now;

  this->next_day_start_ts = ((now / DAY_IN_MS) + 1) * DAY_IN_MS;

  this->current_bin_log_name = this->file_name_template_function(log_start_ts);
  if(this->current_bin_log_name.length() > FILENAME_LEN_MAX) {
      MS_ERROR("Filename is longer than FILENAME_LEN_MAX: %" PRIu16, FILENAME_LEN_MAX);
  }

  snprintf(buff, sizeof(buff), this->bin_log_name_template.c_str(), this->current_bin_log_name.c_str());
  this->bin_log_file_path.assign(buff);
}


void StatsBinLog::DeinitLog()
{
  LogClose();

  this->initialized       = false;

  this->log_start_ts      = UINT64_UNSET;
  this->next_day_start_ts = UINT64_UNSET;
  this->log_last_ts       = UINT64_UNSET;

  this->bin_log_name_template.clear();
  this->current_bin_log_name.clear();
  this->bin_log_file_path.clear();
}

std::string GetUserIdFromAppData(const json& appData) {
    if (appData.contains("userId") && appData["userId"].is_string()) {
        std::string userId = appData["userId"].get<std::string>();

        for (char& c : userId) {
            if (!std::isalnum(c) && c != '-') {
                c = '-';
            }
        }

        return userId;
    }
    return "";
}

std::string ProducerFileName(
        const std::string &callId,
        const std::string &producerId,
        const std::string &userId,
        uint64_t timestamp,
        const std::string &version
) {
    return "ms_p_" + userId + "_" + callId + "_" + producerId + "_" + std::to_string(timestamp) + "." + version + ".bin";
}

std::string ConsumerFileName(const std::string& callId, uint64_t timestamp, const std::string& version) {
    return "ms_c_" + callId + "_" + std::to_string(timestamp) + "." + version + ".bin";
}
} //Lively
