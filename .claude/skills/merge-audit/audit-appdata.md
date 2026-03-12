# Audit AppData Skill

## Overview

This skill provides procedural knowledge for auditing Lively's appData handling. Use this skill when you need to verify that Lively-specific fields (callId, peerId, userId, etc.) flow correctly from Node.js to C++.

**Scope Boundaries:**
- IN SCOPE: FBS field definitions, Node.js extraction from appData, C++ extraction from FBS, storage in members
- OUT OF SCOPE: Field usage in features (use `trace-data-flow`), FBS schema completeness (use `audit-fbs-schema`)

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `scope` | string | No | "full", "transport", "producer", or specific field name |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `field_checks` | object[] | Results for each Lively field |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Lively Fields Reference

### Transport-Level Fields (Options table)

| Field | Source | FBS Field | Used By |
|-------|--------|-----------|---------|
| callId | `appData.callId` | `Options.call_id` | Binlog filenames |
| peerId | `appData.peerId` | `Options.peer_id` | Diagnostics |
| mirrorId | `appData.mirrorId` | `Options.mirror_id` | Diagnostics |
| streamName | `appData.streamName` | `Options.stream_name` | Diagnostics |
| clientReferrer | `appData.clientReferrer` | `Options.client_referrer` | Binlog subdirectory |
| producerStats | `appData.producerStats` | `Options.producer_stats` | Stats emission |

### Producer-Level Fields (ProduceRequest table)

| Field | Source | FBS Field | Used By |
|-------|--------|-----------|---------|
| userId | `appData` via helper | `ProduceRequest.user_id` | Producer binlog filename |
| clientReferrer | `appData.clientReferrer` | `ProduceRequest.client_referrer` | Producer binlog subdirectory |

## Silent Failure Modes

### Mode 1: FBS Field Missing from Schema
**Symptom**: Field always empty in C++, no compilation error
```bash
grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats" worker/fbs/transport.fbs | wc -l
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 2: Node.js Doesn't Extract from appData
**Symptom**: FBS field defined but value always null
```bash
grep -c "appData.*callId\|appData.*peerId\|appData.*mirrorId\|appData.*streamName\|appData.*clientReferrer\|appData.*producerStats" node/src/Router.ts
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 4: C++ Doesn't Extract from FBS
**Symptom**: Value sent but not stored in member
```bash
grep -c "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 6: userId Not Extracted in Producer
**Symptom**: Producer binlog filenames have empty userId
```bash
grep "data->userId\|userId.*=" worker/src/RTC/Producer.cpp | head -5
```
**Expected**: `userId = data->userId()->str();`
**Failure**: `userId = ""` or missing

## Methodology

### Step 1: FBS Schema Check

```bash
# 1.1 Options table fields
grep -A30 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"

# 1.2 ProduceRequest table fields
grep -A15 "table ProduceRequest" worker/fbs/transport.fbs | grep -E "user_id|client_referrer"
```

### Step 2: Node.js Extraction (Router.ts)

```bash
# 2.1 AppData extraction
grep -n "appData" node/src/Router.ts | grep -E "callId|peerId|mirrorId|streamName|clientReferrer|producerStats" | head -10

# 2.2 OptionsT constructor
grep -B5 -A20 "new FbsTransport.OptionsT" node/src/Router.ts | head -30
```

### Step 3: Node.js Extraction (Transport.ts)

```bash
# 3.1 ProduceRequest userId
grep -n "userId" node/src/Transport.ts | head -10

# 3.2 ProduceRequest clientReferrer
grep -n "clientReferrer" node/src/Transport.ts | grep -E "Offset|add" | head -5
```

### Step 4: C++ Extraction (Transport.cpp)

```bash
# 4.1 Field extraction
grep -A2 "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer" worker/src/RTC/Transport.cpp

# 4.2 producerStats handling
grep -A3 "options->producerStats" worker/src/RTC/Transport.cpp
```

### Step 5: C++ Extraction (Producer.cpp)

```bash
# 5.1 userId extraction
grep -B3 -A3 "data->userId" worker/src/RTC/Producer.cpp

# 5.2 clientReferrer extraction
grep -B3 -A3 "data->clientReferrer" worker/src/RTC/Producer.cpp
```

### Step 6: Lively.hpp Verification

```bash
# 6.1 AppData class
cat worker/include/Lively.hpp
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| MISSING | M001 | FBS field missing | Add field to transport.fbs |
| MISSING | M002 | Node.js extraction missing | Add appData extraction in Router.ts |
| MISSING | M003 | C++ extraction missing | Add options-> extraction in Transport.cpp |
| REGRESSION | R001 | Field assignment removed | Restore assignment from v3-lively |

## Examples

### Example 1: Clean Audit

**Output:**
```markdown
## AppData Audit Results

### Field Verification
| Field | FBS | Node.js | C++ | Status |
|-------|-----|---------|-----|--------|
| callId | YES | YES | YES | PASS |
| peerId | YES | YES | YES | PASS |
| mirrorId | YES | YES | YES | PASS |
| streamName | YES | YES | YES | PASS |
| clientReferrer | YES | YES | YES | PASS |
| producerStats | YES | YES | YES | PASS |
| userId | YES | YES | YES | PASS |

### Status: PASS
```

## Constraints

### Operational Limits
- Maximum 20 grep commands per audit

### Prohibited Actions
- NEVER modify source files

## Output Template

```markdown
## AppData Audit Results

### Transport-Level Fields
| Field | FBS Schema | Node.js Extract | C++ Extract | Storage | Status |
|-------|------------|-----------------|-------------|---------|--------|
| callId | [ ] | [ ] | [ ] | [ ] | |
| peerId | [ ] | [ ] | [ ] | [ ] | |
| mirrorId | [ ] | [ ] | [ ] | [ ] | |
| streamName | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer | [ ] | [ ] | [ ] | [ ] | |
| producerStats | [ ] | [ ] | [ ] | [ ] | |

### Producer-Level Fields
| Field | FBS Schema | Node.js Extract | C++ Extract | Storage | Status |
|-------|------------|-----------------|-------------|---------|--------|
| userId | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer | [ ] | [ ] | [ ] | [ ] | |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-fbs-schema` | Verifying complete FBS schema |
| `trace-data-flow` | Verifying field values reach features |
