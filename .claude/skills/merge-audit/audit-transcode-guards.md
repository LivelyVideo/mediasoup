# Audit TRANSCODE Guards Skill

## Overview

This skill provides procedural knowledge for auditing `#ifdef TRANSCODE` guards. Use this skill when you need to verify that TRANSCODE guards match v3-lively exactly. Incorrect guards are the #1 cause of silent feature breakage.

**Scope Boundaries:**
- IN SCOPE: Guard counts, guard locations, what code is guarded vs unguarded
- OUT OF SCOPE: Feature functionality (use feature-specific skills)

## Critical Understanding

**The production build uses NO TRANSCODE flag.** This means:
- Code inside `#ifdef TRANSCODE` blocks is **excluded** from production
- Code outside guards **runs in production**
- Adding a guard to code that wasn't guarded = **disables it in production**
- Removing a guard from code that was guarded = **enables it in production**

Both directions can cause regressions.

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `files` | string[] | No | Specific files to audit (default: all Lively-critical files) |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `guard_counts` | object | v3-lively vs current guard counts per file |
| `guard_violations` | object[] | Guards that should/shouldn't be present |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Silent Failure Modes

### Mode 1: Guard Added to Timer Creation
**Symptom**: No binlog files in production
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 2: Guard Added to FillBinLogStats
**Symptom**: Binlog files exist but are empty
```bash
grep -B3 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 3: Guard Added to InitLog
**Symptom**: Binlogs never opened
```bash
grep -B3 "binLog.InitLog\|consumersBinLog.InitLog" worker/src/RTC/Transport.cpp worker/src/RTC/Producer.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 4: Guard Removed from ShmTransport Include
**Symptom**: Compilation error in notranscode build
```bash
grep -B1 "include.*ShmTransport" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `1` (guard present)
**Failure**: `0` (guard missing)

### Mode 5: Guard Added to EmitProducerStats
**Symptom**: Producer stats event never fires
```bash
grep -B3 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

### Mode 6: Guard Added to Consumer Binlog Record
**Symptom**: Consumer binlogs missing
```bash
grep -B2 "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
**Failure**: `1` or more

## Methodology

### Step 1: Guard Count Comparison

```bash
# Transport.cpp
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "#ifdef TRANSCODE"
grep -c "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp

# Router.cpp
git show origin/v3-lively:worker/src/RTC/Router.cpp | grep -c "#ifdef TRANSCODE"
grep -c "#ifdef TRANSCODE" worker/src/RTC/Router.cpp

# Producer.cpp
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -c "#ifdef TRANSCODE"
grep -c "#ifdef TRANSCODE" worker/src/RTC/Producer.cpp
```

### Step 2: ShmTransport Guards (SHOULD be guarded)

```bash
# 2.1 Include guard
grep -B1 -A1 'include.*ShmTransport\|include.*ShmConsumer' worker/src/RTC/Transport.cpp

# 2.2 ShmConsumer creation guard
grep -B5 "new RTC::ShmConsumer" worker/src/RTC/Transport.cpp

# 2.3 dynamic_cast guards
grep -B3 "dynamic_cast.*ShmTransport" worker/src/RTC/Transport.cpp

# 2.4 Router ShmTransport handler
grep -B3 -A3 "ROUTER_CREATE_SHMTRANSPORT" worker/src/RTC/Router.cpp
```

### Step 3: Binlog Code (should NOT be guarded)

```bash
# 3.1 Timer creation
grep -B5 "binLogTimer = new" worker/src/RTC/Transport.cpp

# 3.2 Timer start
grep -B5 "binLogTimer->Start" worker/src/RTC/Transport.cpp

# 3.3 consumersBinLog.InitLog
grep -B5 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp

# 3.4 FillBinLogStats in OnTimer
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp

# 3.5 Producer binLog.InitLog
grep -B5 "binLog.InitLog" worker/src/RTC/Producer.cpp
```

### Step 4: Consumer Binlog Code (should NOT be guarded)

```bash
# 4.1 SimpleConsumer
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp | grep "TRANSCODE"

# 4.2 SimulcastConsumer
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SimulcastConsumer.cpp | grep "TRANSCODE"

# 4.3 SvcConsumer
grep -B3 "rtpStreamBinLogRecord" worker/src/RTC/SvcConsumer.cpp | grep "TRANSCODE"

# 4.4 PipeConsumer
grep -B3 "rtpStreamBinLogRecords" worker/src/RTC/PipeConsumer.cpp | grep "TRANSCODE"
```

### Step 5: Producer Stats Code (should NOT be guarded)

```bash
# 5.1 lastProducerStatsReport init
grep -B5 "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep "TRANSCODE"

# 5.2 EmitProducerStats call
grep -B5 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep "TRANSCODE"

# 5.3 EmitProducerStats implementation
grep -B5 "void Producer::EmitProducerStats" worker/src/RTC/Producer.cpp | grep "TRANSCODE"
```

### Step 6: Guard Location Verification

```bash
# List all guards with line numbers
grep -n "#ifdef TRANSCODE\|#endif" worker/src/RTC/Transport.cpp | head -30

# Compare guard positions with v3-lively
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "#ifdef TRANSCODE"
grep -n "#ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| REGRESSION | R001 | Guard added to production code | Remove the guard |
| REGRESSION | R002 | Guard removed from transcode code | Restore the guard |
| MISMATCH | M001 | Guard count differs from v3-lively | Investigate each guard |

## Examples

### Example 1: Clean Audit

**Output:**
```markdown
## TRANSCODE Guard Audit Results

### Guard Counts
| File | v3-lively | Current | Match |
|------|-----------|---------|-------|
| Transport.cpp | 4 | 4 | PASS |
| Router.cpp | 2 | 2 | PASS |
| Producer.cpp | 0 | 0 | PASS |

### Should Be Guarded
| Code | Guarded |
|------|---------|
| ShmTransport include | YES |
| ShmConsumer creation | YES |
| dynamic_cast<ShmTransport*> | YES |

### Should NOT Be Guarded
| Code | Unguarded |
|------|-----------|
| binLogTimer creation | YES |
| FillBinLogStats | YES |
| InitLog calls | YES |
| Consumer binlog records | YES |

### Status: PASS
```

### Example 2: Regression Found

**Output:**
```markdown
## TRANSCODE Guard Audit Results

### Guard Counts
| File | v3-lively | Current | Match |
|------|-----------|---------|-------|
| Transport.cpp | 4 | 5 | REGRESSION |

### Regression Details
- **File**: worker/src/RTC/Transport.cpp
- **Issue**: New `#ifdef TRANSCODE` guard at line 1234
- **Code Affected**: `binLogTimer = new TimerHandle(this);`
- **Impact**: Timer not created in production build = no binlog files

### Status: REGRESSION
```

## Constraints

### Operational Limits
- Maximum 30 grep commands per audit

### Prohibited Actions
- NEVER modify source files
- NEVER add or remove guards

## Output Template

```markdown
## TRANSCODE Guard Audit Results

### Guard Counts
| File | v3-lively | Current | Match |
|------|-----------|---------|-------|
| Transport.cpp | {n} | {n} | PASS/FAIL |
| Router.cpp | {n} | {n} | PASS/FAIL |
| Producer.cpp | {n} | {n} | PASS/FAIL |

### Should Be Guarded (Transcode-Only Code)
| Code | Status |
|------|--------|
| ShmTransport include | GUARDED/MISSING |
| ShmConsumer creation | GUARDED/MISSING |
| dynamic_cast<ShmTransport*> | GUARDED/MISSING |
| ROUTER_CREATE_SHMTRANSPORT | GUARDED/MISSING |

### Should NOT Be Guarded (Production Code)
| Code | Status |
|------|--------|
| binLogTimer creation | OK/GUARDED |
| binLogTimer->Start | OK/GUARDED |
| consumersBinLog.InitLog | OK/GUARDED |
| FillBinLogStats | OK/GUARDED |
| binLog.InitLog (Producer) | OK/GUARDED |
| rtpStreamBinLogRecord | OK/GUARDED |
| EmitProducerStats | OK/GUARDED |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `compare-v3-lively` | Detailed line-by-line comparison |
| `audit-binlogs` | Verifying binlog feature |
