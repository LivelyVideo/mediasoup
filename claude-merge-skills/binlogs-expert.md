# Binary Stats Logging Expert

**Last Updated**: 2026-03-11

You are the expert on Lively's binary stats logging system (`ms_p_*` and `ms_c_*` files). This feature produces binary telemetry files for producer and consumer statistics.

## Feature Overview

Binary stats logging writes periodic statistics to binary files for offline analysis. It consists of:
- **Producer binlogs** (`ms_p_*.binlog`) — per-producer stats
- **Consumer binlogs** (`ms_c_*.binlog`) — per-consumer stats

## Activation Chain

Every link must be present for files to appear:

```
1. WebRtcTransport constructor
   └── Sets producerBinLogEnabled = true

2. Transport constructor
   └── Calls consumersBinLog.InitLog(...) with ConsumerFileName lambda

3. Producer constructor
   └── Calls binLog.InitLog(...) (only if producerBinLogEnabled is true)

4. Transport::OnTimer (every 2 seconds)
   └── Calls FillBinLogStats() on all producers and consumers

5. File I/O
   └── LivelyBinLogs.cpp writes data
```

---

## Silent Failure Modes

### Mode 1: Timer Never Created
**Symptom**: No binlog files appear, no errors
**Cause**: TRANSCODE guard around timer creation
**Detection**:
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0` (no guard)
**Failure**: `1` or more (guard present = REGRESSION)

### Mode 2: InitLog Called with Empty CallId
**Symptom**: Files not created, no error logged
**Cause**: callId not extracted from FBS or empty in appData
**Detection**:
```bash
grep -B5 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp | grep "callId"
```
**Expected**: `std::string const callId = this->lively.callId;`
**Failure**: `callId = ""` or missing line

### Mode 3: FillBinLogStats Never Called
**Symptom**: Files created but empty/stale
**Cause**: TRANSCODE guard around OnTimer binlog section
**Detection**:
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 4: producerBinLogEnabled Not Set
**Symptom**: Producer binlogs not created (consumer binlogs work)
**Cause**: WebRtcTransport doesn't set the flag
**Detection**:
```bash
grep -c "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
```
**Expected**: `2` (both constructors)
**Failure**: `0` or `1`

### Mode 5: Consumer Binlog Record Not Initialized
**Symptom**: Consumer binlogs missing for specific consumer type
**Cause**: rtpStreamBinLogRecord initialization missing or guarded
**Detection**:
```bash
grep -c "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp
```
**Expected**: `3` or more (init, cleanup, usage)
**Failure**: `0` or only cleanup

### Mode 6: Destructor Cleanup Guarded
**Symptom**: Memory leaks (no visible symptom in logs)
**Cause**: TRANSCODE guard around delete rtpStreamBinLogRecord
**Detection**:
```bash
grep -B2 "delete.*rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep -c "ifdef"
```
**Expected**: `0`
**Failure**: `1` or more

---

## Mechanical Audit Checklist

### Transport.cpp

#### 1.1 Timer Creation
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp
```
**Expected output**:
```
        // Initialize binary logging timer if not disabled
        if (!Settings::configuration.logBinStatsDisabled)
        {
            this->binLogTimer = new TimerHandle(this);
```
**Failure indicator**: `#ifdef TRANSCODE` in output
- [ ] Timer created unconditionally (no TRANSCODE guard)

#### 1.2 Timer Start
```bash
grep -n "binLogTimer->Start" worker/src/RTC/Transport.cpp
```
**Expected**: At least 1 match in Connected() region
- [ ] Timer started

#### 1.3 Timer Stop in Destructor
```bash
grep -n "binLogTimer" worker/src/RTC/Transport.cpp | grep -E "delete|Stop|Close"
```
**Expected**: Cleanup in destructor
- [ ] Timer stopped/deleted in destructor

#### 1.4 FillBinLogStats in OnTimer
```bash
grep -B3 "FillBinLogStats" worker/src/RTC/Transport.cpp | head -15
```
**Expected**: No `#ifdef TRANSCODE` before the call
- [ ] FillBinLogStats called unconditionally

#### 1.5 consumersBinLog.InitLog
```bash
grep -A10 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp
```
**Expected**: Lambda with `[clientReferrer, callId]` capture
- [ ] InitLog called with correct lambda

---

### WebRtcTransport.cpp

#### 2.1 producerBinLogEnabled
```bash
grep -n "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
```
**Expected**: 2 matches (both constructors)
- [ ] Set in first constructor
- [ ] Set in second constructor

---

### Producer.cpp

#### 3.1 binLog.InitLog
```bash
grep -B5 -A10 "binLog.InitLog" worker/src/RTC/Producer.cpp
```
**Expected**:
- No `#ifdef TRANSCODE` guard
- Lambda captures `[callId, producerId, userId, clientReferrer]`
- [ ] InitLog called unconditionally
- [ ] Lambda captures all 4 variables

#### 3.2 Variable Sources
```bash
grep -E "userId =|callId =|clientReferrer =" worker/src/RTC/Producer.cpp | head -10
```
**Expected**: Each extracted from `data->fieldName()->str()`
- [ ] userId from FBS
- [ ] callId from transport/FBS
- [ ] clientReferrer from FBS

#### 3.3 Destructor Cleanup
```bash
grep -B2 -A2 "binLog" worker/src/RTC/Producer.cpp | grep -E "~Producer|delete|Deinit"
```
**Expected**: Cleanup without TRANSCODE guard
- [ ] Destructor cleanup unconditional

---

### Consumer Files

For each: SimpleConsumer, SimulcastConsumer, SvcConsumer, PipeConsumer

#### 4.1 Check SimpleConsumer
```bash
grep -n "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp
```
**Expected**: Init line, delete line, usage line - no TRANSCODE guards
- [ ] SimpleConsumer binlog handling intact

#### 4.2 Check SimulcastConsumer
```bash
grep -n "rtpStreamBinLogRecord" worker/src/RTC/SimulcastConsumer.cpp
```
- [ ] SimulcastConsumer binlog handling intact

#### 4.3 Check SvcConsumer
```bash
grep -n "rtpStreamBinLogRecord" worker/src/RTC/SvcConsumer.cpp
```
- [ ] SvcConsumer binlog handling intact

#### 4.4 Check PipeConsumer
```bash
grep -n "rtpStreamBinLogRecords" worker/src/RTC/PipeConsumer.cpp
```
**Note**: PipeConsumer uses plural `rtpStreamBinLogRecords` (map)
- [ ] PipeConsumer binlog handling intact

---

## File Ownership

| File | Binlog-Related Code |
|------|---------------------|
| `worker/src/RTC/Transport.cpp` | Timer creation, OnTimer callback, consumersBinLog init |
| `worker/src/RTC/WebRtcTransport.cpp` | `producerBinLogEnabled = true` |
| `worker/src/RTC/Producer.cpp` | binLog.InitLog(), FillBinLogStats() |
| `worker/src/RTC/SimpleConsumer.cpp` | rtpStreamBinLogRecord init/cleanup |
| `worker/src/RTC/SimulcastConsumer.cpp` | rtpStreamBinLogRecord init/cleanup |
| `worker/src/RTC/SvcConsumer.cpp` | rtpStreamBinLogRecord init/cleanup |
| `worker/src/RTC/PipeConsumer.cpp` | rtpStreamBinLogRecords init/cleanup |
| `worker/include/RTC/Transport.hpp` | StatsBinLog members |
| `worker/include/RTC/Producer.hpp` | binLog member |
| `worker/src/LivelyBinLogs.cpp` | File I/O implementation |

---

## TRANSCODE Guard Rules for Binlogs

**NOT guarded** (must run in production):
- Timer creation (`new TimerHandle(this)`)
- Timer start/stop/close
- `binLog.InitLog()` calls
- `FillBinLogStats()` calls
- `rtpStreamBinLogRecord` initialization
- `delete rtpStreamBinLogRecord` cleanup

**Guarded** (transcode-only):
- File I/O operations in `LivelyBinLogs.cpp` (some builds)

---

## Filename Format Reference

### Producer: `ms_p_<userId>_<callId>_<producerId>_<streamIdx>_<timestamp>.binlog`
### Consumer: `<clientReferrer>/ms_c_<callId>_<consumerId>_<timestamp>.binlog`

---

## Invocation

```
/binlogs-expert audit
```

Performs complete binlog system audit across all files.
