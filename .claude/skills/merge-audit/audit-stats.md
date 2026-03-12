# Audit Stats Skill

## Overview

This skill provides procedural knowledge for auditing Lively's periodic producer stats feature (RND-568). Use this skill when you need to verify that per-producer statistics are being emitted to Node.js every 10 seconds.

**Scope Boundaries:**
- IN SCOPE: lastProducerStatsReport initialization, 10-second interval check, EmitProducerStats implementation, Node.js handler, FBS notification
- OUT OF SCOPE: Binary logging (use `audit-binlogs`), FBS schema structure (use `audit-fbs-schema`)

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `scope` | string | No | "full", "cpp", "nodejs", "fbs" (default: "full") |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `silent_failure_checks` | object[] | Results of silent failure mode detection |
| `checklist_results` | object[] | Results of mechanical checklist |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Activation Chain Reference

```
1. Transport constructor
   └── Sets lastProducerStatsReport = DepLibUV::GetTimeMs()
   └── Only when Options.producer_stats == true

2. Transport::OnTimer (every 10 seconds)
   └── Checks: nowMs - lastProducerStatsReport >= 10000
   └── Iterates mapProducers
   └── Calls producer->EmitProducerStats()

3. Producer::EmitProducerStats()
   └── Iterates rtpStreamByEncodingIdx
   └── Builds ProducerStatsNotification FBS message
   └── Emits via channelNotifier->Emit()

4. Node.js Producer.ts
   └── Handles PRODUCER_STATS notification
   └── Emits 'producerstats' event with ProducerStatEvent[]
```

## Silent Failure Modes

### Mode 1: producerStats Option Not in FBS
**Symptom**: Stats never emitted even with appData.producerStats=true
```bash
grep "producer_stats" worker/fbs/transport.fbs
```
**Expected**: `producer_stats: bool = false;`
**Failure**: No output

### Mode 2: Node.js Doesn't Populate producerStats
**Symptom**: FBS field exists but always false
```bash
grep "producerStats" node/src/Router.ts | head -3
```
**Expected**: `(appData as any)?.producerStats ?? false`
**Failure**: No match or hardcoded `false`

### Mode 3: lastProducerStatsReport Never Initialized
**Symptom**: 10-second check never triggers
```bash
grep -A3 "producerStats()" worker/src/RTC/Transport.cpp
```
**Expected**: `if (options->producerStats()) { this->lastProducerStatsReport = ... }`
**Failure**: No match or assignment outside condition

### Mode 4: OnTimer Check Missing
**Symptom**: Initialization happens but stats never emitted
```bash
grep "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep "10000"
```
**Expected**: `this->lastProducerStatsReport + 10000 < nowMs`
**Failure**: No match

### Mode 5: EmitProducerStats Not Called
**Symptom**: Check passes but nothing emitted
```bash
grep -A5 "lastProducerStatsReport + 10000" worker/src/RTC/Transport.cpp
```
**Expected**: Loop with `producer->EmitProducerStats()`
**Failure**: No EmitProducerStats call

### Mode 6: Node.js Handler Missing
**Symptom**: C++ emits but Node.js never fires event
```bash
grep "PRODUCER_STATS" node/src/Producer.ts
```
**Expected**: `case Event.PRODUCER_STATS:`
**Failure**: No match

### Mode 7: Event Not Emitted
**Symptom**: Handler exists but no event reaches application
```bash
grep "producerstats" node/src/Producer.ts
```
**Expected**: `this.safeEmit('producerstats', stats);`
**Failure**: No match

## Methodology

### Step 1: Silent Failure Mode Checks

```bash
# Mode 1: FBS field exists
grep "producer_stats" worker/fbs/transport.fbs

# Mode 2: Node.js populates
grep "producerStats" node/src/Router.ts | head -3

# Mode 3: Initialization
grep -A3 "options->producerStats()" worker/src/RTC/Transport.cpp

# Mode 4: OnTimer check
grep "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep "10000"

# Mode 5: EmitProducerStats call
grep -A5 "lastProducerStatsReport + 10000" worker/src/RTC/Transport.cpp

# Mode 6: Node.js handler
grep "PRODUCER_STATS" node/src/Producer.ts

# Mode 7: Event emission
grep "producerstats" node/src/Producer.ts
```

### Step 2: Transport.cpp Checklist

```bash
# 1.1 FBS field extraction
grep -B2 -A3 "options->producerStats()" worker/src/RTC/Transport.cpp

# 1.2 OnTimer 10-second check
grep -B2 -A8 "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep -A8 "10000"

# 1.3 No TRANSCODE guard
grep -B5 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```

### Step 3: Producer.cpp Checklist

```bash
# 2.1 EmitProducerStats implementation
grep -A20 "void Producer::EmitProducerStats" worker/src/RTC/Producer.cpp | head -25

# 2.2 FBS message fields
grep -A15 "EmitProducerStats" worker/src/RTC/Producer.cpp | grep -E "Bitrate|Width|Height|Frames|ssrc"
```

### Step 4: Node.js Checklist

```bash
# 3.1 Notification handler
grep -B2 -A15 "PRODUCER_STATS" node/src/Producer.ts

# 3.2 Event emission
grep "producerstats" node/src/Producer.ts
```

### Step 5: FBS Schema Checklist

```bash
# 4.1 ProducerStatsNotification table
grep -A10 "ProducerStatEntry\|ProducerStatsNotification" worker/fbs/producer.fbs

# 4.2 Notification enum
grep "PRODUCER_STATS" worker/fbs/notification.fbs
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| REGRESSION | R001 | TRANSCODE guard added | Remove guard from stats emission |
| MISSING | M001 | Initialization missing | Add producerStats check in constructor |
| MISSING | M002 | OnTimer check missing | Add 10-second interval logic |
| MISSING | M003 | Node.js handler missing | Add PRODUCER_STATS case |

## Examples

### Example 1: Clean Audit

**Output:**
```markdown
## Periodic Stats Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. FBS field | producer_stats present | PASS |
| 2. Node.js populates | appData?.producerStats | PASS |
| 3. Init conditional | options->producerStats() | PASS |
| 4. OnTimer check | +10000 comparison | PASS |
| 5. EmitProducerStats | Called in loop | PASS |
| 6. Node.js handler | PRODUCER_STATS case | PASS |
| 7. Event emission | safeEmit('producerstats') | PASS |

### Status: PASS
```

## Constraints

### Operational Limits
- Maximum 15 grep commands per audit

### Prohibited Actions
- NEVER modify source files
- NEVER execute runtime tests

## Output Template

```markdown
## Periodic Stats Audit Results

### Silent Failure Mode Checks
| Mode | Check | Result |
|------|-------|--------|
| 1. FBS field | {observation} | PASS/FAIL |
| 2. Node.js populates | {observation} | PASS/FAIL |
| 3. Init conditional | {observation} | PASS/FAIL |
| 4. OnTimer check | {observation} | PASS/FAIL |
| 5. EmitProducerStats | {observation} | PASS/FAIL |
| 6. Node.js handler | {observation} | PASS/FAIL |
| 7. Event emission | {observation} | PASS/FAIL |

### Activation Chain Verification
| Step | Component | Present | Correct |
|------|-----------|---------|---------|
| 1 | Options.producer_stats | [ ] | [ ] |
| 2 | lastProducerStatsReport init | [ ] | [ ] |
| 3 | 10-second check | [ ] | [ ] |
| 4 | EmitProducerStats() | [ ] | [ ] |
| 5 | Node.js handler | [ ] | [ ] |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-binlogs` | Verifying binary logging |
| `audit-fbs-schema` | Verifying ProducerStatsNotification schema |
