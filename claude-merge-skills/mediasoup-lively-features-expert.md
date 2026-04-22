# Mediasoup Lively Features Expert (Catch-All)

**Last Updated**: 2026-03-11

You are the catch-all expert for Lively-specific features not covered by other specialized experts. You handle miscellaneous Lively modifications and ensure complete audit coverage.

## Role

This expert serves two purposes:
1. **Coverage guarantee** — Any Lively-modified file not assigned to a specialist comes here
2. **General expertise** — Knowledge of all Lively features for cross-cutting concerns

---

## Silent Failure Modes

### Mode 1: CLI Option Removed
**Symptom**: Deployment scripts fail silently or use wrong defaults
**Cause**: Lively CLI option removed during merge
**Detection**:
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -c "binStatsDisabled\|binStatsPath\|logDevLevel"
grep -c "binStatsDisabled\|binStatsPath\|logDevLevel" worker/src/main.cpp
```
**Expected**: Same count
**Failure**: Current count lower

### Mode 2: Settings Member Removed
**Symptom**: Feature silently uses default/disabled state
**Cause**: Settings struct member removed
**Detection**:
```bash
git show origin/v3-lively:worker/include/Settings.hpp | grep -E "logBinStats|logDevLevel|logTraceEnabled"
grep -E "logBinStats|logDevLevel|logTraceEnabled" worker/include/Settings.hpp
```
**Expected**: All members present
**Failure**: Missing members

### Mode 3: Type Export Removed
**Symptom**: Consumer TypeScript code fails to compile
**Cause**: Public type no longer exported from index.ts
**Detection**:
```bash
git show origin/v3-lively:node/src/index.ts | grep "export" | wc -l
grep "export" node/src/index.ts | wc -l
```
**Expected**: Same or higher count
**Failure**: Lower count

### Mode 4: Lively Comment Block Deleted
**Symptom**: Code works but loses documentation/context
**Cause**: Merge deleted Lively comments
**Detection**:
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "// Lively\|// RND-\|// PM-\|// Amir"
grep -c "// Lively\|// RND-\|// PM-\|// Amir" worker/src/RTC/Transport.cpp
```
**Expected**: Same or higher count
**Failure**: Lower count

### Mode 5: Header Include Removed
**Symptom**: Compilation error or missing functionality
**Cause**: Lively header removed from includes
**Detection**:
```bash
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep "#include.*Lively"
grep "#include.*Lively" worker/src/RTC/Transport.cpp
```
**Expected**: Same includes
**Failure**: Missing includes

---

## Mechanical Audit Checklist

### 1. CLI Arguments (main.cpp)

#### 1.1 Lively Options Present
```bash
grep -E "binStatsDisabled|binStatsPath|logDevLevel|logTraceEnabled" worker/src/main.cpp
```
**Expected**: All 4 options handled
- [ ] binStatsDisabled option
- [ ] binStatsPath option
- [ ] logDevLevel option
- [ ] logTraceEnabled option

#### 1.2 Option Letters Match v3-lively
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -E "case '[a-zA-Z]':" | sort
grep -E "case '[a-zA-Z]':" worker/src/main.cpp | sort
```
- [ ] All option letters identical

#### 1.3 Long Options Match
```bash
git show origin/v3-lively:worker/src/main.cpp | grep -A1 "logBinStatsDisabled\|logBinStatsPath\|logDevLevel"
grep -A1 "logBinStatsDisabled\|logBinStatsPath\|logDevLevel" worker/src/main.cpp
```
- [ ] Long option names identical

---

### 2. Settings (Settings.hpp/cpp)

#### 2.1 Settings Members Present
```bash
grep -E "logBinStatsDisabled|logBinStatsPath|logDevLevel|logTraceEnabled" worker/include/Settings.hpp
```
**Expected**: All members declared
- [ ] logBinStatsDisabled member
- [ ] logBinStatsPath member
- [ ] logDevLevel member
- [ ] logTraceEnabled member

#### 2.2 Settings Initialization
```bash
grep -E "logBinStatsDisabled|logBinStatsPath|logDevLevel|logTraceEnabled" worker/src/Settings.cpp
```
**Expected**: All members initialized/handled
- [ ] All settings properly initialized

---

### 3. Node.js Exports (index.ts)

#### 3.1 Type Exports
```bash
git show origin/v3-lively:node/src/index.ts | grep "^export" | sort
grep "^export" node/src/index.ts | sort
```
- [ ] All v3-lively exports present

#### 3.2 Lively-Specific Types
```bash
grep -E "ProducerStatEvent|LogDevLevel" node/src/index.ts
```
- [ ] Lively types exported

---

### 4. Worker Types (WorkerTypes.ts)

#### 4.1 Settings Types
```bash
git show origin/v3-lively:node/src/WorkerTypes.ts | grep -E "binStatsDisabled|binStatsPath|logDevLevel"
grep -E "binStatsDisabled|binStatsPath|logDevLevel" node/src/WorkerTypes.ts
```
- [ ] All settings in WorkerSettings type

#### 4.2 Worker Methods
```bash
git show origin/v3-lively:node/src/Worker.ts | grep -E "logOpen|logRotate" | head -5
grep -E "logOpen|logRotate" node/src/Worker.ts | head -5
```
- [ ] Lively worker methods present

---

### 5. Lively Headers

#### 5.1 Lively.hpp Present
```bash
ls -la worker/include/Lively.hpp
```
- [ ] Lively.hpp exists

#### 5.2 LivelyBinLogs.hpp Present
```bash
ls -la worker/include/LivelyBinLogs.hpp
```
- [ ] LivelyBinLogs.hpp exists

#### 5.3 Lively Includes in Transport
```bash
grep "#include.*Lively" worker/src/RTC/Transport.cpp
```
- [ ] Lively headers included

---

### 6. Lively Comments Preservation

#### 6.1 RND Comments
```bash
grep -c "// RND-" worker/src/RTC/Transport.cpp worker/src/RTC/Producer.cpp worker/src/RTC/WebRtcTransport.cpp
```
- [ ] RND comments preserved

#### 6.2 Lively Feature Comments
```bash
grep -c "// Lively\|// Amir\|// PM-" worker/src/RTC/Transport.cpp
```
- [ ] Feature comments preserved

---

### 7. Build System

#### 7.1 Lively Source Files in Meson
```bash
grep -E "LivelyBinLogs|Lively.cpp" worker/meson.build
```
- [ ] Lively source files listed

#### 7.2 Lively Headers in Include Path
```bash
grep "include" worker/meson.build | head -10
```
- [ ] Include paths correct

---

### 8. Cross-Feature Verification

#### 8.1 Data Flow: appData → binlog filename
```bash
# Trace callId from Node.js to binlog
grep "callId" node/src/Router.ts | head -2
grep "lively.callId\|options->callId" worker/src/RTC/Transport.cpp | head -2
grep "callId" worker/src/RTC/Producer.cpp | head -2
```
- [ ] callId flows from appData to binlog

#### 8.2 Event Flow: C++ → Node.js
```bash
# Trace PRODUCER_STATS notification
grep "PRODUCER_STATS" worker/fbs/notification.fbs
grep "PRODUCER_STATS" worker/src/RTC/Producer.cpp | head -2
grep "PRODUCER_STATS" node/src/Producer.ts | head -2
```
- [ ] PRODUCER_STATS event chain complete

#### 8.3 Settings Flow: CLI → C++ feature
```bash
# Trace binStatsDisabled
grep "binStatsDisabled" worker/src/main.cpp | head -2
grep "logBinStatsDisabled" worker/include/Settings.hpp
grep "logBinStatsDisabled" worker/src/RTC/Transport.cpp | head -2
```
- [ ] Settings flow from CLI to feature

---

## Feature Expert Routing

| Pattern Found | Route To |
|--------------|----------|
| binLog, FillBinLogStats, rtpStreamBinLogRecord | binlogs-expert |
| EmitProducerStats, lastProducerStatsReport | stats-expert |
| MS_DEBUG_DEV, LogDevLevel, MSLOG_OPEN | textlogs-expert |
| appData, callId, peerId, userId | appdata-expert |
| ShmTransport, TRANSCODE | transcode-features-expert |
| **Other Lively code** | **this expert** |

---

## Quick Discovery Commands

```bash
# Find all files with Lively modifications
grep -rl "Lively\|RND-\|Amir Pauker\|PM-156" worker/src worker/include node/src

# Find all Lively types used
grep -rh "LivelyBinLog\|LogDevLevel\|AppData\|StatsBinLog" worker/src | sort -u

# Find all Lively macros used
grep -rh "MS_DEBUG_DEV\|MS_WARN_DEV\|MS_DEBUG_TAG_LIVELY" worker/src | head -10

# Count Lively markers per file
for f in worker/src/RTC/*.cpp; do echo "$f: $(grep -c 'Lively\|RND-' $f)"; done
```

---

## Invocation

```
/mediasoup-lively-features-expert audit <file>
```

Or for general audit:
```
/mediasoup-lively-features-expert audit --uncovered
```

Performs catch-all Lively features audit.
