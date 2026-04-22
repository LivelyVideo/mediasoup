# Audit Textlogs Skill

## Overview

This skill provides procedural knowledge for auditing Lively's text logging system. Use this skill when you need to verify per-worker log files, developer log levels, and Lively-specific log macros.

**Scope Boundaries:**
- IN SCOPE: Log macros (MS_DEBUG_DEV, MS_WARN_DEV, MS_DEBUG_TAG_LIVELYAPP), LogDevLevel enum, MSLOG_OPEN/ROTATE handlers, LOGGER_WRITE_FAILED notification
- OUT OF SCOPE: Binary logging (use `audit-binlogs`), FBS schema (use `audit-fbs-schema`)

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `scope` | string | No | "full", "macros", "handlers", "fbs" (default: "full") |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `silent_failure_checks` | object[] | Results of silent failure mode detection |
| `checklist_results` | object[] | Results of mechanical checklist |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Status Determination

- **PASS**: All counts match exactly AND all handlers present AND all enum values correct
- **REGRESSION**: Any log macro count is LOWER in current than v3-lively (code R002), OR enum values changed (code R001)
- **FAIL**: Unable to complete audit (file missing, command error)

**CRITICAL**: A single REGRESSION in any check means overall status is REGRESSION.

## Silent Failure Modes

### Mode 1: LogDevLevel Enum Values Changed
**Symptom**: Wrong log level filtering
```bash
grep -A5 "enum.*LogDevLevel\|LOG_DEV_NONE\|LOG_DEV_WARN\|LOG_DEV_DEBUG" worker/include/Settings.hpp
```
**Expected**: `LOG_DEV_NONE = 0`, `LOG_DEV_WARN = 2`, `LOG_DEV_DEBUG = 3`
**Failure**: Different values

### Mode 2: Log Macro Definitions Changed
**Symptom**: Logs don't appear at expected level
```bash
grep -A3 "define MS_DEBUG_DEV\|define MS_WARN_DEV" worker/include/Logger.hpp
```
**Expected**: Checks `logDevLevel >= LOG_DEV_DEBUG` / `LOG_DEV_WARN`
**Failure**: Different condition

### Mode 3: MSLOG_OPEN Handler Missing
**Symptom**: Log files never created
```bash
grep "WORKER_MSLOG_OPEN\|MSLOG_OPEN" node/src/Worker.ts
```
**Expected**: Handler exists with channel.request call
**Failure**: No match

### Mode 4: LOGGER_WRITE_FAILED Not Handled
**Symptom**: Write failures go unnoticed
```bash
grep "LOGGER_WRITE_FAILED" node/src/Worker.ts
```
**Expected**: Case in notification handler
**Failure**: No match

### Mode 5: Log Macro Removed (CRITICAL)
**Symptom**: Debug/diagnostic information lost, no compilation error
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_DEV"
grep -c "MS_DEBUG_DEV" worker/src/RTC/Transport.cpp
```
**Expected**: Counts are EQUAL
**Failure**: Current count is LOWER than v3-lively = REGRESSION

## Methodology

**IMPORTANT: All steps are MANDATORY. Do not report PASS until every step has been executed and documented.**

### Step 1: Silent Failure Mode Checks

```bash
# Mode 1: LogDevLevel enum
grep -A5 "enum.*LogDevLevel" worker/include/Settings.hpp

# Mode 2: Log macro definitions
grep -A3 "define MS_DEBUG_DEV" worker/include/Logger.hpp

# Mode 3: MSLOG_OPEN handler
grep "WORKER_MSLOG_OPEN\|MSLOG_OPEN" node/src/Worker.ts

# Mode 4: LOGGER_WRITE_FAILED handler
grep "LOGGER_WRITE_FAILED" node/src/Worker.ts
```

### Step 2: Logger.hpp Checklist

```bash
# 2.1 Log macros defined
grep -n "define MS_DEBUG_DEV\|define MS_WARN_DEV\|define MS_DEBUG_TAG_LIVELYAPP" worker/include/Logger.hpp

# 2.2 LogDevLevel check in macros
grep -A2 "MS_DEBUG_DEV\|MS_WARN_DEV" worker/include/Logger.hpp | grep "logDevLevel"

# 2.3 logTraceEnabled used
grep "logTraceEnabled" worker/include/Logger.hpp
```

### Step 3: Settings.hpp Checklist

```bash
# 3.1 LogDevLevel member
grep "logDevLevel" worker/include/Settings.hpp

# 3.2 logTraceEnabled member
grep "logTraceEnabled" worker/include/Settings.hpp
```

### Step 4: Worker.ts Checklist

```bash
# 4.1 MSLOG_OPEN handler
grep -B2 -A10 "MSLOG_OPEN\|logOpen" node/src/Worker.ts

# 4.2 MSLOG_ROTATE handler
grep "MSLOG_ROTATE" node/src/Worker.ts

# 4.3 LOGGER_WRITE_FAILED handler
grep -B2 -A5 "LOGGER_WRITE_FAILED" node/src/Worker.ts
```

### Step 5: log.fbs Checklist

```bash
# 5.1 Schema tables
grep "table" worker/fbs/log.fbs
```

### Step 6: notification.fbs Checklist

```bash
# 6.1 LOGGER_WRITE_FAILED enum
grep "LOGGER_WRITE_FAILED" worker/fbs/notification.fbs
```

### Step 7: v3-lively Log Macro Comparison [MANDATORY - PRIMARY REGRESSION CHECK]

You MUST run ALL of the following commands and compare the counts for EACH macro type separately:

```bash
# 7.1 MS_DEBUG_TAG_LIVELYAPP count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_TAG_LIVELYAPP"
grep -c "MS_DEBUG_TAG_LIVELYAPP" worker/src/RTC/Transport.cpp

# 7.2 MS_DEBUG_DEV count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_DEV"
grep -c "MS_DEBUG_DEV" worker/src/RTC/Transport.cpp

# 7.3 MS_WARN_DEV count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_WARN_DEV"
grep -c "MS_WARN_DEV" worker/src/RTC/Transport.cpp
```

**CRITICAL**: If ANY count in the current branch is LOWER than v3-lively, this is error R002 (Log macro removed). Report status as **REGRESSION** immediately.

When a count difference is detected, you MUST run this drill-down command to identify the specific removed line:

```bash
git diff origin/v3-lively -- worker/src/RTC/Transport.cpp | grep -B3 -A3 "MS_DEBUG_DEV"
```

Include the specific removed line in your report.

## Pre-Completion Checkpoint

**Before writing the final status, verify:**
- [ ] All 7 methodology steps executed
- [ ] All table rows in output template populated
- [ ] All count comparisons show EXACT matches (not just "close")
- [ ] If any count is lower, REGRESSION status assigned and specific line identified

## Error Handling

| Type | Code | Trigger Condition | Detection Step | Recovery |
|------|------|-------------------|----------------|----------|
| REGRESSION | R001 | Enum values changed | Step 1 | Restore v3-lively enum values |
| REGRESSION | R002 | Log macro count LOWER than v3-lively | Step 7 | Restore log macro from v3-lively |
| MISSING | M001 | Handler missing | Step 4 | Add MSLOG handler to Worker.ts |

## Examples

### Example 1: Clean Audit

**Output:**
```markdown
## Text Logging Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. LogDevLevel enum | Values correct | PASS |
| 2. Macro definitions | Present | PASS |
| 3. MSLOG_OPEN | Handler present | PASS |
| 4. LOGGER_WRITE_FAILED | Handler present | PASS |

### Log Macro Counts
| Macro | v3-lively | Current | Match |
|-------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | 34 | 34 | PASS |
| MS_DEBUG_DEV | 9 | 9 | PASS |
| MS_WARN_DEV | 0 | 0 | PASS |

### Status: PASS
```

### Example 2: Regression Detected (Log Macro Removed)

**Scenario:** One MS_DEBUG_DEV log line was removed during upstream merge.

**Output:**
```markdown
## Text Logging Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. LogDevLevel enum | Values correct | PASS |
| 2. Macro definitions | Present | PASS |
| 3. MSLOG_OPEN | Handler present | PASS |
| 4. LOGGER_WRITE_FAILED | Handler present | PASS |

### Log Macro Counts
| Macro | v3-lively | Current | Match |
|-------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | 34 | 34 | PASS |
| MS_DEBUG_DEV | 9 | 8 | **REGRESSION** |
| MS_WARN_DEV | 0 | 0 | PASS |

### Regression Details
- **Error Code**: R002 (Log macro removed)
- **File**: worker/src/RTC/Transport.cpp
- **Macro Type**: MS_DEBUG_DEV
- **v3-lively count**: 9
- **Current count**: 8
- **Removed Line**:
```cpp
MS_DEBUG_DEV("recvRtxTransmission.GetPacketCount()=%zu", this->recvRtxTransmission.GetPacketCount());
```
- **Location**: Inside `ReceiveRtpPacketResult::RETRANSMISSION` case

### Status: REGRESSION
```

## Constraints

### Operational Limits
- Maximum 20 grep commands per audit

### Prohibited Actions
- NEVER modify source files
- NEVER report PASS if any count comparison shows current < v3-lively

## Output Template

```markdown
## Text Logging Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. LogDevLevel enum | {observation} | PASS/FAIL |
| 2. Macro definitions | {observation} | PASS/FAIL |
| 3. MSLOG_OPEN | {observation} | PASS/FAIL |
| 4. LOGGER_WRITE_FAILED | {observation} | PASS/FAIL |

### Log Macro Counts
| Macro | v3-lively | Current | Match |
|-------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | {n} | {n} | PASS/REGRESSION |
| MS_DEBUG_DEV | {n} | {n} | PASS/REGRESSION |
| MS_WARN_DEV | {n} | {n} | PASS/REGRESSION |

### Regression Details (if any)
- **Error Code**: {code}
- **File**: {filepath}
- **Removed Line**: {line content}

### Status: {PASS/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-fbs-schema` | Verifying log.fbs schema completeness |
| `compare-v3-lively` | Detailed comparison of log formats |
