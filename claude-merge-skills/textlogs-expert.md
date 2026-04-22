# Text Logging Expert

**Last Updated**: 2026-03-11

You are the expert on Lively's text logging system. This includes per-worker log files, developer log levels, and Lively-specific log macros.

## Feature Overview

Lively extends mediasoup's logging with:
1. **Per-worker log files** — Logs written to files via WORKER_MSLOG_OPEN/ROTATE
2. **Developer log levels** — Additional granularity (LOG_DEV_DEBUG, LOG_DEV_WARN, LOG_DEV_NONE)
3. **Lively log macros** — MS_DEBUG_DEV, MS_WARN_DEV, MS_DEBUG_TAG_LIVELYAPP

---

## Silent Failure Modes

### Mode 1: LogDevLevel Enum Values Changed
**Symptom**: Wrong log level filtering - too many or too few logs
**Cause**: Enum integer values differ from v3-lively
**Detection**:
```bash
grep -A5 "enum.*LogDevLevel\|LOG_DEV_NONE\|LOG_DEV_WARN\|LOG_DEV_DEBUG" worker/include/Settings.hpp
```
**Expected**: `LOG_DEV_NONE = 0`, `LOG_DEV_WARN = 2`, `LOG_DEV_DEBUG = 3`
**Failure**: Different values

### Mode 2: Log Macro Definitions Changed
**Symptom**: Logs don't appear at expected level
**Cause**: Macro condition changed
**Detection**:
```bash
grep -A3 "define MS_DEBUG_DEV\|define MS_WARN_DEV" worker/include/Logger.hpp
```
**Expected**: Checks `logDevLevel >= LOG_DEV_DEBUG` / `LOG_DEV_WARN`
**Failure**: Different condition

### Mode 3: MSLOG_OPEN Handler Missing
**Symptom**: Log files never created
**Cause**: Request handler removed from Worker.ts
**Detection**:
```bash
grep "WORKER_MSLOG_OPEN\|MSLOG_OPEN" node/src/Worker.ts
```
**Expected**: Handler exists with channel.request call
**Failure**: No match

### Mode 4: LOGGER_WRITE_FAILED Not Handled
**Symptom**: Write failures go unnoticed
**Cause**: Notification handler missing
**Detection**:
```bash
grep "LOGGER_WRITE_FAILED" node/src/Worker.ts
```
**Expected**: Case in notification handler
**Failure**: No match

### Mode 5: FBS Schema Missing Tables
**Symptom**: Requests/notifications fail to parse
**Cause**: Tables removed from log.fbs
**Detection**:
```bash
grep -E "MslogOpenRequest|WriteFailedNotification" worker/fbs/log.fbs
```
**Expected**: Both tables defined
**Failure**: Missing either

---

## Mechanical Audit Checklist

### Logger.hpp

#### 1.1 Log Macros Defined
```bash
grep -n "define MS_DEBUG_DEV\|define MS_WARN_DEV\|define MS_DEBUG_TAG_LIVELYAPP" worker/include/Logger.hpp
```
**Expected**: 3 macro definitions
- [ ] MS_DEBUG_DEV defined
- [ ] MS_WARN_DEV defined
- [ ] MS_DEBUG_TAG_LIVELYAPP defined

#### 1.2 LogDevLevel Check in Macros
```bash
grep -A2 "MS_DEBUG_DEV\|MS_WARN_DEV" worker/include/Logger.hpp | grep "logDevLevel"
```
**Expected**: `logDevLevel == LogDevLevel::LOG_DEV_DEBUG` and `>= LOG_DEV_WARN`
- [ ] DEBUG macro checks correct level
- [ ] WARN macro checks correct level

#### 1.3 logTraceEnabled Used
```bash
grep "logTraceEnabled" worker/include/Logger.hpp
```
**Expected**: Used in MS_TRACE or similar
- [ ] logTraceEnabled referenced

---

### Settings.hpp

#### 2.1 LogDevLevel Member
```bash
grep "logDevLevel" worker/include/Settings.hpp
```
**Expected**: `LogDevLevel logDevLevel{LogDevLevel::LOG_DEV_NONE};`
- [ ] Member declared with default

#### 2.2 logTraceEnabled Member
```bash
grep "logTraceEnabled" worker/include/Settings.hpp
```
**Expected**: `bool logTraceEnabled{false};`
- [ ] Member declared

---

### Worker.ts (Node.js)

#### 3.1 MSLOG_OPEN Handler
```bash
grep -B2 -A10 "MSLOG_OPEN\|logOpen" node/src/Worker.ts
```
**Expected**:
- Method exists
- Calls channel.request with WORKER_MSLOG_OPEN
- [ ] logOpen method exists
- [ ] Sends WORKER_MSLOG_OPEN request

#### 3.2 MSLOG_ROTATE Handler
```bash
grep "MSLOG_ROTATE" node/src/Worker.ts
```
**Expected**: Request call present
- [ ] MSLOG_ROTATE request exists

#### 3.3 LOGGER_WRITE_FAILED Handler
```bash
grep -B2 -A5 "LOGGER_WRITE_FAILED" node/src/Worker.ts
```
**Expected**: Case in notification switch
- [ ] LOGGER_WRITE_FAILED handled

---

### log.fbs

#### 4.1 Schema Tables
```bash
cat worker/fbs/log.fbs
```
**Expected tables**:
- `Log` with `data: string`
- `MslogOpenRequest` with `mslogname: string`
- `WriteFailedNotification` with source, error, file, data
- [ ] Log table present
- [ ] MslogOpenRequest table present
- [ ] WriteFailedNotification table present

---

### notification.fbs

#### 5.1 LOGGER_WRITE_FAILED Enum
```bash
grep "LOGGER_WRITE_FAILED" worker/fbs/notification.fbs
```
**Expected**: `LOGGER_WRITE_FAILED,` in Event enum
- [ ] Enum value present

---

## File Ownership

| File | Logging-Related Code |
|------|---------------------|
| `worker/src/Logger.cpp` | Log file handling, level management |
| `worker/include/Logger.hpp` | LogDevLevel enum, log macros |
| `worker/include/Settings.hpp` | logDevLevel, logTraceEnabled members |
| `worker/fbs/log.fbs` | Logger request/notification schemas |
| `worker/fbs/notification.fbs` | LOGGER_WRITE_FAILED enum |
| `node/src/Worker.ts` | WORKER_MSLOG_OPEN/ROTATE handlers |
| `node/src/WorkerTypes.ts` | Logger-related types |

---

## TRANSCODE Guard Rules

Text logging has **NO TRANSCODE guards**. All logging features run in production.

Exception: Some log messages may have conditional output based on ShmTransport:
```cpp
#ifdef TRANSCODE
    if (dynamic_cast<RTC::ShmTransport*>(this) == nullptr)
#endif
    MS_DEBUG_TAG_LIVELYAPP(bwe, this->appData, "message");
```
This is correct — it suppresses ShmTransport-specific noise in production.

---

## Format String Verification

Log format strings must match v3-lively exactly. Changes are behavioral regressions.

```bash
# Compare format strings
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep "MS_DEBUG_TAG_LIVELYAPP" | head -5
grep "MS_DEBUG_TAG_LIVELYAPP" worker/src/RTC/Transport.cpp | head -5
```

---

## Invocation

```
/textlogs-expert audit
```

Performs complete text logging system audit.
