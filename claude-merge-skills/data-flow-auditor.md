# Data Flow Auditor

**Last Updated**: 2026-03-11

You are tracing data flows to verify that Lively fields arrive at their destinations with correct values. Silent data corruption (wrong values, empty strings, zeros) is harder to detect than missing code.

## Core Principle

**Trace the data, not just the code.** A function can exist and be called but receive wrong inputs. An FBS field can be defined but never populated. A variable can be initialized but with a placeholder value.

---

## Field Verification Commands

### Verify: callId

```bash
# 1. FBS Schema - field exists?
grep "call_id" worker/fbs/transport.fbs
# Expected: call_id: string;  (in Options table)
# Failure: No output
```
- [ ] FBS field exists

```bash
# 2. Node.js Population - Router.ts populates?
grep -n "callId" node/src/Router.ts | grep -E "appData|OptionsT"
# Expected: (appData as any)?.callId ?? null
# Failure: No match or hardcoded value
```
- [ ] Node.js populates field

```bash
# 3. C++ Extraction - Transport.cpp extracts?
grep -A2 "options->callId()" worker/src/RTC/Transport.cpp
# Expected: this->lively.callId.assign(options->callId()->str());
# Failure: No match or wrong assignment
```
- [ ] C++ extracts field

```bash
# 4. Feature Usage - used in binlog filename?
grep "lively.callId\|callId" worker/src/RTC/Transport.cpp | grep -v "//"
# Expected: Multiple matches including ConsumerFileName lambda
# Failure: No usage in feature code
```
- [ ] Field used in feature

---

### Verify: userId

```bash
# 1. FBS Schema - field exists in ProduceRequest?
grep "user_id" worker/fbs/transport.fbs
# Expected: user_id: string;  (in ProduceRequest table, around line 53)
# Failure: No output
```
- [ ] FBS field exists

```bash
# 2. Node.js Population - Transport.ts populates?
grep -n "userId\|user_id" node/src/Transport.ts | grep -E "Offset|add"
# Expected: userIdOffset = builder.createString(userId)
#           ProduceRequest.addUserId(builder, userIdOffset)
# Failure: No addUserId call
```
- [ ] Node.js populates field

```bash
# 3. C++ Extraction - Producer.cpp extracts?
grep -A2 "data->userId()" worker/src/RTC/Producer.cpp
# Expected: userId = data->userId()->str();
# Failure: No match or userId = "" hardcoded
```
- [ ] C++ extracts field

```bash
# 4. Feature Usage - used in ProducerFileName?
grep -n "userId" worker/src/RTC/Producer.cpp | grep -E "binLog|FileName|lambda"
# Expected: userId captured in lambda for ProducerFileName
# Failure: userId not in lambda capture
```
- [ ] Field used in feature

---

### Verify: clientReferrer

```bash
# 1. FBS Schema - field exists in BOTH tables?
grep -n "client_referrer" worker/fbs/transport.fbs
# Expected: 2 matches - Options (line ~178) AND ProduceRequest (line ~54)
# Failure: Only 1 match or no matches
```
- [ ] FBS field exists in Options
- [ ] FBS field exists in ProduceRequest

```bash
# 2a. Node.js Population - Router.ts (for Transport)?
grep -n "clientReferrer" node/src/Router.ts | head -5
# Expected: (appData as any)?.clientReferrer ?? null
# Failure: No match
```
- [ ] Node.js populates in Router.ts

```bash
# 2b. Node.js Population - Transport.ts (for Producer)?
grep -n "clientReferrer" node/src/Transport.ts | grep -E "Offset|add"
# Expected: clientReferrerOffset and addClientReferrer
# Failure: No addClientReferrer call
```
- [ ] Node.js populates in Transport.ts

```bash
# 3a. C++ Extraction - Transport.cpp?
grep -A2 "options->clientReferrer()" worker/src/RTC/Transport.cpp
# Expected: clientReferrer.assign(options->clientReferrer()->str());
# Failure: No extraction
```
- [ ] C++ extracts in Transport

```bash
# 3b. C++ Extraction - Producer.cpp?
grep -A2 "data->clientReferrer()" worker/src/RTC/Producer.cpp
# Expected: clientReferrer = data->clientReferrer()->str();
# Failure: No extraction
```
- [ ] C++ extracts in Producer

---

### Verify: producerStats

```bash
# 1. FBS Schema - field exists?
grep "producer_stats" worker/fbs/transport.fbs
# Expected: producer_stats: bool = false;  (in Options table)
# Failure: No output
```
- [ ] FBS field exists

```bash
# 2. Node.js Population - Router.ts?
grep -n "producerStats" node/src/Router.ts
# Expected: (appData as any)?.producerStats ?? false
# Failure: No match or hardcoded false
```
- [ ] Node.js populates field

```bash
# 3. C++ Extraction and Usage - Transport.cpp?
grep -A3 "options->producerStats()" worker/src/RTC/Transport.cpp
# Expected:
#   if (options->producerStats())
#   {
#       this->lastProducerStatsReport = DepLibUV::GetTimeMs();
# Failure: No condition check or wrong assignment
```
- [ ] C++ extracts and uses field

```bash
# 4. Feature Activation - OnTimer checks?
grep -n "lastProducerStatsReport" worker/src/RTC/Transport.cpp | head -5
# Expected: Initialization in constructor AND 10000ms check in OnTimer
# Failure: Missing either initialization or check
```
- [ ] Feature activates based on field

---

### Verify: peerId, mirrorId, streamName

```bash
# All three follow same pattern - verify in batch:

# 1. FBS Schema
grep -E "peer_id|mirror_id|stream_name" worker/fbs/transport.fbs
# Expected: 3 lines in Options table
```
- [ ] All three FBS fields exist

```bash
# 2. Node.js Population
grep -E "peerId|mirrorId|streamName" node/src/Router.ts | grep "appData"
# Expected: 3 lines with (appData as any)?.fieldName ?? null
```
- [ ] Node.js populates all three

```bash
# 3. C++ Extraction
grep -E "options->(peerId|mirrorId|streamName)" worker/src/RTC/Transport.cpp
# Expected: 3 extraction lines
```
- [ ] C++ extracts all three

---

## Binlog Filename Verification

### Producer Filename: `ms_p_<userId>_<callId>_<producerId>_...`

```bash
# Verify ProducerFileName function signature
grep -A5 "ProducerFileName" worker/include/LivelyBinLogs.hpp
# Expected: Parameters include userId, callId, producerId, clientReferrer
```

```bash
# Verify lambda captures correct variables
grep -B2 -A5 "ProducerFileName" worker/src/RTC/Producer.cpp
# Expected: [callId, producerId, userId, clientReferrer] capture
# Failure: Missing any of the 4 variables
```

```bash
# Verify each captured variable is extracted from FBS
grep -E "(userId|callId|producerId|clientReferrer) =" worker/src/RTC/Producer.cpp | head -10
# Expected: Each assigned from data->field() or parameter
# Failure: Any assigned to "" or hardcoded
```

### Consumer Filename: `<clientReferrer>/ms_c_<callId>_<consumerId>_...`

```bash
# Verify ConsumerFileName in Transport.cpp
grep -B2 -A5 "ConsumerFileName" worker/src/RTC/Transport.cpp
# Expected: [clientReferrer, callId] capture
```

```bash
# Verify clientReferrer and callId sources
grep -E "(clientReferrer|callId).*=" worker/src/RTC/Transport.cpp | head -10
# Expected: Both extracted from options->
# Failure: Either empty or hardcoded
```

---

## Silent Failure Modes

### Mode 1: FBS Field Missing
**Symptom**: Field always empty in C++, no error
**Detection**:
```bash
grep "call_id" worker/fbs/transport.fbs || echo "MISSING"
```
**Expected**: Field definition
**Failure**: "MISSING" output

### Mode 2: Node.js Doesn't Populate
**Symptom**: FBS field exists but value not sent
**Detection**:
```bash
grep "addCallId\|call_id" node/src/Router.ts
```
**Expected**: addCallId or call_id assignment
**Failure**: No match

### Mode 3: C++ Doesn't Extract
**Symptom**: Value sent but not stored
**Detection**:
```bash
grep "options->callId()" worker/src/RTC/Transport.cpp
```
**Expected**: Extraction and assignment
**Failure**: No match

### Mode 4: Wrong Variable in Lambda
**Symptom**: Filename has wrong/empty component
**Detection**:
```bash
grep -A3 "ProducerFileName" worker/src/RTC/Producer.cpp | grep -o "\[.*\]"
```
**Expected**: `[callId, producerId, userId, clientReferrer]`
**Failure**: Missing any variable

### Mode 5: Parameter Order Wrong
**Symptom**: Filename components in wrong positions
**Detection**:
```bash
# Compare v3-lively parameter order
git show origin/v3-lively:worker/src/RTC/Producer.cpp | grep -A1 "ProducerFileName"
grep -A1 "ProducerFileName" worker/src/RTC/Producer.cpp
```
**Expected**: Same parameter order
**Failure**: Different order

---

## Complete Field Audit Checklist

| Field | FBS | Node.js | C++ Extract | Feature Use | Status |
|-------|-----|---------|-------------|-------------|--------|
| callId | [ ] | [ ] | [ ] | [ ] | |
| peerId | [ ] | [ ] | [ ] | [ ] | |
| mirrorId | [ ] | [ ] | [ ] | [ ] | |
| streamName | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer (Options) | [ ] | [ ] | [ ] | [ ] | |
| clientReferrer (ProduceRequest) | [ ] | [ ] | [ ] | [ ] | |
| producerStats | [ ] | [ ] | [ ] | [ ] | |
| userId | [ ] | [ ] | [ ] | [ ] | |

---

## Invocation

```
/data-flow-auditor <field_name>
```

Or for complete audit:
```
/data-flow-auditor --all-lively-fields
```
