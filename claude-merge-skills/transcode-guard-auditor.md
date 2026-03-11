# TRANSCODE Guard Auditor

**Last Updated**: 2026-03-11

You are auditing `#ifdef TRANSCODE` guards to ensure they match `origin/v3-lively` exactly. Incorrect guards are the #1 cause of silent feature breakage.

## Critical Understanding

**The production build uses NO TRANSCODE flag.** This means:
- Code inside `#ifdef TRANSCODE` blocks is **excluded** from production
- Code outside guards **runs in production**
- Adding a guard to code that wasn't guarded = **disables it in production**
- Removing a guard from code that was guarded = **enables it in production**

Both directions can cause regressions.

---

## Silent Failure Modes

### Mode 1: Guard Added to Timer Creation
**Symptom**: No binlog files appear in production
**Cause**: `#ifdef TRANSCODE` added around timer creation
**Detection**:
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 2: Guard Added to FillBinLogStats
**Symptom**: Binlog files exist but are empty
**Cause**: `#ifdef TRANSCODE` added around FillBinLogStats calls
**Detection**:
```bash
grep -B3 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 3: Guard Added to InitLog
**Symptom**: Binlogs never opened, no files created
**Cause**: `#ifdef TRANSCODE` added around InitLog calls
**Detection**:
```bash
grep -B3 "binLog.InitLog\|consumersBinLog.InitLog" worker/src/RTC/Transport.cpp worker/src/RTC/Producer.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 4: Guard Removed from ShmTransport Include
**Symptom**: Compilation error in notranscode build
**Cause**: `#ifdef TRANSCODE` removed from ShmTransport include
**Detection**:
```bash
grep -B1 "include.*ShmTransport" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `1` (guard present)
**Failure**: `0` (guard missing)

### Mode 5: Guard Added to EmitProducerStats
**Symptom**: Producer stats event never fires
**Cause**: `#ifdef TRANSCODE` added around stats emission
**Detection**:
```bash
grep -B3 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 6: Guard Added to Consumer Binlog Record
**Symptom**: Consumer binlogs missing
**Cause**: `#ifdef TRANSCODE` added around rtpStreamBinLogRecord
**Detection**:
```bash
grep -B2 "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

---

## Mechanical Audit Checklist

### 1. Guard Count Comparison

#### 1.1 Transport.cpp Guards
```bash
# v3-lively guard count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "#ifdef TRANSCODE"

# Current guard count
grep -c "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```
**Expected**: Same count
- [ ] Guard counts match

#### 1.2 Router.cpp Guards
```bash
git show origin/v3-lively:worker/src/RTC/Router.cpp | grep -c "#ifdef TRANSCODE"
grep -c "#ifdef TRANSCODE" worker/src/RTC/Router.cpp
```
- [ ] Guard counts match

#### 1.3 Producer.cpp Guards
```bash
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -c "#ifdef TRANSCODE"
grep -c "#ifdef TRANSCODE" worker/src/RTC/Producer.cpp
```
- [ ] Guard counts match

---

### 2. ShmTransport Guards (SHOULD be guarded)

#### 2.1 Include Guard
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
- [ ] ShmTransport includes guarded

#### 2.2 ShmConsumer Creation Guard
```bash
grep -B5 "new RTC::ShmConsumer" worker/src/RTC/Transport.cpp
```
**Expected**: Inside `#ifdef TRANSCODE` block
- [ ] ShmConsumer creation guarded

#### 2.3 dynamic_cast Guards
```bash
grep -n "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp
```
For each line found:
```bash
grep -B3 "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp
```
**Expected**: Each has `#ifdef TRANSCODE` above
- [ ] All dynamic_cast<ShmTransport*> guarded

#### 2.4 Router ShmTransport Handler
```bash
grep -B3 -A3 "ROUTER_CREATE_SHMTRANSPORT" worker/src/RTC/Router.cpp
```
**Expected**: Inside `#ifdef TRANSCODE` block
- [ ] ShmTransport creation handler guarded

---

### 3. Binlog Code (should NOT be guarded)

#### 3.1 Timer Creation
```bash
grep -B5 "binLogTimer = new" worker/src/RTC/Transport.cpp
```
**Expected**: No `#ifdef TRANSCODE` in output
- [ ] Timer creation NOT guarded

#### 3.2 Timer Start
```bash
grep -B5 "binLogTimer->Start" worker/src/RTC/Transport.cpp
```
**Expected**: No `#ifdef TRANSCODE` in output
- [ ] Timer start NOT guarded

#### 3.3 consumersBinLog.InitLog
```bash
grep -B5 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp
```
**Expected**: No `#ifdef TRANSCODE` in output
- [ ] Consumer binlog init NOT guarded

#### 3.4 FillBinLogStats in OnTimer
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp
```
**Expected**: No `#ifdef TRANSCODE` in output
- [ ] FillBinLogStats NOT guarded

#### 3.5 Producer binLog.InitLog
```bash
grep -B5 "binLog.InitLog" worker/src/RTC/Producer.cpp
```
**Expected**: No `#ifdef TRANSCODE` in output
- [ ] Producer binlog init NOT guarded

---

### 4. Consumer Binlog Code (should NOT be guarded)

#### 4.1 SimpleConsumer
```bash
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] SimpleConsumer binlog NOT guarded

#### 4.2 SimulcastConsumer
```bash
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SimulcastConsumer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] SimulcastConsumer binlog NOT guarded

#### 4.3 SvcConsumer
```bash
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SvcConsumer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] SvcConsumer binlog NOT guarded

#### 4.4 PipeConsumer
```bash
grep -B3 "rtpStreamBinLogRecords" worker/src/RTC/PipeConsumer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] PipeConsumer binlog NOT guarded

---

### 5. Producer Stats Code (should NOT be guarded)

#### 5.1 lastProducerStatsReport Init
```bash
grep -B5 "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] Producer stats init NOT guarded

#### 5.2 EmitProducerStats Call
```bash
grep -B5 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] EmitProducerStats call NOT guarded

#### 5.3 EmitProducerStats Implementation
```bash
grep -B5 "void Producer::EmitProducerStats" worker/src/RTC/Producer.cpp | grep "TRANSCODE"
```
**Expected**: No output
- [ ] EmitProducerStats implementation NOT guarded

---

### 6. Guard Location Verification

#### 6.1 List All Guards with Line Numbers
```bash
grep -n "#ifdef TRANSCODE\|#endif" worker/src/RTC/Transport.cpp | head -30
```
For each `#ifdef`, verify the corresponding `#endif` and what code is enclosed.
- [ ] All guard pairs make sense
- [ ] No unexpected guards

#### 6.2 Compare Guard Positions
```bash
# v3-lively guard locations
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "#ifdef TRANSCODE"

# Current guard locations
grep -n "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```
**Verify**: Guards protect the same logical code blocks
- [ ] Guard positions match v3-lively intent

---

## Quick Verification Commands

```bash
# Count all guards in a file
grep -c "#ifdef TRANSCODE" <filepath>

# Show what each guard protects
grep -A5 "#ifdef TRANSCODE" <filepath>

# Find unmatched guards (count should be equal)
echo "ifdef count: $(grep -c '#ifdef TRANSCODE' <filepath>)"
echo "endif count (may include others): $(grep -c '#endif' <filepath>)"

# Compare with v3-lively
diff <(git show origin/v3-lively:<filepath> | grep -n "#ifdef TRANSCODE") <(grep -n "#ifdef TRANSCODE" <filepath>)
```

---

## Common Regression Patterns

### Pattern 1: Upstream Merge Removes Guards

Upstream doesn't have TRANSCODE guards. A merge may:
- Delete guard lines entirely
- Replace guarded code with unguarded upstream version

### Pattern 2: Copy-Paste Without Guards

When adding similar code to multiple files, guards may be forgotten.

### Pattern 3: Refactoring Moves Code Outside Guards

Code movement during refactoring may place previously-guarded code outside guards.

---

## Invocation

```
/transcode-guard-auditor <filepath>
```

Or for comprehensive audit:
```
/transcode-guard-auditor --all-lively-files
```

Performs complete TRANSCODE guard verification.
