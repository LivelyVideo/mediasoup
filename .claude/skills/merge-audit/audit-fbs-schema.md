# Audit FBS Schema Skill

## Overview

This skill provides procedural knowledge for auditing FlatBuffers schema files. Use this skill when you need to verify that all Lively data fields have explicit schema entries. Missing FBS fields cause silent data loss.

**Scope Boundaries:**
- IN SCOPE: FBS table definitions, field presence, enum values, TypeScript population, C++ extraction
- OUT OF SCOPE: Data flow verification (use `trace-data-flow`), feature functionality

## Critical Understanding

In the JSON era, Lively extracted fields from free-form JSON:
```cpp
auto it = json.find("callId");  // Works even if field not in any schema
```

In the FBS era, fields must be **explicitly defined**:
```flatbuffers
table Options {
    call_id: string;  // Must exist or field is inaccessible
}
```

**A missing FBS field = silent data loss.** The C++ code compiles, runs, but gets empty/default values.

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `tables` | string[] | No | Specific tables to audit (default: all Lively tables) |

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `schema_checks` | object[] | Field presence in each table |
| `population_checks` | object[] | TypeScript population verification |
| `extraction_checks` | object[] | C++ extraction verification |
| `status` | string | "PASS", "FAIL", or "REGRESSION" |

## Silent Failure Modes

### Mode 1: Field Missing from FBS Schema
**Symptom**: C++ code gets empty string/default value, no error
```bash
grep "call_id\|peer_id\|mirror_id\|stream_name\|client_referrer\|producer_stats" worker/fbs/transport.fbs | wc -l
```
**Expected**: `6` (all Lively fields present)
**Failure**: Less than 6

### Mode 2: Field in FBS but Not Populated in TypeScript
**Symptom**: Field exists in schema but always null/empty in C++
```bash
grep -c "callId\|peerId\|mirrorId\|streamName\|clientReferrer\|producerStats" node/src/Router.ts
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 3: Field in FBS but Not Extracted in C++
**Symptom**: Node.js sends value but C++ ignores it
```bash
grep -c "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp
```
**Expected**: `6`
**Failure**: Less than 6

### Mode 4: ProduceRequest userId Missing
**Symptom**: Producer binlog filename has wrong/empty userId
```bash
grep "user_id" worker/fbs/transport.fbs
```
**Expected**: `user_id: string;` in ProduceRequest table
**Failure**: No output

### Mode 5: Enum Value Mismatch
**Symptom**: Wrong notification handler fires
```bash
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A5 "PRODUCER_STATS\|LOGGER_WRITE_FAILED"
grep -A5 "PRODUCER_STATS\|LOGGER_WRITE_FAILED" worker/fbs/notification.fbs
```
**Failure**: Different enum positions

## Methodology

### Step 1: Options Table (transport.fbs)

```bash
# 1.1 All Lively fields present
grep -A50 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"

# 1.2 Field types correct
grep -A50 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"
```

### Step 2: ProduceRequest Table (transport.fbs)

```bash
# 2.1 Lively fields present
grep -A20 "table ProduceRequest" worker/fbs/transport.fbs | grep -E "user_id|client_referrer"
```

### Step 3: Notification Events (notification.fbs)

```bash
# 3.1 Lively events present
grep -E "PRODUCER_STATS|LOGGER_WRITE_FAILED" worker/fbs/notification.fbs

# 3.2 Event enum values match v3-lively
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A100 "enum Event" | head -50
grep -A100 "enum Event" worker/fbs/notification.fbs | head -50
```

### Step 4: Producer FBS Tables (producer.fbs)

```bash
# 4.1 ProducerStatsNotification present
grep "ProducerStatsNotification\|ProducerStatEntry" worker/fbs/producer.fbs

# 4.2 ProducerStatEntry fields
grep -A10 "table ProducerStatEntry" worker/fbs/producer.fbs
```

### Step 5: Log Tables (log.fbs)

```bash
# 5.1 Logger tables present
grep "table" worker/fbs/log.fbs

# 5.2 MslogOpenRequest fields
grep -A5 "MslogOpenRequest" worker/fbs/log.fbs

# 5.3 WriteFailedNotification fields
grep -A10 "WriteFailedNotification" worker/fbs/log.fbs
```

### Step 6: TypeScript Population Verification

```bash
# 6.1 Router.ts Options population
grep -A30 "new FbsTransport.OptionsT" node/src/Router.ts | head -35

# 6.2 Transport.ts ProduceRequest population
grep -A20 "ProduceRequest" node/src/Transport.ts | grep -E "userId|clientReferrer"
```

### Step 7: C++ Extraction Verification

```bash
# 7.1 Transport.cpp Options extraction
grep -A2 "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp

# 7.2 Producer.cpp ProduceRequest extraction
grep -A2 "data->userId\|data->clientReferrer" worker/src/RTC/Producer.cpp
```

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| MISSING | M001 | Field missing from schema | Add field to .fbs file |
| MISSING | M002 | Field not populated | Add assignment in TypeScript |
| MISSING | M003 | Field not extracted | Add extraction in C++ |
| REGRESSION | R001 | Enum order changed | Restore v3-lively enum order |

## Examples

### Example 1: Clean Audit

**Output:**
```markdown
## FBS Schema Audit Results

### Options Table
| Field | Defined | Type | Status |
|-------|---------|------|--------|
| call_id | YES | string | PASS |
| peer_id | YES | string | PASS |
| mirror_id | YES | string | PASS |
| stream_name | YES | string | PASS |
| client_referrer | YES | string | PASS |
| producer_stats | YES | bool | PASS |

### ProduceRequest Table
| Field | Defined | Type | Status |
|-------|---------|------|--------|
| user_id | YES | string | PASS |
| client_referrer | YES | string | PASS |

### Notification Events
| Event | Present | Status |
|-------|---------|--------|
| PRODUCER_STATS | YES | PASS |
| LOGGER_WRITE_FAILED | YES | PASS |

### Status: PASS
```

## Constraints

### Operational Limits
- Maximum 25 grep commands per audit

### Prohibited Actions
- NEVER modify .fbs files
- NEVER regenerate FBS code

## Output Template

```markdown
## FBS Schema Audit Results

### Options Table (transport.fbs)
| Field | Defined | Type Correct | Status |
|-------|---------|--------------|--------|
| call_id | [ ] | [ ] | |
| peer_id | [ ] | [ ] | |
| mirror_id | [ ] | [ ] | |
| stream_name | [ ] | [ ] | |
| client_referrer | [ ] | [ ] | |
| producer_stats | [ ] | [ ] | |

### ProduceRequest Table (transport.fbs)
| Field | Defined | Type Correct | Status |
|-------|---------|--------------|--------|
| user_id | [ ] | [ ] | |
| client_referrer | [ ] | [ ] | |

### Notification Events
| Event | Present | Order Correct | Status |
|-------|---------|---------------|--------|
| PRODUCER_STATS | [ ] | [ ] | |
| LOGGER_WRITE_FAILED | [ ] | [ ] | |

### Producer Stats Tables
| Table | Defined | Fields Complete | Status |
|-------|---------|-----------------|--------|
| ProducerStatEntry | [ ] | [ ] | |
| ProducerStatsNotification | [ ] | [ ] | |

### Status: {PASS/FAIL/REGRESSION}
```

## Related Skills

| Skill | Use When |
|-------|----------|
| `audit-appdata` | Verifying appData extraction |
| `trace-data-flow` | Verifying end-to-end field flow |
