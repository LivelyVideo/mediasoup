# v3-lively Comparator

**Last Updated**: 2026-03-11

You are comparing the current branch against `origin/v3-lively` to identify regressions. Your job is to establish what v3-lively looked like and flag any differences.

## Core Principle

**v3-lively is the source of truth.** Any difference from v3-lively is a potential regression unless explicitly approved. Do not rationalize differences as "improvements" or "unlikely to matter."

---

## Silent Failure Modes

### Mode 1: Enum Value Changed
**Symptom**: Protocol mismatch, handlers never fire
**Cause**: Enum integer assignment changed
**Detection**:
```bash
# Compare enum values
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A50 "enum Event"
grep -A50 "enum Event" worker/fbs/notification.fbs
```
**Failure**: Different integer assignments

### Mode 2: CLI Option Letter Changed
**Symptom**: Deployment scripts break silently
**Cause**: Short option letter reassigned
**Detection**:
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -E "case '[a-zA-Z]':"
grep -E "case '[a-zA-Z]':" worker/src/main.cpp
```
**Failure**: Different letter for same option

### Mode 3: Event Name Changed
**Symptom**: Node.js handlers never called
**Cause**: Event string literal changed
**Detection**:
```bash
git show origin/v3-lively:node/src/Producer.ts | grep "safeEmit\|this.emit"
grep "safeEmit\|this.emit" node/src/Producer.ts
```
**Failure**: Different event name strings

### Mode 4: Log Format String Changed
**Symptom**: Log parsing breaks, alerts miss patterns
**Cause**: Format specifiers or delimiters changed
**Detection**:
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep "MS_DEBUG_TAG_LIVELYAPP" | head -5
grep "MS_DEBUG_TAG_LIVELYAPP" worker/src/RTC/Transport.cpp | head -5
```
**Failure**: Different format strings

### Mode 5: Type Export Removed
**Symptom**: Consumer TypeScript code fails to compile
**Cause**: Public type no longer exported
**Detection**:
```bash
git show origin/v3-lively:node/src/index.ts | grep "export"
grep "export" node/src/index.ts
```
**Failure**: Missing export

---

## Mechanical Audit Checklist

### 1. Enum Value Verification

#### 1.1 Notification Events
```bash
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A100 "enum Event" | head -50
grep -A100 "enum Event" worker/fbs/notification.fbs | head -50
```
**Verify**: Each enum name has same integer value
- [ ] All notification events match

#### 1.2 Request Methods
```bash
git show origin/v3-lively:worker/fbs/request.fbs | grep -A100 "enum Method" | head -80
grep -A100 "enum Method" worker/fbs/request.fbs | head -80
```
- [ ] All request methods match

---

### 2. CLI Option Verification

#### 2.1 Short Options
```bash
git show origin/v3-lively:worker/src/main.cpp | grep "getopt_long\|case '" | head -30
grep "getopt_long\|case '" worker/src/main.cpp | head -30
```
**Expected**: Identical option strings and case handlers
- [ ] All short options match v3-lively

#### 2.2 Long Option Names
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -A30 "option long_options"
grep -A30 "option long_options" worker/src/main.cpp
```
- [ ] All long options match v3-lively

---

### 3. Event Name Verification

#### 3.1 Producer Events
```bash
git show origin/v3-lively:node/src/Producer.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Producer.ts | head -10
```
- [ ] All producer event names match

#### 3.2 Transport Events
```bash
git show origin/v3-lively:node/src/Transport.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Transport.ts | head -10
```
- [ ] All transport event names match

#### 3.3 Worker Events
```bash
git show origin/v3-lively:node/src/Worker.ts | grep -E "safeEmit|this\.emit" | head -10
grep -E "safeEmit|this\.emit" node/src/Worker.ts | head -10
```
- [ ] All worker event names match

---

### 4. Log Format Verification

#### 4.1 Lively Log Macros
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep "MS_DEBUG_TAG_LIVELYAPP\|MS_DEBUG_DEV\|MS_WARN_DEV" | wc -l
grep "MS_DEBUG_TAG_LIVELYAPP\|MS_DEBUG_DEV\|MS_WARN_DEV" worker/src/RTC/Transport.cpp | wc -l
```
**Expected**: Same count
- [ ] Log macro count matches v3-lively

#### 4.2 Format String Content
```bash
# For each file with Lively logs, compare format strings
git diff origin/v3-lively -- worker/src/RTC/Transport.cpp | grep "MS_DEBUG_TAG_LIVELYAPP\|MS_DEBUG_DEV"
```
**Expected**: No differences in format strings
- [ ] All format strings identical

---

### 5. Type Export Verification

#### 5.1 Index Exports
```bash
git show origin/v3-lively:node/src/index.ts | grep "^export" | sort
grep "^export" node/src/index.ts | sort
```
- [ ] All exports present

#### 5.2 Type Definitions
```bash
git show origin/v3-lively:node/src/types.ts | grep "^export" | sort
grep "^export" node/src/types.ts | sort
```
- [ ] All type exports present

---

### 6. Lively Initialization Verification

#### 6.1 WebRtcTransport Constructor
```bash
git show origin/v3-lively:worker/src/RTC/WebRtcTransport.cpp | grep -A3 "producerBinLogEnabled"
grep -A3 "producerBinLogEnabled" worker/src/RTC/WebRtcTransport.cpp
```
- [ ] producerBinLogEnabled initialization matches

#### 6.2 Transport Constructor
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A5 "consumersBinLog.InitLog\|lastProducerStatsReport"
grep -A5 "consumersBinLog.InitLog\|lastProducerStatsReport" worker/src/RTC/Transport.cpp
```
- [ ] Consumer binlog init matches
- [ ] Producer stats init matches

#### 6.3 Producer Constructor
```bash
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -A10 "binLog.InitLog"
grep -A10 "binLog.InitLog" worker/src/RTC/Producer.cpp
```
- [ ] Producer binlog init matches

---

### 7. Timer Logic Verification

#### 7.1 OnTimer Binlog Calls
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A20 "void Transport::OnTimer" | grep "FillBinLogStats"
grep -A50 "void Transport::OnTimer" worker/src/RTC/Transport.cpp | grep "FillBinLogStats"
```
- [ ] FillBinLogStats call present

#### 7.2 OnTimer Stats Emission
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -A50 "void Transport::OnTimer" | grep "EmitProducerStats"
grep -A50 "void Transport::OnTimer" worker/src/RTC/Transport.cpp | grep "EmitProducerStats"
```
- [ ] EmitProducerStats call present

---

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

---

## Invocation

```
/v3-lively-comparator <filepath>
```

Or for multiple files:
```
/v3-lively-comparator <file1> <file2> ...
```

Performs character-by-character comparison against v3-lively.
