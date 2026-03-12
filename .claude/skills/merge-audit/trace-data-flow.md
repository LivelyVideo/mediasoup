# Trace Data Flow Skill

## Overview

This skill provides procedural knowledge for tracing data flows from source to destination. Use this skill when you need to verify that Lively field values arrive at their destinations correctly. Silent data corruption (wrong values, empty strings) is harder to detect than missing code.

**Core Principle**: Trace the data, not just the code. A function can exist and be called but receive wrong inputs.

**Scope Boundaries:**
- IN SCOPE: FBS schema -> TypeScript population -> C++ extraction -> Feature usage
- OUT OF SCOPE: Schema definition (use `audit-fbs-schema`), component existence (use feature-specific skills)

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `field` | string | No | Specific field to trace (default: all Lively fields) |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `field_traces` | object[] | Complete trace for each field |
| `broken_links` | object[] | Points where data flow breaks |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Lively Fields to Trace

| Field | Flow |
|-------|------|
| callId | FBS Options -> Router.ts -> Transport.cpp -> binlog filename |
| userId | FBS ProduceRequest -> Transport.ts -> Producer.cpp -> binlog filename |
| clientReferrer | FBS Options/ProduceRequest -> TS -> C++ -> binlog subdirectory |
| producerStats | FBS Options -> Router.ts -> Transport.cpp -> timer init |
| peerId | FBS Options -> Router.ts -> Transport.cpp -> lively struct |
| mirrorId | FBS Options -> Router.ts -> Transport.cpp -> lively struct |
| streamName | FBS Options -> Router.ts -> Transport.cpp -> lively struct |

## Methodology

### Trace: callId

```bash
# 1. FBS Schema - field exists?
grep "call_id" worker/fbs/transport.fbs
# Expected: call_id: string;  (in Options table)
```

```bash
# 2. Node.js Population - Router.ts populates?
grep -n "callId" node/src/Router.ts | grep -E "appData|OptionsT"
# Expected: (appData as any)?.callId ?? null
```

```bash
# 3. C++ Extraction - Transport.cpp extracts?
grep -A2 "options->callId()" worker/src/RTC/Transport.cpp
# Expected: this->lively.callId.assign(options->callId()->str());
```

```bash
# 4. Feature Usage - used in binlog filename?
grep "lively.callId\|callId" worker/src/RTC/Transport.cpp | grep -v "//"
# Expected: Multiple matches including ConsumerFileName lambda
```

### Trace: userId

```bash
# 1. FBS Schema - field exists in ProduceRequest?
grep "user_id" worker/fbs/transport.fbs
# Expected: user_id: string;  (in ProduceRequest table)
```

```bash
# 2. Node.js Population - Transport.ts populates?
grep -n "userId\|user_id" node/src/Transport.ts | grep -E "Offset|add"
# Expected: userIdOffset = builder.createString(userId)
```

```bash
# 3. C++ Extraction - Producer.cpp extracts?
grep -A2 "data->userId()" worker/src/RTC/Producer.cpp
# Expected: userId = data->userId()->str();
```

```bash
# 4. Feature Usage - used in ProducerFileName?
grep -n "userId" worker/src/RTC/Producer.cpp | grep -E "binLog|FileName|lambda"
# Expected: userId captured in lambda
```

### Trace: clientReferrer

```bash
# 1. FBS Schema - field exists in BOTH tables?
grep -n "client_referrer" worker/fbs/transport.fbs
# Expected: 2 matches - Options AND ProduceRequest
```

```bash
# 2a. Node.js Population - Router.ts (for Transport)?
grep -n "clientReferrer" node/src/Router.ts | head -5
# Expected: (appData as any)?.clientReferrer ?? null
```

```bash
# 2b. Node.js Population - Transport.ts (for Producer)?
grep -n "clientReferrer" node/src/Transport.ts | grep -E "Offset|add"
# Expected: clientReferrerOffset and addClientReferrer
```

```bash
# 3a. C++ Extraction - Transport.cpp?
grep -A2 "options->clientReferrer()" worker/src/RTC/Transport.cpp
# Expected: clientReferrer.assign(options->clientReferrer()->str());
```

```bash
# 3b. C++ Extraction - Producer.cpp?
grep -A2 "data->clientReferrer()" worker/src/RTC/Producer.cpp
# Expected: clientReferrer = data->clientReferrer()->str();
```

### Trace: producerStats

```bash
# 1. FBS Schema - field exists?
grep "producer_stats" worker/fbs/transport.fbs
# Expected: producer_stats: bool = false;
```

```bash
# 2. Node.js Population - Router.ts?
grep -n "producerStats" node/src/Router.ts
# Expected: (appData as any)?.producerStats ?? false
```

```bash
# 3. C++ Extraction and Usage - Transport.cpp?
grep -A3 "options->producerStats()" worker/src/RTC/Transport.cpp
# Expected: if (options->producerStats()) { this->lastProducerStatsReport = ... }
```

```bash
# 4. Feature Activation - OnTimer checks?
grep -n "lastProducerStatsReport" worker/src/RTC/Transport.cpp | head -5
# Expected: Init AND 10000ms check
```

### Trace: peerId, mirrorId, streamName (batch)

```bash
# 1. FBS Schema
grep -E "peer_id|mirror_id|stream_name" worker/fbs/transport.fbs
# Expected: 3 lines in Options table

# 2. Node.js Population
grep -E "peerId|mirrorId|streamName" node/src/Router.ts | grep "appData"
# Expected: 3 lines with (appData as any)?.fieldName ?? null

# 3. C++ Extraction
grep -E "options->(peerId|mirrorId|streamName)" worker/src/RTC/Transport.cpp
# Expected: 3 extraction lines
```

## Binlog Filename Verification

### Producer Filename: `ms_p_<userId>_<callId>_<producerId>_...`

```bash
# Verify lambda captures correct variables
grep -B2 -A5 "ProducerFileName" worker/src/RTC/Producer.cpp
# Expected: [callId, producerId, userId, clientReferrer] capture
```

```bash
# Verify each captured variable is extracted from FBS
grep -E "(userId|callId|producerId|clientReferrer) =" worker/src/RTC/Producer.cpp | head -10
# Expected: Each assigned from data->field() or parameter
```

### Consumer Filename: `<clientReferrer>/ms_c_<callId>_<consumerId>_...`

```bash
# Verify ConsumerFileName in Transport.cpp
grep -B2 -A5 "ConsumerFileName" worker/src/RTC/Transport.cpp
# Expected: [clientReferrer, callId] capture
```

## Silent Failure Modes

### Mode 1: FBS Field Missing
```bash
grep "call_id" worker/fbs/transport.fbs || echo "MISSING"
```

### Mode 2: Node.js Doesn't Populate
```bash
grep "addCallId\|call_id" node/src/Router.ts
```

### Mode 3: C++ Doesn't Extract
```bash
grep "options->callId()" worker/src/RTC/Transport.cpp
```

### Mode 4: Wrong Variable in Lambda
```bash
grep -A3 "ProducerFileName" worker/src/RTC/Producer.cpp | grep -o "\[.*\]"
# Expected: [callId, producerId, userId, clientReferrer]
```

### Mode 5: Parameter Order Wrong
```bash
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -A1 "ProducerFileName"
grep -A1 "ProducerFileName" worker/src/RTC/Producer.cpp
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| BROKEN | B001 | FBS field missing | Add field to schema |
| BROKEN | B002 | Node.js doesn't populate | Add appData extraction |
| BROKEN | B003 | C++ doesn't extract | Add options-> extraction |
| BROKEN | B004 | Lambda missing variable | Add variable to capture |
| REGRESSION | R001 | Wrong variable assignment | Restore v3-lively pattern |

## Examples

### Example 1: Clean Trace

**Output:**
```markdown
## Data Flow Audit Results

### Field Traces
| Field | FBS | TS Populate | C++ Extract | Feature Use | Status |
|-------|-----|-------------|-------------|-------------|--------|
| callId | YES | YES | YES | YES | PASS |
| userId | YES | YES | YES | YES | PASS |
| clientReferrer (Options) | YES | YES | YES | YES | PASS |
| clientReferrer (Produce) | YES | YES | YES | YES | PASS |
| producerStats | YES | YES | YES | YES | PASS |
| peerId | YES | YES | YES | YES | PASS |
| mirrorId | YES | YES | YES | YES | PASS |
| streamName | YES | YES | YES | YES | PASS |

### Binlog Filename Lambda
| Filename | Variables Captured | Status |
|----------|-------------------|--------|
| ProducerFileName | callId, producerId, userId, clientReferrer | PASS |
| ConsumerFileName | clientReferrer, callId | PASS |

### Status: PASS
```

## Constraints

### Operational Limits
- Maximum 30 grep commands per trace

### Prohibited Actions
- NEVER modify source files
- NEVER assume values without verification

## Output Template

```markdown
## Data Flow Audit Results

### Complete Field Traces
| Field | FBS | Node.js | C++ Extract | Feature Use | Status |
|-------|-----|---------|-------------|-------------|--------|
| callId | [ ] | [ ] | [ ] | [ ] | |
| peerId | [ ] | [ ] | [ ] | [ ] | |
| mirrorId | [ ] | [ ] | [ ] | [ ] | |
| streamName | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer (Options) | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer (Produce) | [ ] | [ ] | [ ] | [ ] | |
| producerStats | [ ] | [ ] | [ ] | [ ] | |
| userId | [ ] | [ ] | [ ] | [ ] | |

### Lambda Capture Verification
| Lambda | Expected Variables | Actual | Status |
|--------|-------------------|--------|--------|
| ProducerFileName | callId, producerId, userId, clientReferrer | {actual} | PASS/FAIL |
| ConsumerFileName | clientReferrer, callId | {actual} | PASS/FAIL |

### Broken Links
| Field | Break Point | Issue |
|-------|-------------|-------|
| {field} | {location} | {description} |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-fbs-schema` | Verifying schema completeness |
| `audit-appdata` | Verifying extraction patterns |
| `audit-binlogs` | Verifying binlog feature |
