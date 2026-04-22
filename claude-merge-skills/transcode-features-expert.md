# Transcode Features Expert

**Last Updated**: 2026-03-11

You are the expert on Lively's transcode-only features. These features are compiled only when the TRANSCODE flag is defined and include ShmTransport and related functionality.

## Feature Overview

Transcode features enable shared-memory communication between mediasoup and transcoding processes. They are:
- **Excluded** from the production (notranscode) build
- **Included** only in the transcode build

---

## Silent Failure Modes

### Mode 1: Guard Removed from ShmTransport Include
**Symptom**: Compilation errors in notranscode build
**Cause**: `#include "RTC/ShmTransport.hpp"` not guarded
**Detection**:
```bash
grep -B1 'include.*ShmTransport' worker/src/RTC/Transport.cpp
```
**Expected**: `#ifdef TRANSCODE` on line before
**Failure**: No guard

### Mode 2: Guard Removed from ShmConsumer Creation
**Symptom**: Undefined symbol errors in notranscode build
**Cause**: ShmConsumer instantiation not guarded
**Detection**:
```bash
grep -B2 "new RTC::ShmConsumer" worker/src/RTC/Transport.cpp
```
**Expected**: `#ifdef TRANSCODE` above
**Failure**: No guard

### Mode 3: Guard ADDED to Binlog Timer (REGRESSION)
**Symptom**: Binlogs don't appear in production
**Cause**: Timer creation wrapped in TRANSCODE guard
**Detection**:
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 4: Guard ADDED to FillBinLogStats (REGRESSION)
**Symptom**: Binlog files empty
**Cause**: FillBinLogStats call wrapped in TRANSCODE guard
**Detection**:
```bash
grep -B3 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 5: Guard ADDED to InitLog (REGRESSION)
**Symptom**: Binlogs never initialized
**Cause**: InitLog call wrapped in TRANSCODE guard
**Detection**:
```bash
grep -B3 "InitLog" worker/src/RTC/Producer.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 6: dynamic_cast Not Guarded
**Symptom**: Compilation errors (ShmTransport undefined)
**Cause**: `dynamic_cast<ShmTransport*>` outside guard
**Detection**:
```bash
grep -B2 "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: Count equals number of dynamic_cast occurrences
**Failure**: Less guards than casts

---

## Mechanical Audit Checklist

### Transport.cpp Guards

#### 1.1 ShmTransport Include Guard
```bash
grep -B1 -A1 'include.*ShmTransport\|include.*ShmConsumer' worker/src/RTC/Transport.cpp
```
**Expected**:
```cpp
#ifdef TRANSCODE
#include "RTC/ShmConsumer.hpp"
#include "RTC/ShmTransport.hpp"
#endif
```
- [ ] Both includes inside TRANSCODE guard

#### 1.2 ShmConsumer Creation Guard
```bash
grep -B5 "new RTC::ShmConsumer" worker/src/RTC/Transport.cpp
```
**Expected**: Inside `#ifdef TRANSCODE` block
- [ ] ShmConsumer creation guarded

#### 1.3 dynamic_cast Guards
```bash
grep -n "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp
```
Then for each line, check guard:
```bash
grep -B3 "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp
```
**Expected**: Each has `#ifdef TRANSCODE` above
- [ ] All dynamic_cast<ShmTransport*> guarded

#### 1.4 Conditional Logging Pattern
```bash
grep -B2 -A2 "dynamic_cast.*ShmTransport.*nullptr" worker/src/RTC/Transport.cpp
```
**Expected pattern**:
```cpp
#ifdef TRANSCODE
    if (dynamic_cast<RTC::ShmTransport*>(this) == nullptr)
#endif
    MS_DEBUG_TAG_LIVELYAPP(...)
```
- [ ] Conditional logging pattern correct

---

### What Should NOT Be Guarded

#### 2.1 Timer Creation
```bash
grep -B5 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep "TRANSCODE"
```
**Expected**: No output (not guarded)
- [ ] Timer creation NOT guarded

#### 2.2 binLog.InitLog
```bash
grep -B5 "binLog.InitLog" worker/src/RTC/Producer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] InitLog NOT guarded

#### 2.3 FillBinLogStats
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] FillBinLogStats NOT guarded

#### 2.4 rtpStreamBinLogRecord
```bash
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] Consumer binlog record NOT guarded

#### 2.5 EmitProducerStats
```bash
grep -B3 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] Producer stats NOT guarded

---

### Guard Count Verification

#### 3.1 Compare Guard Counts
```bash
# v3-lively guard count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"

# Current guard count
grep -c "ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```
**Expected**: Same count (or document why different)
- [ ] Guard count matches v3-lively

#### 3.2 List All Guards
```bash
grep -n "#ifdef TRANSCODE\|#endif" worker/src/RTC/Transport.cpp | head -20
```
**Verify**: Each guard pair makes sense
- [ ] All guards properly paired
- [ ] No unexpected guards

---

## TRANSCODE Guard Inventory

### What SHOULD Be Guarded (transcode-only)

| Location | Purpose |
|----------|---------|
| `#include "RTC/ShmTransport.hpp"` | ShmTransport header |
| `#include "RTC/ShmConsumer.hpp"` | ShmConsumer header |
| `new RTC::ShmConsumer(...)` | ShmConsumer creation |
| `dynamic_cast<ShmTransport*>` checks | Type-specific behavior |
| Conditional logging before ShmTransport check | Suppress noise |
| ShmTransport-specific request handlers | Transcode-only requests |

### What Should NOT Be Guarded (production features)

| Location | Why |
|----------|-----|
| Timer creation | Timers needed for stats in production |
| binLog.InitLog() calls | Initialization needed even without I/O |
| FillBinLogStats() calls | Data collection needed |
| rtpStreamBinLogRecord init | Record objects needed |
| Destructor cleanup | Memory management needed |
| EmitProducerStats() | Stats emission needed |
| lastProducerStatsReport | Stats feature needed |

---

## File Ownership

| File | Transcode-Related Code |
|------|----------------------|
| `worker/src/RTC/ShmTransport.cpp` | Entire file transcode-only |
| `worker/include/RTC/ShmTransport.hpp` | Entire file transcode-only |
| `worker/src/RTC/ShmConsumer.cpp` | Entire file transcode-only |
| `worker/include/RTC/ShmConsumer.hpp` | Entire file transcode-only |
| `worker/src/RTC/Router.cpp` | ShmTransport creation handler |
| `worker/src/RTC/Transport.cpp` | ShmTransport includes, type checks |

---

## Quick Guard Verification Commands

```bash
# Count guards in Transport.cpp
grep -c "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp

# Compare with v3-lively
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "#ifdef TRANSCODE"

# List all guard locations
grep -n "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp

# Verify guard balance
echo "ifdef count: $(grep -c '#ifdef TRANSCODE' worker/src/RTC/Transport.cpp)"
echo "endif count: $(grep -c '#endif' worker/src/RTC/Transport.cpp)"
```

---

## Invocation

```
/transcode-features-expert audit
```

Performs complete transcode features audit.
