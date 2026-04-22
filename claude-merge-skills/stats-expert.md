# Periodic Producer Stats Expert

**Last Updated**: 2026-03-11

You are the expert on Lively's periodic producer stats feature (RND-568). This feature emits per-producer statistics to Node.js every 10 seconds.

## Feature Overview

Periodic producer stats emits bitrate, dimensions, and frame data from each producer to the Node.js layer, where it's exposed as a `producerstats` event.

## Activation Chain

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

---

## Silent Failure Modes

### Mode 1: producerStats Option Not in FBS
**Symptom**: Stats never emitted even with appData.producerStats=true
**Cause**: FBS schema missing the field
**Detection**:
```bash
grep "producer_stats" worker/fbs/transport.fbs
```
**Expected**: `producer_stats: bool = false;`
**Failure**: No output

### Mode 2: Node.js Doesn't Populate producerStats
**Symptom**: FBS field exists but always false
**Cause**: Router.ts doesn't extract from appData
**Detection**:
```bash
grep "producerStats" node/src/Router.ts | head -3
```
**Expected**: `(appData as any)?.producerStats ?? false`
**Failure**: No match or hardcoded `false`

### Mode 3: lastProducerStatsReport Never Initialized
**Symptom**: 10-second check always fails (0 + 10000 < nowMs is always true, but never triggers)
**Cause**: Initialization code missing or in wrong condition
**Detection**:
```bash
grep -A3 "producerStats()" worker/src/RTC/Transport.cpp
```
**Expected**:
```cpp
if (options->producerStats())
{
    this->lastProducerStatsReport = DepLibUV::GetTimeMs();
}
```
**Failure**: No match or assignment outside condition

### Mode 4: OnTimer Check Missing
**Symptom**: Initialization happens but stats never emitted
**Cause**: 10-second interval check removed or broken
**Detection**:
```bash
grep "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep "10000"
```
**Expected**: `this->lastProducerStatsReport + 10000 < nowMs` or similar
**Failure**: No match

### Mode 5: EmitProducerStats Not Called
**Symptom**: Check passes but nothing emitted
**Cause**: Loop over mapProducers missing or EmitProducerStats call removed
**Detection**:
```bash
grep -A5 "lastProducerStatsReport + 10000" worker/src/RTC/Transport.cpp
```
**Expected**: Loop with `producer->EmitProducerStats()`
**Failure**: No EmitProducerStats call

### Mode 6: Node.js Handler Missing
**Symptom**: C++ emits but Node.js never fires event
**Cause**: PRODUCER_STATS case missing in Producer.ts
**Detection**:
```bash
grep "PRODUCER_STATS" node/src/Producer.ts
```
**Expected**: `case Event.PRODUCER_STATS:`
**Failure**: No match

### Mode 7: Event Not Emitted
**Symptom**: Handler exists but no event reaches application
**Cause**: safeEmit call missing or wrong event name
**Detection**:
```bash
grep "producerstats" node/src/Producer.ts
```
**Expected**: `this.safeEmit('producerstats', stats);`
**Failure**: No match or wrong event name

---

## Mechanical Audit Checklist

### Transport.cpp

#### 1.1 FBS Field Extraction
```bash
grep -B2 -A3 "options->producerStats()" worker/src/RTC/Transport.cpp
```
**Expected output**:
```cpp
// Lively-specific: enable periodic producer stats emission (RND-568)
if (options->producerStats())
{
    this->lastProducerStatsReport = DepLibUV::GetTimeMs();
}
```
- [ ] producerStats extracted from options
- [ ] lastProducerStatsReport initialized conditionally

#### 1.2 OnTimer 10-Second Check
```bash
grep -B2 -A8 "lastProducerStatsReport" worker/src/RTC/Transport.cpp | grep -A8 "10000"
```
**Expected**:
- Check `lastProducerStatsReport + 10000 < nowMs`
- Loop over `mapProducers`
- Call `EmitProducerStats()`
- Update `lastProducerStatsReport = nowMs`
- [ ] 10-second interval check present
- [ ] Iterates mapProducers
- [ ] Calls EmitProducerStats()
- [ ] Updates timestamp after emission

#### 1.3 No TRANSCODE Guard
```bash
grep -B5 "EmitProducerStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: `0`
- [ ] No TRANSCODE guard around stats emission

---

### Producer.cpp

#### 2.1 EmitProducerStats Implementation
```bash
grep -A20 "void Producer::EmitProducerStats" worker/src/RTC/Producer.cpp | head -25
```
**Expected**:
- Method exists
- Iterates `rtpStreamByEncodingIdx`
- Builds FBS notification
- Emits via channelNotifier
- [ ] Method implemented
- [ ] Iterates RTP streams
- [ ] Builds FBS message
- [ ] Emits notification

#### 2.2 FBS Message Fields
```bash
grep -A15 "EmitProducerStats" worker/src/RTC/Producer.cpp | grep -E "Bitrate|Width|Height|Frames|ssrc"
```
**Expected**: All 5 fields extracted and included
- [ ] ssrc included
- [ ] bitrate included
- [ ] width included
- [ ] height included
- [ ] frames included

---

### Producer.ts (Node.js)

#### 3.1 Notification Handler
```bash
grep -B2 -A15 "PRODUCER_STATS" node/src/Producer.ts
```
**Expected**:
- `case Event.PRODUCER_STATS:`
- Creates ProducerStatsNotification object
- Extracts entries
- Builds stats array
- Emits event
- [ ] Handler case exists
- [ ] Extracts notification body
- [ ] Builds stats array
- [ ] Emits 'producerstats' event

#### 3.2 Event Emission
```bash
grep "producerstats" node/src/Producer.ts
```
**Expected**: `this.safeEmit('producerstats', stats);`
- [ ] Correct event name
- [ ] Uses safeEmit

---

### FBS Schema

#### 4.1 ProducerStatsNotification Table
```bash
grep -A10 "ProducerStatEntry\|ProducerStatsNotification" worker/fbs/producer.fbs
```
**Expected**:
```flatbuffers
table ProducerStatEntry {
    now_ms: uint64;
    ssrc: uint32;
    bitrate: uint32;
    width: uint32;
    height: uint32;
    frames: uint32;
}

table ProducerStatsNotification {
    entries: [ProducerStatEntry] (required);
}
```
- [ ] ProducerStatEntry table defined
- [ ] All fields present (now_ms, ssrc, bitrate, width, height, frames)
- [ ] ProducerStatsNotification table defined

#### 4.2 Notification Enum
```bash
grep "PRODUCER_STATS" worker/fbs/notification.fbs
```
**Expected**: `PRODUCER_STATS,` in Event enum
- [ ] PRODUCER_STATS in notification enum

---

## File Ownership

| File | Stats-Related Code |
|------|-------------------|
| `worker/src/RTC/Transport.cpp` | Timer check, EmitProducerStats() calls |
| `worker/src/RTC/Producer.cpp` | EmitProducerStats() implementation |
| `worker/include/RTC/Transport.hpp` | lastProducerStatsReport member |
| `worker/include/RTC/Producer.hpp` | EmitProducerStats() declaration |
| `worker/fbs/producer.fbs` | ProducerStatsNotification table |
| `worker/fbs/notification.fbs` | PRODUCER_STATS enum value |
| `node/src/Producer.ts` | PRODUCER_STATS handler, event emission |
| `node/src/ProducerTypes.ts` | ProducerStatEvent type |

---

## TRANSCODE Guard Rules

The periodic stats feature has **NO TRANSCODE guards**. It runs in production.

If any TRANSCODE guard is found around:
- `lastProducerStatsReport` initialization
- 10-second interval check in OnTimer
- `EmitProducerStats()` calls
- `EmitProducerStats()` implementation

→ This is a **REGRESSION**

---

## Invocation

```
/stats-expert audit
```

Performs complete periodic stats system audit.
