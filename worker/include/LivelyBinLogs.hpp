#ifndef MS_LIVELY_BIN_LOGS_HPP
#define MS_LIVELY_BIN_LOGS_HPP

#include "common.hpp"
#include <cstring>
#include "RTC/RtpStream.hpp"
#include <functional>

#define BINLOG_MIN_TIMESPAN   20000
#define BINLOG_FORMAT_VERSION "223fac"
//"c1b126"

// CALL_STATS_BIN_LOG_CONS_REC_NUM * sizeof(CallStatsSample)
// and
// CALL_STATS_BIN_LOG_PROD_REC_NUM * sizeof(CallStatsSample)
// should be divisible by 16 b/c of alignment concerns;
// otherwise, there will be random sized padding added
// at the end of the array of records to align
// ConsumerRecord and ProducerRecord structs to 16 bytes.
// sizeof(CallStatsSample)== 28 bytes, the number of samples should be divisible by 4.
// Alternative is to increase it to 32 at a cost of wasting 4 bytes per data record.

#define CALL_STATS_BIN_LOG_PROD_REC_NUM 4
#define CALL_STATS_BIN_LOG_CONS_REC_NUM 8
#define CALL_STATS_BIN_LOG_SAMPLING    2000

#define UINT16_UNSET                   ((uint16_t)-1)
#define UINT32_UNSET                   ((uint32_t)-1)
#define UINT64_UNSET                   ((uint64_t)-1)
#define ZERO_UUID "00000000-0000-0000-0000-000000000000"
#define UUID_BYTE_LEN 16
#define UUID_CHAR_LEN 36

namespace Lively
{
struct StreamStats;
class StatsBinLog;

// Data sample
struct CallStatsSample
{
  uint16_t epoch_len;             // epoch duration in milliseconds counting from the previous sample
  uint16_t packets_count;         // RTP packets per epoch
  uint16_t packets_lost;
  uint16_t packets_discarded;
  uint16_t packets_retransmitted;
  uint16_t packets_repaired;
  uint16_t nack_count;            // number of NACK requests
  uint16_t nack_pkt_count;        // number of NACK packets requested
  uint16_t kf_count;              // Requests for key frame, PLI or FIR, see RequestKeyFrame()
  uint16_t rtt;
  uint32_t max_pts;
  uint32_t bytes_count;
  uint32_t frames_count;          // frames count based on pkt timestamps
};

// Record headers are aligned to 16 bytes.
// timestamp filled ssrc
// 8         4      4
// timestamp filled ssrc consumer_uuid producer_uuid
// 8          4     4    16            16           
struct ConsumerRecord
{
  uint64_t        start_tm {UINT64_UNSET};                  // the record start timestamp in milliseconds
  uint32_t        ssrc {UINT32_UNSET};                      // ssrc as in original RTP stream
  uint16_t        filled {UINT16_UNSET};                    // number of filled records in the array below
  uint8_t         payload {0};                              // payload id as in original RTP stream
  uint8_t         content;                                  // 'a' or 'v'
  uint8_t         consumer_id [UUID_BYTE_LEN];              //
  uint8_t         producer_id [UUID_BYTE_LEN];              //
  CallStatsSample samples[CALL_STATS_BIN_LOG_CONS_REC_NUM]; // collection of data samples
};

struct ProducerRecord
{
  uint64_t        start_tm {UINT64_UNSET};                  // the record start timestamp in milliseconds
  uint32_t        ssrc {UINT32_UNSET};                      // ssrc as in original RTP stream
  uint16_t        filled {UINT16_UNSET};                    // number of filled records in the array below
  uint8_t         payload {0};                              // payload id as in original RTP stream
  uint8_t         content;                                  // 'a' or 'v'
  CallStatsSample samples[CALL_STATS_BIN_LOG_PROD_REC_NUM]; // collection of data samples
};

//enum class LogType {
//	Consumer,
//	Producer,
//};

class CallStatsRecord
{
  public:
    uint8_t     type {0};     // 0 for producer or 1 for consumer
    std::string call_id;      // call id: uuid4() string
    std::string object_id;    // uuid4(), producer or consumer id, depending on source
    std::string producer_id;  // uuid4(), undef if source is producer, or consumer's corresponding producer id

  public:
    CallStatsRecord(uint64_t objType, uint16_t ssrc, uint8_t payload, char content, std::string callId, std::string objId, std::string producerId);

    bool fwriteRecord(std::FILE* fd);
    
    uint32_t filled() const {return type ? record.c.filled : record.p.filled;}
    size_t maxSamples() const {
    	return type ?
    			(sizeof(record.c.samples) / sizeof(CallStatsSample)) :
				(sizeof(record.p.samples) / sizeof(CallStatsSample));
    }

    void set_filled(uint32_t n)
    {
      if (type) 
        record.c.filled = n; 
      else
        record.p.filled = n; 
    }

    uint64_t start_tm() const {return type ? record.c.start_tm : record.p.start_tm;}
    void set_start_tm(uint64_t t)
    {
      if (type) 
        record.c.start_tm = t; 
      else
        record.p.start_tm = t;
    }
    
    uint32_t ssrc() const {return type ? record.c.ssrc : record.p.ssrc;}

    void resetSamples(uint64_t nowMs);
    bool addSample(StreamStats& last, StreamStats& curr);    

    bool isPktCountZero() const;

  private:
    // Binary data record
    union Record
    {
      ConsumerRecord c;
      ProducerRecord p;
      Record() { memset( this, 0, sizeof( Record ) ); }
    } record;

    bool uuidToBytes(std::string uuid, uint8_t *out);
    uint8_t* hexStrToBytes(const char* from, int num, uint8_t *to);
  };

  struct StreamStats
  {
    uint64_t ts {UINT64_UNSET}; // ts when data was received from a stream        
    size_t packetsCount {0};
    size_t bytesCount {0};
    size_t framesCount {0};
    uint32_t packetsLost {0};
    size_t packetsDiscarded {0};
    size_t packetsRetransmitted {0};
    size_t packetsRepaired {0};
    size_t nackCount {0};
    size_t nackPacketCount {0};
    size_t kfCount {0};
    float rtt {0};
    uint32_t maxPacketTs {0};
  };

  class CallStatsRecordCtx
  {
  public:
    CallStatsRecord record;
  
  private:
    StreamStats last {}; // before very first sample is recorded, ts is unset, then it will always be set into some valid time
    StreamStats curr {};

    bool warnIdleStats {true}; // if true we can write a warning once into ms.log if all samples in record have zero packets count

  public:
    CallStatsRecordCtx(uint64_t objType, uint32_t ssrc, uint8_t payload, char content, std::string callId, std::string objId, std::string producerId) : record(objType, ssrc, payload, content, callId, objId, producerId) {}
    void AddStatsRecord(StatsBinLog* log, RTC::RtpStream* stream, bool isActive); // either recv or send stream
    uint64_t LastTs() const { return last.ts; }
  };

  // Binary log presentation
  class StatsBinLog
  {
  public:
    std::string   bin_log_file_path;                               // binary log's full file name: combo of call id, timestamp and "version"
    std::FILE*    fd {0};                                          
    uint64_t      sampling_interval {CALL_STATS_BIN_LOG_SAMPLING}; // frequency of collecting samples, non-configurable
  
  private:
    bool          initialized {false};
    std::string   bin_log_name_template;          // Log name template, use to rotate log, keep same name except for timestamp
    std::string   current_bin_log_name;
    std::function<std::string(uint64_t)> file_name_template_function;
    const char    version[7] = BINLOG_FORMAT_VERSION;

    uint64_t      log_start_ts {UINT64_UNSET};      // Timestamp included into log's name; used to discard short logs, may be used for log rotation based on time passed 
    uint64_t      next_day_start_ts {UINT64_UNSET}; // Timestamp for start of the next day; used to rotate logs at the beginning of each day
    uint64_t      log_last_ts {UINT64_UNSET};       // Timestamp of the last record in the log, various sources may share same logfile; used to discard short logs

  public:
    StatsBinLog() = default;

    bool IsInitialized() {return initialized;}
    void InitLog(char type, std::string id1, std::string id2); // if type is producer, then log name is a combo of callid, producerid and timestamp
    void InitLog(std::function<std::string(uint64_t)>&& templateFunction);
//    void InitLogNew2(std::string fileNameTemplate);
    int OnLogWrite(CallStatsRecordCtx* ctx);
    void DeinitLog();   // Closes log file and deinitializes state variables

  private:
    int LogOpen();
    void LogClose();
    void UpdateLogTimestamps(uint64_t now);
    bool CreateBinlogDirsIfMissing();
  };

    std::string ProducerFileName(
            const std::string &callId,
            const std::string &producerId,
            const std::string &userId,
            uint64_t timestamp,
            const std::string &version
    );

    std::string ConsumerFileName(const std::string& callId, uint64_t timestamp, const std::string& version);
    std::string GetUserIdFromAppData(const json& appData);

} //Lively

#endif // MS_LIVELY_BIN_LOGS_HPP
