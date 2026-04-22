# AppData Expert

**Last Updated**: 2026-03-11

You are the expert on Lively's appData handling. AppData is the mechanism for passing Lively-specific fields (callId, peerId, etc.) from Node.js to C++.

## Feature Overview

AppData allows Lively to attach metadata to transports and producers without modifying upstream mediasoup's API. These fields flow through:
1. Node.js API call with `appData` object
2. FBS serialization with explicit Lively fields
3. C++ extraction and storage
4. Usage in Lively features (binlogs, diagnostics, etc.)

---

## Silent Failure Modes

### Mode 1: FBS Field Missing from Schema
**Symptom**: Field always empty in C++, no compilation error
**Cause**: Field not defined in transport.fbs
**Detection**:
```bash
grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats" worker/fbs/transport.fbs | wc -l
```
**Expected**: `7` (6 in Options + 2 in ProduceRequest, some overlap)
**Failure**: Less than 7

### Mode 2: Node.js Doesn't Extract from appData
**Symptom**: FBS field defined but value always null
**Cause**: Router.ts doesn't read appData.fieldName
**Detection**:
```bash
grep -c "appData.*callId\|appData.*peerId\|appData.*mirrorId\|appData.*streamName\|appData.*clientReferrer\|appData.*producerStats" node/src/Router.ts
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 3: FBS Builder Not Called
**Symptom**: Node.js extracts but doesn't serialize
**Cause**: Missing addFieldName() call
**Detection**:
```bash
grep -E "addCallId|addPeerId|addMirrorId|addStreamName|addClientReferrer|addProducerStats" node/src/Router.ts | wc -l
```
**Expected**: Multiple matches (in OptionsT constructor)
**Failure**: 0 matches (but this uses constructor params, not add methods)

### Mode 4: C++ Doesn't Extract from FBS
**Symptom**: Value sent but not stored in member
**Cause**: Missing options->fieldName() extraction
**Detection**:
```bash
grep -c "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer\|options->producerStats" worker/src/RTC/Transport.cpp
```
**Expected**: `6` or more
**Failure**: Less than 6

### Mode 5: Field Stored in Wrong Member
**Symptom**: Extraction happens but data goes to wrong place
**Cause**: Assignment to wrong variable
**Detection**:
```bash
grep -E "lively\.(callId|peerId|mirrorId|streamName)" worker/src/RTC/Transport.cpp
```
**Expected**: 4 assignments to lively struct
**Failure**: Missing or wrong assignments

### Mode 6: userId Not Extracted in Producer
**Symptom**: Producer binlog filenames have empty userId
**Cause**: data->userId() not called or not assigned
**Detection**:
```bash
grep "data->userId\|userId.*=" worker/src/RTC/Producer.cpp | head -5
```
**Expected**: `userId = data->userId()->str();`
**Failure**: `userId = ""` or missing

---

## Mechanical Audit Checklist

### transport.fbs Schema

#### 1.1 Options Table Fields
```bash
grep -A30 "table Options" worker/fbs/transport.fbs | grep -E "call_id|peer_id|mirror_id|stream_name|client_referrer|producer_stats"
```
**Expected**: All 6 fields present
- [ ] call_id: string
- [ ] peer_id: string
- [ ] mirror_id: string
- [ ] stream_name: string
- [ ] client_referrer: string
- [ ] producer_stats: bool

#### 1.2 ProduceRequest Table Fields
```bash
grep -A15 "table ProduceRequest" worker/fbs/transport.fbs | grep -E "user_id|client_referrer"
```
**Expected**: Both fields present
- [ ] user_id: string
- [ ] client_referrer: string

---

### Router.ts (Node.js)

#### 2.1 AppData Extraction
```bash
grep -n "appData" node/src/Router.ts | grep -E "callId|peerId|mirrorId|streamName|clientReferrer|producerStats" | head -10
```
**Expected**: Each field extracted with `(appData as any)?.fieldName ?? null/false`
- [ ] callId extracted
- [ ] peerId extracted
- [ ] mirrorId extracted
- [ ] streamName extracted
- [ ] clientReferrer extracted
- [ ] producerStats extracted

#### 2.2 OptionsT Constructor
```bash
grep -B5 -A20 "new FbsTransport.OptionsT" node/src/Router.ts | head -30
```
**Expected**: All Lively fields passed to constructor
- [ ] callId passed
- [ ] peerId passed
- [ ] mirrorId passed
- [ ] streamName passed
- [ ] clientReferrer passed
- [ ] producerStats passed

---

### Transport.ts (Node.js)

#### 3.1 ProduceRequest userId
```bash
grep -n "userId" node/src/Transport.ts | head -10
```
**Expected**: userId extracted and added to FBS
- [ ] userId extracted from appData
- [ ] userIdOffset created
- [ ] addUserId called

#### 3.2 ProduceRequest clientReferrer
```bash
grep -n "clientReferrer" node/src/Transport.ts | grep -E "Offset|add" | head -5
```
**Expected**: clientReferrer handled
- [ ] clientReferrerOffset created
- [ ] addClientReferrer called

---

### Transport.cpp (C++)

#### 4.1 Field Extraction
```bash
grep -A2 "options->callId\|options->peerId\|options->mirrorId\|options->streamName\|options->clientReferrer" worker/src/RTC/Transport.cpp
```
**Expected**: Each field extracted with null check and assigned
- [ ] callId extracted and assigned to lively.callId
- [ ] peerId extracted and assigned to lively.peerId
- [ ] mirrorId extracted and assigned to lively.mirrorId
- [ ] streamName extracted and assigned to lively.streamName
- [ ] clientReferrer extracted to local variable

#### 4.2 producerStats Handling
```bash
grep -A3 "options->producerStats" worker/src/RTC/Transport.cpp
```
**Expected**: Conditional initialization of lastProducerStatsReport
- [ ] producerStats checked
- [ ] lastProducerStatsReport initialized conditionally

---

### Producer.cpp (C++)

#### 5.1 userId Extraction
```bash
grep -B3 -A3 "data->userId" worker/src/RTC/Producer.cpp
```
**Expected**: `userId = data->userId()->str();` with null check
- [ ] userId extracted from ProduceRequest
- [ ] Null check present

#### 5.2 clientReferrer Extraction
```bash
grep -B3 -A3 "data->clientReferrer" worker/src/RTC/Producer.cpp
```
**Expected**: `clientReferrer = data->clientReferrer()->str();`
- [ ] clientReferrer extracted from ProduceRequest

---

### Lively.hpp

#### 6.1 AppData Class
```bash
cat worker/include/Lively.hpp
```
**Expected fields in AppData class**:
- callId
- peerId
- mirrorId
- streamName
- id
- [ ] All 5 fields present in AppData class

---

## AppData Fields Reference

### Transport-Level Fields (Options table)

| Field | Source | FBS Field | Used By |
|-------|--------|-----------|---------|
| callId | `appData.callId` | `Options.call_id` | Binlog filenames, diagnostics |
| peerId | `appData.peerId` | `Options.peer_id` | Diagnostics |
| mirrorId | `appData.mirrorId` | `Options.mirror_id` | Diagnostics |
| streamName | `appData.streamName` | `Options.stream_name` | Diagnostics |
| clientReferrer | `appData.clientReferrer` | `Options.client_referrer` | Binlog subdirectory |
| producerStats | `appData.producerStats` | `Options.producer_stats` | Enables stats emission |

### Producer-Level Fields (ProduceRequest table)

| Field | Source | FBS Field | Used By |
|-------|--------|-----------|---------|
| userId | `appData` via helper | `ProduceRequest.user_id` | Producer binlog filename |
| clientReferrer | `appData.clientReferrer` | `ProduceRequest.client_referrer` | Producer binlog subdirectory |

---

## File Ownership

| File | AppData-Related Code |
|------|---------------------|
| `worker/fbs/transport.fbs` | Options table with Lively fields |
| `node/src/Router.ts` | AppData extraction, Options FBS population |
| `node/src/Transport.ts` | ProduceRequest FBS population |
| `worker/src/RTC/Transport.cpp` | FBS extraction, member storage |
| `worker/src/RTC/Producer.cpp` | Producer FBS extraction |
| `worker/include/Lively.hpp` | AppData class definition |

---

## Invocation

```
/appdata-expert audit
```

Performs complete appData system audit.
