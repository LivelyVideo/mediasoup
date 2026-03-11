# FBS Schema Auditor

**Last Updated**: 2026-03-11

You are auditing FlatBuffers schema files to ensure all Lively data fields have explicit schema entries. Missing FBS fields cause silent data loss.

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

---

## Silent Failure Modes

### Mode 1: Field Missing from FBS Schema
**Symptom**: C++ code gets empty string/default value, no error
**Cause**: Field not defined in .fbs file
**Detection**:
```bash
grep "call_id\|peer_id\|mirror_id\|stream_name\|client_referrer\|producer_stats" worker/fbs/transport.fbs | wc -l
```
**Expected**: `6` (all Lively fields present)
**Failure**: Less than 6

### Mode 2: Field in FBS but Not Populated in TypeScript
**Symptom**: Field exists in schema but always null/empty in C++
**Cause**: Node.js code doesn't set the field value
**Detection**:
```bash
grep -c "callId\|peerId\|mirrorId\|streamName\|clientReferrer\|producerStats" node/src/Router.ts
```
**Expected**: `6` or more (each field referenced)
**Failure**: Less than 6

### Mode 3: Field in FBS but Not Extracted in C++
**Symptom**: Node.js sends value but C++ ignores it
**Cause**: C++ doesn't call options->field_name()
**Detection**:
```bash
grep -c "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp
```
**Expected**: `6` (all fields extracted)
**Failure**: Less than 6

### Mode 4: ProduceRequest userId Missing
**Symptom**: Producer binlog filename has wrong/empty userId
**Cause**: user_id not in ProduceRequest table
**Detection**:
```bash
grep "user_id" worker/fbs/transport.fbs
```
**Expected**: `user_id: string;` in ProduceRequest table
**Failure**: No output

### Mode 5: Enum Value Mismatch
**Symptom**: Wrong notification handler fires
**Cause**: Enum integer assignment changed
**Detection**:
```bash
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A5 "PRODUCER_STATS\|LOGGER_WRITE_FAILED"
grep -A5 "PRODUCER_STATS\|LOGGER_WRITE_FAILED" worker/fbs/notification.fbs
```
**Failure**: Different enum positions

---

## Mechanical Audit Checklist

### 1. Options Table (transport.fbs)

#### 1.1 All Lively Fields Present
```bash
grep -A50 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"
```
**Expected**: 6 lines with field definitions
- [ ] call_id: string
- [ ] peer_id: string
- [ ] mirror_id: string
- [ ] stream_name: string
- [ ] client_referrer: string
- [ ] producer_stats: bool

#### 1.2 Field Types Correct
```bash
grep -A50 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"
```
**Verify**: Each field has correct type (string for IDs, bool for flags)
- [ ] All string fields are `string` type
- [ ] producer_stats is `bool` type

---

### 2. ProduceRequest Table (transport.fbs)

#### 2.1 Lively Fields Present
```bash
grep -A20 "table ProduceRequest" worker/fbs/transport.fbs | grep -E "user_id|client_referrer"
```
**Expected**: Both fields defined
- [ ] user_id: string
- [ ] client_referrer: string

---

### 3. Notification Events (notification.fbs)

#### 3.1 Lively Events Present
```bash
grep -E "PRODUCER_STATS|LOGGER_WRITE_FAILED" worker/fbs/notification.fbs
```
**Expected**: Both events in enum
- [ ] PRODUCER_STATS in Event enum
- [ ] LOGGER_WRITE_FAILED in Event enum

#### 3.2 Event Enum Values Match v3-lively
```bash
# v3-lively enum
git show origin/v3-lively:worker/fbs/notification.fbs | grep -A100 "enum Event" | head -50

# Current enum
grep -A100 "enum Event" worker/fbs/notification.fbs | head -50
```
**Verify**: Same order and explicit values
- [ ] Enum order matches v3-lively
- [ ] No new items inserted before Lively events

---

### 4. Producer FBS Tables (producer.fbs)

#### 4.1 ProducerStatsNotification Present
```bash
grep "ProducerStatsNotification\|ProducerStatEntry" worker/fbs/producer.fbs
```
**Expected**: Both tables defined
- [ ] ProducerStatEntry table exists
- [ ] ProducerStatsNotification table exists

#### 4.2 ProducerStatEntry Fields
```bash
grep -A10 "table ProducerStatEntry" worker/fbs/producer.fbs
```
**Expected fields**:
- [ ] now_ms: uint64
- [ ] ssrc: uint32
- [ ] bitrate: uint32
- [ ] width: uint32
- [ ] height: uint32
- [ ] frames: uint32

---

### 5. Log Tables (log.fbs)

#### 5.1 Logger Tables Present
```bash
grep "table" worker/fbs/log.fbs
```
**Expected**:
- [ ] Log table
- [ ] MslogOpenRequest table
- [ ] WriteFailedNotification table

#### 5.2 MslogOpenRequest Fields
```bash
grep -A5 "MslogOpenRequest" worker/fbs/log.fbs
```
**Expected**: `mslogname: string`
- [ ] mslogname field present

#### 5.3 WriteFailedNotification Fields
```bash
grep -A10 "WriteFailedNotification" worker/fbs/log.fbs
```
**Expected fields**:
- [ ] source field
- [ ] error field
- [ ] file field
- [ ] data field

---

### 6. TypeScript Population Verification

#### 6.1 Router.ts Options Population
```bash
grep -A30 "new FbsTransport.OptionsT" node/src/Router.ts | head -35
```
**Verify**: All Lively fields passed to constructor
- [ ] callId passed
- [ ] peerId passed
- [ ] mirrorId passed
- [ ] streamName passed
- [ ] clientReferrer passed
- [ ] producerStats passed

#### 6.2 Transport.ts ProduceRequest Population
```bash
grep -A20 "ProduceRequest" node/src/Transport.ts | grep -E "userId|clientReferrer"
```
**Verify**: Lively fields populated
- [ ] userId offset created
- [ ] clientReferrer offset created

---

### 7. C++ Extraction Verification

#### 7.1 Transport.cpp Options Extraction
```bash
grep -A2 "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp
```
**Verify**: Each field extracted with null check
- [ ] callId extracted to lively.callId
- [ ] peerId extracted to lively.peerId
- [ ] mirrorId extracted to lively.mirrorId
- [ ] streamName extracted to lively.streamName
- [ ] clientReferrer extracted to local variable
- [ ] producerStats checked and used

#### 7.2 Producer.cpp ProduceRequest Extraction
```bash
grep -A2 "data->userId\|data->clientReferrer" worker/src/RTC/Producer.cpp
```
**Verify**: Both fields extracted
- [ ] userId extracted from data
- [ ] clientReferrer extracted from data

---

### 8. End-to-End Field Verification

For each Lively field, verify complete chain:

#### 8.1 callId Chain
```bash
# 1. FBS schema
grep "call_id" worker/fbs/transport.fbs

# 2. Node.js extraction from appData
grep "callId" node/src/Router.ts | head -3

# 3. C++ extraction
grep "options->callId\|lively.callId" worker/src/RTC/Transport.cpp | head -3

# 4. Usage in feature
grep "lively.callId\|callId" worker/src/RTC/Producer.cpp | head -3
```
- [ ] callId: FBS → TS → C++ → Feature

#### 8.2 userId Chain
```bash
# 1. FBS schema
grep "user_id" worker/fbs/transport.fbs

# 2. Node.js (Transport.ts for produce)
grep "userId" node/src/Transport.ts | head -5

# 3. C++ extraction
grep "data->userId" worker/src/RTC/Producer.cpp

# 4. Usage (binlog filename)
grep "userId" worker/src/RTC/Producer.cpp | grep "binLog\|filename" | head -3
```
- [ ] userId: FBS → TS → C++ → Binlog

---

## Quick Verification Commands

```bash
# Count Lively fields in Options table
grep -A50 "table Options" worker/fbs/transport.fbs | grep -cE "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"

# List all FBS tables
grep "^table" worker/fbs/*.fbs

# Compare with v3-lively
diff <(git show origin/v3-lively:worker/fbs/transport.fbs | grep -A50 "table Options") <(grep -A50 "table Options" worker/fbs/transport.fbs)
```

---

## Invocation

```
/fbs-schema-auditor <table_name>
```

Or for complete audit:
```
/fbs-schema-auditor --all-tables
```

Performs complete FBS schema verification for Lively fields.
