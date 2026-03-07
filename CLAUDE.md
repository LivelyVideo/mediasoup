# Lively Fork of mediasoup - AI Assistant Instructions

## PART ONE: GOVERNANCE RULES (Read First)

These rules govern all work on this codebase. They are **bright-line compliance rules**, not risk assessments. Do not rationalize exceptions.

### Rule 1: CONTRACTS MATCH V3-LIVELY (highest priority)

External contracts must match `origin/v3-lively` exactly. External contracts are:
- Command-line option names and their short-option letters (e.g., `'b'` for binStatsDisabled)
- Node.js API: method signatures, event names, event payloads, type exports
- FBS wire format: enum values and their integer assignments, table field names, notification event IDs
- Environment variable names

**Copy the exact value from v3-lively. Do not derive, infer, or "improve" it.**

### Rule 2: NO REGRESSIONS

Any behavioral change from v3-lively is a regression unless explicitly approved. This includes:
- Log output format changes (adding/removing characters, changing delimiters)
- Log destination changes (stdout vs log file)
- Thread safety changes (removing `thread_local` qualifiers)
- Cleanup/shutdown behavior changes (removing flush-on-abort)
- Any observable difference in output, timing, or side effects

**Every regression is equally important.** Do not classify regressions as "minor" or "low-risk."

### Rule 3: ESCALATE WHEN REPLICATION IS NON-OBVIOUS

When preserving v3-lively behavior does not have an obvious, direct solution, you MUST escalate:

```
ESCALATION: [one-line summary of what cannot be replicated]

v3-lively behavior: [exact output, data fields, format, purpose]

Obstacle: [why replication is non-obvious - missing API, format change, etc.]

Awaiting your decision before proceeding.
```

Then **wait** for human decision. Do not implement substitutes, approximations, or reduced versions.

### Rule 4: NO UNNECESSARY CHANGES

Do not change function signatures, type definitions, or public APIs unless **required to compile and run the current task**. Do not:
- Add parameters to existing functions
- Widen or narrow existing types
- Rename existing fields or methods
- Change return types

If a signature change seems necessary, stop and state: (a) what won't compile without it, (b) the exact proposed change. Wait for approval.

### Rule 5: VERIFY AGAINST V3-LIVELY

After implementing each file, run `git show origin/v3-lively:<filepath>` and diff every external-facing value character-by-character:
- Format strings
- Field names and field counts in log messages
- Type names, event names, option letters
- Enum values and their integer assignments

If any value differs, fix it (Rule 2) or escalate (Rule 3).

### Rule 6: INTERNALS FOLLOW UPSTREAM (when safe)

Internal implementation should follow upstream patterns:
- Use FBS for request/notification bodies (not raw JSON)
- Use `absl::flat_hash_map` (not `std::map`)
- Follow upstream class hierarchies and include patterns

**Exception**: When upstream patterns would cause a regression (Rules 1-2), preserve the v3-lively pattern. When uncertain whether an upstream pattern is safe, escalate.

---

## PART TWO: FILE RESTRICTIONS

### Restricted Files (Do NOT Modify Without Approval)

These files require explicit human approval before any modification:

| File | Sensitivity | Why restricted |
|------|-------------|----------------|
| `transport.fbs` | Highest | Wire format - changes break Node.js ↔ C++ compatibility |
| `Transport.cpp` | High | Contains all Lively timer logic, binlog init, producer stats |
| `Transport.hpp` | High | Lively member declarations |
| `WebRtcTransport.cpp` | High | Sets `producerBinLogEnabled`, Lively initialization |
| `Router.ts` | Medium | Transport factory, appData forwarding |

**During audits**: These files require the **most scrutiny** because they contain Lively-specific initialization logic that upstream lacks. Reading and examining these files is required; modifying them requires approval.

### Other Restrictions

- **Do NOT remove** any `#ifdef TRANSCODE` guards from SHM-related code
- **Do NOT add** `#ifdef TRANSCODE` guards around binary logging, text logging, or producer stats code (see TRANSCODE Guard Rules in Part Six)
- **Do NOT modify** files outside the current task scope without asking first
- **appData fields** must arrive at the same C++ destinations as in v3-lively (serialization format may differ)

---

## PART THREE: TASK BOUNDARIES

### Defining "Current Task"

The user defines each task explicitly (e.g., "fix the binlog regression", "audit producer stats"). If task boundaries are unclear, ask:

> "Should this task include [specific file/feature], or is that out of scope?"

### When Scope Expands

If completing a task requires changes outside its defined scope:
1. Stop work on the out-of-scope change
2. Report: "Completing [task] requires modifying [file/component] because [reason]"
3. Wait for approval to expand scope or receive alternative direction

---

## PART FOUR: REGRESSION AUDIT PROCEDURE

Run this procedure after upstream merges or when requested.

### Scope of Audits

Audit these files:
1. Files with merge conflicts
2. Files containing Lively-specific features (see Part Five)
3. Files in the restricted list (Part Two)

Files with upstream-only changes and no Lively modifications do not require Lively regression audits.

### Step 1: Trace Activation Chains

For each Lively feature, verify the complete chain from configuration → initialization → execution → output.

**Verify three things for each component**:
- **Exists**: Member/method/type is declared
- **Activated**: Something writes to it, calls it, or sets it (verify the setter)
- **Sourced correctly**: Values come from the right place (not hardcoded placeholders)

**Bad verification**: "StatsBinLog member exists in Transport.hpp"
**Bad verification**: "`binLog.InitLog()` is called in Producer constructor"
**Good verification**: "Producer constructor calls `binLog.InitLog()` with lambda capturing `userId` extracted from `ProduceRequest.user_id` FBS field"

### Step 2: Diff Lively-Modified Methods

Line-by-line diff against v3-lively:
- **Constructors**: `Transport`, `WebRtcTransport`, `PlainTransport`, `PipeTransport`, `Producer`
- **Timer callbacks**: `Transport::OnTimer`
- **Request handlers**: Transport produce handler
- **Methods with Lively comments**: `// RND-568`, `// PM-1560`, `// Amir Pauker`

One missing initialization line can silently disable an entire subsystem.

### Step 3: Identify Silent Failure Modes

For each feature, ask: "Under what conditions would this silently fail?"

**Three levels of silent failure**:
| Level | Description | Example |
|-------|-------------|---------|
| Chain broken | Init never called | `InitLog()` missing → timer fires, data goes nowhere |
| Values wrong | Init called with bad data | `userId = "0"` → files exist but contain wrong data |
| Feature absent | All code removed | No member, no timer logic, no handler → nothing happens |

Verify none of these conditions exist.

### Step 4: Verify Data Values

Trace **data flowing through**, not just execution paths:
- For lambdas: verify captured variables have correct sources
- For function calls: verify parameters come from the right place
- Hardcoded placeholders (`userId = "0"`, `clientReferrer = ""`) where v3-lively extracted real values are **critical regressions**

Use the **Lively Data Field Reference** (Part Five) as a checklist.

### Step 5: Audit FBS Schema Completeness

For each FBS table that replaced JSON:
1. Read v3-lively code that parsed the JSON version
2. List every field extracted via `json.find()`, `json["key"]`, or helpers
3. Verify each field exists in the FBS table
4. Verify C++ code reads the field from FBS
5. Verify Node.js populates the field when sending

### Step 6: Diff Log Messages

Compare all Lively log calls (`MS_DEBUG_TAG`, `MS_WARN_TAG`, `MS_DEBUG_DEV`, `MS_WARN_DEV`) against v3-lively. Missing or changed log messages are behavioral regressions.

### Step 7: Document Results

Audit is complete when all Lively features have been verified per steps 1-8. Document each feature:

```
[Feature Name]: PASS | FAIL | ESCALATED
  - Activation chain: [verified/broken at step X]
  - Data values: [correct/placeholder at field Y]
  - Log messages: [match/differ at location Z]
  - TRANSCODE guards: [correct/incorrect at location W]
```

### Step 8: Audit TRANSCODE Guards

Compare `#ifdef TRANSCODE` usage between v3-lively and current branch:

```bash
# Find all TRANSCODE guards in v3-lively
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "TRANSCODE"

# Find all TRANSCODE guards in current
grep -n "TRANSCODE" worker/src/RTC/Transport.cpp
```

For each file containing Lively code, verify:
- **No new guards added** around binary logging, text logging, or producer stats
- **No guards removed** from SHM-related code
- Guard count matches v3-lively (±0 for most files)

**Red flags** (these are REGRESSIONS):
- TRANSCODE guard around `binLog`, `binLogTimer`, `rtpStreamBinLogRecord`
- TRANSCODE guard around `InitLog()`, `DeinitLog()`, `FillBinLogStats()`
- Missing TRANSCODE guard around `ShmTransport`, `ShmConsumer`

### Error Recovery

If a regression is discovered after implementation:
1. Stop further changes
2. Document the regression with location and impact
3. Await human decision on revert vs. patch

---

## PART FIVE: LIVELY-SPECIFIC FEATURES

Features not present in upstream mediasoup. **Each must be verified during audits.**

### Feature 1: Text Logging

- **Requests**: `WORKER_MSLOG_OPEN`, `WORKER_MSLOG_ROTATE`
- **Notification**: `LOGGER_WRITE_FAILED`
- **Purpose**: Per-worker log files

### Feature 2: Binary Stats Logging

Produces `ms_p_*` (producer) and `ms_c_*` (consumer) binary stat files.

**Controlled by**: `binStatsDisabled`, `binStatsPath` settings

**Activation chain** (every link required):
1. `WebRtcTransport` constructor → sets `producerBinLogEnabled = true`
2. `Transport` constructor → calls `consumersBinLog.InitLog(...)` with `ConsumerFileName` lambda
3. `Producer` constructor → calls `binLog.InitLog(...)` (only if `producerBinLogEnabled`)
4. `Transport::OnTimer` → calls `FillBinLogStats()` every 2 seconds
5. File I/O implementation in `LivelyBinLogs.cpp` (included only in transcode build via meson.build)

**IMPORTANT**: Usage sites (`InitLog`, `FillBinLogStats`, timer callbacks, `rtpStreamBinLogRecord` init/cleanup) must NOT be guarded by `#ifdef TRANSCODE` - they compile in both builds.

### Feature 3: Developer Log Levels

- **Enum**: `LogDevLevel` (`LOG_DEV_DEBUG=3`, `LOG_DEV_WARN=2`, `LOG_DEV_NONE=0`)
- **Flag**: `logTraceEnabled`

### Feature 4: SHM Transport

- **Request**: `ROUTER_CREATE_SHMTRANSPORT`
- **Build**: Transcode flavor only

### Feature 5: Periodic Producer Stats (RND-568)

Every 10 seconds, emits per-producer stats (bitrate, width, height, frames).

**Activation chain**:
1. `Transport` constructor → `lastProducerStatsReport = DepLibUV::GetTimeMs()` when `producer_stats` option is `true`
2. `Transport::OnTimer` → 10-second interval check → iterates `mapProducers` → calls `producer->EmitProducerStats()`
3. `Producer::EmitProducerStats()` → iterates `rtpStreamByEncodingIdx`, builds `ProducerStatsNotification`, emits via `channelNotifier->Emit()`
4. Node.js `Producer.ts` → handles `PRODUCER_STATS` notification, emits `producerstats` event with `ProducerStatEvent[]`

### Lively Data Field Reference

These fields flow from protocol messages to Lively features. **Every field must have an FBS schema entry.**

**Transport Creation** (FBS `Options` table in `transport.fbs`):

| Field | v3-lively source | FBS field | Used by |
|-------|-----------------|-----------|---------|
| callId | `appData["callId"]` | `Options.call_id` | Binlog filenames, diagnostics |
| peerId | `appData["peerId"]` | `Options.peer_id` | Diagnostics |
| mirrorId | `appData["mirrorId"]` | `Options.mirror_id` | Diagnostics |
| streamName | `appData["streamName"]` | `Options.stream_name` | Diagnostics |
| clientReferrer | `appData["clientReferrer"]` | `Options.client_referrer` | Consumer binlog subdirectory |
| producerStats | `appData["producerStats"]` | `Options.producer_stats` | Enables Feature 5 |

**Produce Request** (FBS `ProduceRequest` table in `transport.fbs`):

| Field | v3-lively source | FBS field | Used by |
|-------|-----------------|-----------|---------|
| userId | `GetUserIdFromAppData()` | `ProduceRequest.user_id` | Producer binlog filename |
| clientReferrer | `appData["clientReferrer"]` | `ProduceRequest.client_referrer` | Producer binlog subdirectory |

**Migration note**: These tables reflect the target FBS state. During migration, verify both JSON extraction (if still present) and FBS extraction.

---

## PART SIX: REFERENCE MATERIAL

### Glossary

| Term | Definition |
|------|------------|
| **Activation chain** | The sequence of calls from configuration to output that enables a feature |
| **External contract** | Any interface visible to code outside this repository (CLI, API, wire format) |
| **Wire format** | The FBS binary format used for Node.js ↔ C++ communication |
| **Binlog** | Binary log files (`ms_p_*`, `ms_c_*`) containing stats data |
| **appData** | Application-specific data passed through mediasoup APIs |
| **v3-lively** | The `origin/v3-lively` branch - the production reference |

### Repository Structure

```
origin:     LivelyVideo/mediasoup (this fork)
upstream:   versatica/mediasoup (original)

Branches:
  v3-lively     Production branch (Lively's "master")
  RND-*         Feature/fix/merge branches (from v3-lively, merge to v3-lively)
  master        Exists but unused
```

### Build Flavors

| Flavor | Guard | Status | Use |
|--------|-------|--------|-----|
| Without transcode | (default) | Production | SFU package |
| With transcode | `#ifdef TRANSCODE` | Secondary | SHM transport, transcoding |

### TRANSCODE Guard Rules

`#ifdef TRANSCODE` guards control what compiles into each build flavor. **Incorrect guards are silent regressions** - features disappear without errors.

**MUST be guarded by TRANSCODE** (transcode-only features):
- `#include "RTC/ShmConsumer.hpp"` and `#include "RTC/ShmTransport.hpp"`
- `ShmTransport` and `ShmConsumer` class usage
- `ROUTER_CREATE_SHMTRANSPORT` request handler
- Conditional logging that checks `dynamic_cast<ShmTransport*>(this)`

**MUST NOT be guarded by TRANSCODE** (Lively features for both builds):
- Binary logging: `binLogTimer`, `binLog.InitLog()`, `FillBinLogStats()`, `rtpStreamBinLogRecord(s)`
- Text logging: `WORKER_MSLOG_OPEN`, `WORKER_MSLOG_ROTATE`
- Producer stats: `EmitProducerStats()`, `lastProducerStatsReport`
- Lively appData extraction and `lively.*` member usage

**When merging upstream code**: If upstream doesn't have Lively code and you're adding it back, do NOT wrap it in TRANSCODE unless it's SHM-related. Check v3-lively - if code was unguarded there, it must remain unguarded.

### Project Layout

```
worker/                 C++ mediasoup-worker process
  worker/fbs/           FlatBuffers schema files (.fbs) - SOURCE OF TRUTH
  worker/src/           C++ implementation
  worker/include/       C++ headers

node/                   Node.js library
  node/src/fbs/         Generated TypeScript (DO NOT HAND-EDIT)
  node/src/Worker.ts    WorkerImpl class
  node/src/WorkerTypes.ts  TypeScript types
  node/src/index.ts     Public API (createWorker)
```

### Upstream Merge Workflow

1. Create `RND-*` branch from `v3-lively`
2. Pull from `upstream` (versatica)
3. Resolve conflicts, fix regressions, test
4. Deploy and validate
5. Merge to `v3-lively`

### FlatBuffers Workflow

1. Edit schema: `worker/fbs/*.fbs`
2. Run FlatBuffers compiler (request human assistance for exact command)
3. Generated C++ headers: `worker/fbs/` (e.g., `FBS/log.h`)
4. Generated TypeScript: `node/src/fbs/`
5. Do not hand-edit generated files

### Channel Communication Pattern

Reference this section when implementing or auditing Node.js ↔ C++ communication.

**Requests** (Node.js → C++):
```typescript
channel.request(Method, Body?, offset?)
```

**Notifications** (C++ → Node.js):
```cpp
channelNotifier->Emit(handlerId, event, bodyType?, offset?)
```

**Worker-level handlerId**: `std::to_string(pid)` (C++) / `String(this.#pid)` (TS)

**Extracting notification body**:
```typescript
const obj = new FBS.SomeType();
data!.body(obj);
// read fields from obj
```

**Event emission**:
- `safeEmit`: Non-critical events (failures don't throw)
- `emit`: Lifecycle events (failures propagate)
- External events: emit on both `this` and `this.#observer`

### Equivalence Definitions

When verifying "exact" matches:
- **Strings/enums**: Byte-identical
- **Behavior**: Same observable outputs under same inputs
- **Timing**: Same ordering guarantees (before/after relationships)
