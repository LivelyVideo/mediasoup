# Audit Binlogs Skill

## Overview

This skill provides procedural knowledge for auditing Lively's binary stats logging system. Use this skill when you need to verify that producer binlogs (`ms_p_*`) and consumer binlogs (`ms_c_*`) are being created correctly.

**Scope Boundaries:**
- IN SCOPE: Timer creation, InitLog calls, FillBinLogStats calls, producerBinLogEnabled flag, consumer binlog records
- OUT OF SCOPE: Periodic stats (use `audit-stats`), text logging (use `audit-textlogs`), FBS schema (use `audit-fbs-schema`)

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `scope` | string | No | "full", "transport", "producer", "consumer" (default: "full") |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `silent_failure_checks` | object[] | Results of silent failure mode detection |
| `checklist_results` | object[] | Results of mechanical checklist |
| `regressions` | object[] | Any identified regressions |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Activation Chain Reference

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

## Silent Failure Modes

### Mode 1: Timer Never Created
**Symptom**: No binlog files appear, no errors
**Cause**: TRANSCODE guard around timer creation
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0` (no guard)
**Failure**: `1` or more

### Mode 2: InitLog Called with Empty CallId
**Symptom**: Files not created, no error logged
```bash
grep -B5 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp | grep "callId"
```
**Expected**: `std::string const callId = this->lively.callId;`
**Failure**: `callId = ""` or missing line

### Mode 3: FillBinLogStats Never Called
**Symptom**: Files created but empty/stale
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 4: producerBinLogEnabled Not Set
**Symptom**: Producer binlogs not created (consumer binlogs work)
```bash
grep -c "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
```
**Expected**: `2` (both constructors)
**Failure**: `0` or `1`

### Mode 5: Consumer Binlog Record Not Initialized
**Symptom**: Consumer binlogs missing for specific consumer type
```bash
grep -c "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp
```
**Expected**: `3` or more
**Failure**: `0` or only cleanup

## Methodology

### Step 1: Silent Failure Mode Checks

Run all silent failure mode detection commands:

```bash
# Mode 1: Timer guard check
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE" || echo "0"

# Mode 3: FillBinLogStats guard check
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE" || echo "0"

# Mode 4: producerBinLogEnabled count
grep -c "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp

# Mode 5: Consumer binlog records
grep -c "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp
grep -c "rtpStreamBinLogRecord" worker/src/RTC/SimulcastConsumer.cpp
grep -c "rtpStreamBinLogRecord" worker/src/RTC/SvcConsumer.cpp
grep -c "rtpStreamBinLogRecords" worker/src/RTC/PipeConsumer.cpp
```

### Step 2: Transport.cpp Checklist

```bash
# 1.1 Timer creation (no guard)
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp

# 1.2 Timer start
grep -n "binLogTimer->Start" worker/src/RTC/Transport.cpp

# 1.3 Timer cleanup
grep -n "binLogTimer" worker/src/RTC/Transport.cpp | grep -E "delete|Stop|Close"

# 1.4 FillBinLogStats call (no guard)
grep -B3 "FillBinLogStats" worker/src/RTC/Transport.cpp | head -15

# 1.5 consumersBinLog.InitLog
grep -A10 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp
```

### Step 3: WebRtcTransport.cpp Checklist

```bash
# 2.1 producerBinLogEnabled set in both constructors
grep -n "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
```

### Step 4: Producer.cpp Checklist

```bash
# 3.1 binLog.InitLog call
grep -B5 -A10 "binLog.InitLog" worker/src/RTC/Producer.cpp

# 3.2 Variable sources
grep -E "userId =|callId =|clientReferrer =" worker/src/RTC/Producer.cpp | head -10
```

### Step 5: v3-lively Comparison

```bash
# Compare guard counts
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
grep -c "ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| REGRESSION | R001 | Timer guard added | Remove TRANSCODE guard from timer creation |
| REGRESSION | R002 | FillBinLogStats guard added | Remove TRANSCODE guard from FillBinLogStats |
| REGRESSION | R003 | producerBinLogEnabled not set | Add assignment in WebRtcTransport constructors |
| MISSING | M001 | InitLog not called | Add binLog.InitLog() call in constructor |

## Examples

### Example 1: Clean Audit

**Scenario:** All binlog checks pass.

**Output:**
```markdown
## Binlog Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. Timer Guard | grep -c = 0 | PASS |
| 3. FillBinLogStats Guard | grep -c = 0 | PASS |
| 4. producerBinLogEnabled | grep -c = 2 | PASS |
| 5. Consumer Records | All >= 3 | PASS |

### Status: PASS
```

### Example 2: Regression Found

**Scenario:** TRANSCODE guard added to timer creation.

**Output:**
```markdown
## Binlog Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. Timer Guard | grep -c = 1 | REGRESSION |

### Regression Details
- **File**: worker/src/RTC/Transport.cpp
- **Issue**: `#ifdef TRANSCODE` guard added around `binLogTimer = new`
- **Impact**: No binlog files in production (notranscode build)
- **Fix**: Remove the guard

### Status: REGRESSION
```

## Constraints

### Operational Limits
- Maximum 20 grep commands per audit
- Timeout after 60 seconds

### Prohibited Actions
- NEVER modify source files
- NEVER run build commands
- NEVER execute the worker binary

## Output Template

```markdown
## Binlog Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. Timer Guard | {command} = {value} | PASS/FAIL |
| 2. CallId Empty | {observation} | PASS/FAIL |
| 3. FillBinLogStats Guard | {command} = {value} | PASS/FAIL |
| 4. producerBinLogEnabled | {command} = {value} | PASS/FAIL |
| 5. Consumer Records | {counts} | PASS/FAIL |

### Activation Chain Verification
| Step | Component | Present | Correct |
|------|-----------|---------|---------|
| 1 | WebRtcTransport sets flag | [ ] | [ ] |
| 2 | Transport.InitLog() | [ ] | [ ] |
| 3 | Producer.InitLog() | [ ] | [ ] |
| 4 | OnTimer.FillBinLogStats | [ ] | [ ] |

### v3-lively Comparison
| File | v3-lively | Current | Match |
|------|-----------|---------|-------|
| Transport.cpp guards | {n} | {n} | PASS/FAIL |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-stats` | Verifying periodic producer stats |
| `audit-transcode-guards` | Comprehensive guard audit |
| `trace-data-flow` | Verifying field values in binlog filenames |
