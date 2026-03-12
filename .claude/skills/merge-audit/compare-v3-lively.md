# Compare v3-lively Skill

## Overview

This skill provides procedural knowledge for comparing the current branch against `origin/v3-lively`. Use this skill when you need to establish what v3-lively looked like and flag any differences as potential regressions.

**Core Principle**: v3-lively is the source of truth. Any difference is a potential regression until proven otherwise. Do not rationalize differences as "improvements" or "unlikely to matter."

**Scope Boundaries:**
- IN SCOPE: Enum values, CLI options, event names, log formats, type exports, initialization code
- OUT OF SCOPE: Feature testing, runtime verification

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `files` | string[] | No | Specific files to compare (default: all Lively-critical files) |
| `focus` | string | No | "enums", "options", "events", "logs", "types", "init" |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `differences` | object[] | Items that differ from v3-lively |
| `matches` | object[] | Items that match v3-lively |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Status Determination

- **PASS**: All counts match exactly AND all enum values match AND all event names match AND all CLI options match
- **REGRESSION**: Any of the following:
  - Log macro count is LOWER in current than v3-lively (R005)
  - Enum value/order changed (R001)
  - CLI option letter changed (R002)
  - Event name changed (R003)
  - Type export removed (R004)
- **FAIL**: Unable to complete comparison (file missing, command error)

**CRITICAL**: A single REGRESSION in any category means overall status is REGRESSION.

## Silent Failure Modes

### Mode 1: Enum Value Changed
**Symptom**: Protocol mismatch, handlers never fire
```bash
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A50 "enum Event"
grep -A50 "enum Event" worker/fbs/notification.fbs
```
**Failure**: Different integer assignments

### Mode 2: CLI Option Letter Changed
**Symptom**: Deployment scripts break silently
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -E "case '[a-zA-Z]':"
grep -E "case '[a-zA-Z]':" worker/src/main.cpp
```
**Failure**: Different letter for same option

### Mode 3: Event Name Changed
**Symptom**: Node.js handlers never called
```bash
git show origin/v3-lively:node/src/Producer.ts | grep "safeEmit\|this.emit"
grep "safeEmit\|this.emit" node/src/Producer.ts
```
**Failure**: Different event name strings

### Mode 4: Log Macro Removed (CRITICAL)
**Symptom**: Debug/diagnostic information lost silently
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_DEV"
grep -c "MS_DEBUG_DEV" worker/src/RTC/Transport.cpp
```
**Expected**: Counts are EQUAL
**Failure**: Current count is LOWER = REGRESSION

### Mode 5: Type Export Removed
**Symptom**: Consumer TypeScript code fails to compile
```bash
git show origin/v3-lively:node/src/index.ts | grep "export"
grep "export" node/src/index.ts
```
**Failure**: Missing export

## Methodology

**IMPORTANT: All steps are MANDATORY. Do not report PASS until every step has been executed and documented.**

### Step 1: Enum Value Verification

```bash
# 1.1 Notification events
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A100 "enum Event" | head -50
grep -A100 "enum Event" worker/fbs/notification.fbs | head -50

# 1.2 Request methods
git show origin/v3-lively:worker/fbs/request.fbs | grep -A100 "enum Method" | head -80
grep -A100 "enum Method" worker/fbs/request.fbs | head -80
```

### Step 2: CLI Option Verification

```bash
# 2.1 Short options
git show origin/v3-lively:worker/src/main.cpp | grep "getopt_long\|case '" | head -30
grep "getopt_long\|case '" worker/src/main.cpp | head -30

# 2.2 Long option names
git show origin/v3-lively:worker/src/main.cpp | grep -A30 "option long_options"
grep -A30 "option long_options" worker/src/main.cpp
```

### Step 3: Event Name Verification

```bash
# 3.1 Producer events
git show origin/v3-lively:node/src/Producer.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Producer.ts | head -10

# 3.2 Transport events
git show origin/v3-lively:node/src/Transport.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Transport.ts | head -10

# 3.3 Worker events
git show origin/v3-lively:node/src/Worker.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Worker.ts | head -10
```

### Step 4: Log Format Verification [MANDATORY - PRIMARY REGRESSION CHECK]

You MUST count EACH macro type separately (not combined):

```bash
# 4.1a MS_DEBUG_TAG_LIVELYAPP count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_TAG_LIVELYAPP"
grep -c "MS_DEBUG_TAG_LIVELYAPP" worker/src/RTC/Transport.cpp

# 4.1b MS_DEBUG_DEV count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_DEBUG_DEV"
grep -c "MS_DEBUG_DEV" worker/src/RTC/Transport.cpp

# 4.1c MS_WARN_DEV count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "MS_WARN_DEV"
grep -c "MS_WARN_DEV" worker/src/RTC/Transport.cpp
```

**CRITICAL**: If ANY count in the current branch is LOWER than v3-lively, this is error R005 (Log macro removed). Report status as **REGRESSION** immediately.

When a count difference is detected, you MUST run this drill-down command:

```bash
# 4.2 Identify specific removed/changed lines
git diff origin/v3-lively -- worker/src/RTC/Transport.cpp | grep -B5 -A5 "MS_DEBUG_DEV"
```

Include the specific removed line in your report.

### Step 5: Type Export Verification

```bash
# 5.1 Index exports
git show origin/v3-lively:node/src/index.ts | grep "^export" | sort
grep "^export" node/src/index.ts | sort

# 5.2 Type definitions
git show origin/v3-lively:node/src/types.ts 2>/dev/null | grep "^export" | sort
grep "^export" node/src/types.ts 2>/dev/null | sort
```

### Step 6: Lively Initialization Verification

```bash
# 6.1 WebRtcTransport constructor
git show origin/v3-lively:worker/src/RTC/WebRtcTransport.cpp | grep -A3 "producerBinLogEnabled"
grep -A3 "producerBinLogEnabled" worker/src/RTC/WebRtcTransport.cpp

# 6.2 Transport constructor
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A5 "consumersBinLog.InitLog\|lastProducerStatsReport"
grep -A5 "consumersBinLog.InitLog\|lastProducerStatsReport" worker/src/RTC/Transport.cpp

# 6.3 Producer constructor
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -A10 "binLog.InitLog"
grep -A10 "binLog.InitLog" worker/src/RTC/Producer.cpp
```

### Step 7: Timer Logic Verification

```bash
# 7.1 OnTimer binlog calls
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A20 "void Transport::OnTimer" | grep "FillBinLogStats"
grep -A50 "void Transport::OnTimer" worker/src/RTC/Transport.cpp | grep "FillBinLogStats"

# 7.2 OnTimer stats emission
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A50 "void Transport::OnTimer" | grep "EmitProducerStats"
grep -A50 "void Transport::OnTimer" worker/src/RTC/Transport.cpp | grep "EmitProducerStats"
```

## Pre-Completion Checkpoint

**Before writing the final status, verify:**
- [ ] All 7 methodology steps executed
- [ ] All table rows in output template populated
- [ ] All count comparisons show EXACT matches (not just "close")
- [ ] If any count is lower, REGRESSION status assigned and specific line identified
- [ ] If any enum/option/event differs, REGRESSION status assigned

## Quick Diff Commands

```bash
# Full file diff
git diff origin/v3-lively -- <filepath>

# Lively-specific lines only
git diff origin/v3-lively -- <filepath> | grep -E "Lively|RND-|binLog|producerStats|MS_DEBUG_DEV"

# Count differences
git diff origin/v3-lively --stat -- <filepath>

# Show only changed function signatures
git diff origin/v3-lively -- <filepath> | grep "^[-+].*(" | head -20
```

## Error Handling

| Type | Code | Trigger Condition | Detection Step | Recovery |
|------|------|-------------------|----------------|----------|
| REGRESSION | R001 | Enum value/order changed | Step 1 | Restore v3-lively enum order |
| REGRESSION | R002 | CLI option letter changed | Step 2 | Restore v3-lively option |
| REGRESSION | R003 | Event name changed | Step 3 | Restore v3-lively event name |
| REGRESSION | R004 | Type export removed | Step 5 | Restore export |
| REGRESSION | R005 | Log macro count LOWER than v3-lively | Step 4 | Restore log macro from v3-lively |

## Examples

### Example 1: Clean Comparison

**Output:**
```markdown
## v3-lively Comparison Results

### Enum Values
| Enum | Location | Match |
|------|----------|-------|
| Event (notification.fbs) | worker/fbs/ | PASS |
| Method (request.fbs) | worker/fbs/ | PASS |

### CLI Options
| Option | v3-lively | Current | Match |
|--------|-----------|---------|-------|
| -b (binStatsDisabled) | 'b' | 'b' | PASS |

### Event Names
| Event | Location | Match |
|-------|----------|-------|
| producerstats | Producer.ts | PASS |

### Log Macro Counts
| Macro | File | v3-lively | Current | Match |
|-------|------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | Transport.cpp | 34 | 34 | PASS |
| MS_DEBUG_DEV | Transport.cpp | 9 | 9 | PASS |
| MS_WARN_DEV | Transport.cpp | 0 | 0 | PASS |

### Status: PASS
```

### Example 2: Regression Detected (Log Macro Removed)

**Scenario:** One MS_DEBUG_DEV log line was removed during upstream merge.

**Output:**
```markdown
## v3-lively Comparison Results

### Enum Values
| Enum | Location | Match |
|------|----------|-------|
| Event (notification.fbs) | worker/fbs/ | PASS |
| Method (request.fbs) | worker/fbs/ | PASS |

### CLI Options
| Option | v3-lively | Current | Match |
|--------|-----------|---------|-------|
| -b (binStatsDisabled) | 'b' | 'b' | PASS |

### Event Names
| Event | Location | Match |
|-------|----------|-------|
| producerstats | Producer.ts | PASS |

### Log Macro Counts
| Macro | File | v3-lively | Current | Match |
|-------|------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | Transport.cpp | 34 | 34 | PASS |
| MS_DEBUG_DEV | Transport.cpp | 9 | 8 | **REGRESSION** |
| MS_WARN_DEV | Transport.cpp | 0 | 0 | PASS |

### Regression Details
- **Error Code**: R005 (Log macro removed)
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
- Maximum 40 git/grep commands per comparison

### Prohibited Actions
- NEVER modify source files
- NEVER revert changes without approval
- NEVER report PASS if any count comparison shows current < v3-lively

## Output Template

```markdown
## v3-lively Comparison Results

### Enum Values
| Enum | Match |
|------|-------|
| Event (notification.fbs) | PASS/REGRESSION |
| Method (request.fbs) | PASS/REGRESSION |

### CLI Options
| Option | v3-lively | Current | Match |
|--------|-----------|---------|-------|
| {option} | {letter} | {letter} | PASS/REGRESSION |

### Event Names
| File | Events Match |
|------|--------------|
| Producer.ts | PASS/REGRESSION |
| Transport.ts | PASS/REGRESSION |
| Worker.ts | PASS/REGRESSION |

### Log Macro Counts
| Macro | File | v3-lively | Current | Match |
|-------|------|-----------|---------|-------|
| MS_DEBUG_TAG_LIVELYAPP | Transport.cpp | {n} | {n} | PASS/REGRESSION |
| MS_DEBUG_DEV | Transport.cpp | {n} | {n} | PASS/REGRESSION |
| MS_WARN_DEV | Transport.cpp | {n} | {n} | PASS/REGRESSION |

### Initialization Code
| Component | Match |
|-----------|-------|
| producerBinLogEnabled | PASS/REGRESSION |
| consumersBinLog.InitLog | PASS/REGRESSION |
| lastProducerStatsReport | PASS/REGRESSION |
| binLog.InitLog (Producer) | PASS/REGRESSION |

### Regression Details (if any)
- **Error Code**: {code}
- **File**: {filepath}
- **Removed/Changed**: {description}

### Status: {PASS/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-transcode-guards` | Comparing guard placement |
| `audit-fbs-schema` | Comparing FBS schemas |
| `audit-textlogs` | Focused log macro verification |
