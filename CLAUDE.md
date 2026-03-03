# Lively Fork of mediasoup

## Repository and branches

This is the Lively fork of [versatica/mediasoup](https://github.com/versatica/mediasoup).

- **origin**: `LivelyVideo/mediasoup` (this fork)
- **upstream**: `versatica/mediasoup` (original project by versatica)

Branches:
- `origin/v3-lively` — **the production branch**. This is effectively Lively's "master". The `master` branch in this fork exists but is not used for anything.
- `RND-*` branches — feature/fix/merge branches. Always branched from `v3-lively`, merged back into `v3-lively`.

## Upstream merge process

The merge workflow:
1. Create `RND-*` branch from `v3-lively`
2. Pull from `upstream` (versatica) into that branch
3. Resolve merge conflicts, fix regressions, test
4. Deploy and validate
5. Push into `v3-lively`

## Build flavors

There are two build flavors:
- **Without transcode** — the primary flavor, currently in production. Used as a package by Lively's SFU project. **Prioritized in building and testing.**
- **With transcode** (`#ifdef TRANSCODE`) — adds shared-memory transport and transcoding support.

Code for the transcode flavor is guarded by `#ifdef TRANSCODE` in several C++ files. **Never remove these guards.**

## Project layout

- `worker/` — C++ mediasoup-worker process
  - `worker/fbs/` — FlatBuffers schema files (`.fbs`), source of truth for FBS wire format
  - `worker/src/` — C++ implementation
  - `worker/include/` — C++ headers
- `node/` — Node.js library that spawns and communicates with the C++ worker
  - `node/src/fbs/` — **Generated** TypeScript FBS code (from `worker/fbs/*.fbs`). Do not hand-edit.
  - `node/src/Worker.ts` — WorkerImpl class
  - `node/src/WorkerTypes.ts` — TypeScript types for Worker API
  - `node/src/index.ts` — Public API, `createWorker()` entry point

## Lively-specific features

Features not present in upstream mediasoup:

1. **Text logging** — Per-worker log files via `WORKER_MSLOG_OPEN` / `WORKER_MSLOG_ROTATE` requests. Failures reported back via `LOGGER_WRITE_FAILED` notifications.

2. **Binary stats logging** — Produces `ms_p_*` (producer) and `ms_c_*` (consumer) binary stat files. Controlled by `binStatsDisabled` and `binStatsPath` settings. **Activation chain** (every link must be present for files to appear):
   - `WebRtcTransport` constructor sets `producerBinLogEnabled = true`
   - `Transport` constructor calls `consumersBinLog.InitLog(...)` with `ConsumerFileName` lambda
   - `Producer` constructor calls `binLog.InitLog(...)` (only if `producerBinLogEnabled` is true)
   - `Transport::OnTimer` calls `FillBinLogStats()` on all producers and consumers every 2 seconds
   - File I/O is in `LivelyBinLogs.cpp`, guarded by `#ifdef TRANSCODE`

3. **Developer log levels** — `LogDevLevel` enum (`LOG_DEV_DEBUG=3`, `LOG_DEV_WARN=2`, `LOG_DEV_NONE=0`) and `logTraceEnabled` flag.

4. **SHM transport** — Shared-memory transport for transcoding (`ROUTER_CREATE_SHMTRANSPORT`), transcode flavor only.

5. **Periodic producer stats** (RND-568) — Every 10 seconds, Transport::OnTimer emits per-producer stats (bitrate, width, height, frames) via FBS notification to Node.js. **Activation chain:**
   - Transport constructor: `lastProducerStatsReport = DepLibUV::GetTimeMs()` when `producer_stats` option is `true`
   - Transport::OnTimer: 10-second interval check → iterates `mapProducers` → calls `producer->EmitProducerStats()`
   - Producer::EmitProducerStats(): iterates `rtpStreamByEncodingIdx`, builds `ProducerStatsNotification`, emits via `channelNotifier->Emit()`
   - Node.js Producer.ts: handles `PRODUCER_STATS` notification, emits `producerstats` event with `ProducerStatEvent[]`

### Lively data field reference

These fields are extracted from protocol messages and used in Lively features. During a JSON→FBS migration, **every field listed here must have an explicit FBS schema entry**. A missing FBS field means the C++ code silently gets empty/default values — no error, no warning, just wrong data.

**Transport creation** (FBS `Options` table in `transport.fbs`):

| Field | v3-lively source | FBS field | Used by |
|-------|-----------------|-----------|---------|
| callId | `appData["callId"]` | `Options.call_id` | Producer/consumer binlog filenames, diagnostics |
| peerId | `appData["peerId"]` | `Options.peer_id` | Lively diagnostics |
| mirrorId | `appData["mirrorId"]` | `Options.mirror_id` | Lively diagnostics |
| streamName | `appData["streamName"]` | `Options.stream_name` | Lively diagnostics |
| clientReferrer | `appData["clientReferrer"]` | `Options.client_referrer` | Consumer binlog subdirectory prefix |
| producerStats | `appData["producerStats"]` | `Options.producer_stats` | Enables periodic producer stats emission |

**Produce request** (FBS `ProduceRequest` table in `transport.fbs`):

| Field | v3-lively source | FBS field | Used by |
|-------|-----------------|-----------|---------|
| userId | `appData` via `GetUserIdFromAppData()` | `ProduceRequest.user_id` | Producer binlog filename (`ms_p_<userId>_...`) |
| clientReferrer | `appData["clientReferrer"]` | `ProduceRequest.client_referrer` | Producer binlog subdirectory prefix |

## Fork maintenance rules (ordered by priority)

**Every regression is equally important.** Do not classify regressions as "minor" or "low-risk." A regression is a regression — it must be identified and fixed regardless of perceived likelihood or impact. Do not rationalize away a rule violation by reasoning about call paths, usage frequency, or probability. The rules below are bright-line compliance rules, not risk assessments.

### 1. CONTRACTS MATCH V3-LIVELY (highest priority)

External contracts must match `origin/v3-lively` exactly. External contracts are:
- Command-line option names and their short-option letters (e.g., `'b'` for binStatsDisabled)
- Node.js API: method signatures, event names, event payloads, type exports
- FBS wire format: enum values and their integer assignments, table field names, notification event IDs
- Environment variable names

**Copy the exact value from v3-lively. Do not derive, infer, or "improve" it.** Do not assume any contract is "internal only" or "rarely used."

### 2. NO REGRESSIONS (behavioral correctness)

Any behavioral change from v3-lively is a regression unless explicitly approved. This includes:
- Log output format changes (e.g., adding/removing characters, changing delimiters)
- Log destination changes (e.g., stdout vs log file)
- Thread safety changes (e.g., removing `thread_local` qualifiers)
- Cleanup/shutdown behavior changes (e.g., removing flush-on-abort)
- Any observable difference in output, timing, or side effects

### 3. ESCALATE WHEN REPLICATION IS NON-OBVIOUS

When preserving v3-lively behavior after an upstream merge does not have an obvious, direct solution — because the target technology differs, an API doesn't exist, a data format changed, or for any other reason — you MUST:

1. STOP work on that specific item
2. NOTIFY the programmer: state which v3-lively behavior cannot be directly replicated
3. SUMMARIZE what v3-lively does: the exact output, data fields, format, and purpose
4. EXPLAIN what makes replication non-obvious: the specific technical obstacle

Then wait for the programmer to decide the approach. Do not implement a substitute, approximation, or reduced version on your own.

### 4. NO UNNECESSARY CHANGES (scope discipline)

Do not change function signatures, type definitions, or public APIs unless the change is **required to make the current task compile and run correctly**. Do not:
- Add parameters to existing functions
- Widen or narrow existing types
- Rename existing fields or methods
- Change return types
If you believe a signature change is necessary, **stop and state**: (a) what won't compile/work without it, and (b) what the exact change is. Wait for approval.

### 5. VERIFY AGAINST V3-LIVELY (mandatory procedure)

After implementing each file, run `git show origin/v3-lively:<filepath>` and diff every external-facing value character-by-character. This includes: format strings, field names, field counts in log messages, type names, event names, and option letters. If any value differs, it is a regression — fix it per Rule 2, or escalate per Rule 3.

### 6. INTERNALS FOLLOW UPSTREAM

Internal implementation must follow upstream patterns:
- Use FBS for request/notification bodies (not raw JSON)
- Use `absl::flat_hash_map` (not `std::map`)
- Follow upstream class hierarchies and include patterns

**But never when it introduces a regression (Rules 1–2).**

## Regression audit procedure

When auditing for regressions after an upstream merge:

### 1. Trace activation chains, not just component existence

Verifying that a type, member, method, or file exists is **not sufficient**. You must trace the runtime flow that activates each Lively feature. For each feature listed in "Lively-specific features" above, walk the chain from configuration → initialization → execution → output. If any link is missing, it is a regression — even if every individual component exists.

This means verifying **three things** for each component:
- **Exists**: The member/method/type is declared
- **Activated**: Something *writes* to it, *calls* it, or *sets* it (verify the setter, not just the getter)
- **Sourced correctly**: The values it receives come from the right place (not hardcoded placeholders)

**Bad**: "StatsBinLog member exists in Transport.hpp ✅"
**Bad**: "`binLog.InitLog()` is called in Producer constructor ✅" (did not check what values the lambda captures)
**Good**: "Producer constructor calls `binLog.InitLog()` with lambda capturing `userId` extracted from `ProduceRequest.user_id` FBS field ✅"

### 2. Diff all Lively-modified methods line-by-line against v3-lively

Regressions hide in initialization code, timer callbacks, request handlers, and anywhere Lively added code alongside upstream logic. After an upstream merge, explicitly diff **all of these** against v3-lively:
- **Constructors**: `Transport`, `WebRtcTransport`, `PlainTransport`, `PipeTransport`, `Producer`
- **Timer callbacks**: `Transport::OnTimer` (contains both binlog and producer stats logic)
- **Request handlers**: Transport produce handler (forwards fields to Producer constructor)
- **Any method with Lively comments** (e.g., `// RND-568`, `// PM-1560`, `// Amir Pauker`)

One missing initialization line (e.g., `producerBinLogEnabled = true`) can silently disable an entire subsystem. A reordered code block (e.g., stats emission before vs after timer restart) is a timing behavioral change.

### 3. Ask "under what conditions would this feature silently fail?"

For each Lively feature, identify the failure modes that produce **no error messages** — just silent absence of output. Then verify those conditions don't exist.

Consider three levels of silent failure:
- **Chain broken**: `InitLog()` never called → timer fires, data goes nowhere
- **Values wrong**: `InitLog()` called with `userId = "0"` → files appear but contain wrong data
- **Feature absent**: Entire feature removed (member + timer logic + method + Node.js handler) → nothing happens, no errors

Example: "Binary stats files won't appear if `InitLog()` is never called — the timer fires, `FillBinLogStats()` runs, but data goes nowhere because the log was never opened."

### 4. Verify data values, not just execution paths

Tracing that code **executes** is necessary but not sufficient. You must also verify the **data flowing through** matches v3-lively. Specifically:

- For every variable captured in a lambda or passed to a function, trace where it gets its value
- If v3-lively extracts a value from JSON (`json.find("key")`, `GetUserIdFromAppData()`), verify the FBS equivalent extraction exists
- A hardcoded placeholder (e.g., `userId = "0"`, `clientReferrer = ""`) where v3-lively extracted a real value is a **critical regression** — it means output files contain wrong data

Use the **Lively data field reference** table above as a checklist. For every field listed, verify the full path: FBS schema field → C++ extraction from FBS → use in feature code.

### 5. Audit FBS schema completeness against v3-lively JSON extraction

In v3-lively, Lively fields were extracted from free-form JSON (`appData`, request data). In the FBS migration, each field must be **explicitly** added to an FBS table. A missing FBS field means the C++ code silently receives empty/default values — no compilation error, no runtime error, just wrong data.

**Procedure**: For each FBS table that replaced a JSON object (`Options`, `ProduceRequest`, `UpdateSettingsRequest`, etc.):
1. Read the v3-lively code that parsed the JSON version
2. List every field extracted via `json.find()`, `json["key"]`, or helper functions
3. Verify each field exists in the FBS table definition
4. Verify the C++ code reads the field from the FBS message
5. Verify the Node.js/TypeScript code populates the field when sending the request

### 6. Diff all Lively log messages against v3-lively

Missing or changed log messages are behavioral regressions (Rule 4). For every Lively-modified file, diff all `MS_DEBUG_TAG`, `MS_WARN_TAG`, `MS_DEBUG_DEV`, and `MS_WARN_DEV` calls against v3-lively. Pay special attention to:
- Diagnostic messages that include variable values (`userId`, `clientReferrer`, `lively.ToStr()`)
- Warning messages for missing/empty fields (these are operational signals)
- Debug messages that mark feature activation ("creating producer bin log", "emitting producerstats")

### 7. Never inherit verdicts from prior audits

Each audit must verify from scratch. A prior audit saying "PASS: Binary stats logging" does not mean binary logging is correct — the prior audit may have used a weaker methodology (e.g., component-existence checking instead of activation-chain tracing). Re-verify every feature independently.

## Constraints for all tasks

- **Do NOT touch** without explicit approval: `Router.ts`, `Transport.hpp`, `Transport.cpp`, `WebRtcTransport.cpp`, `transport.fbs`. Note: this restriction applies to *modifications*. During regression audits, these files require the **most** scrutiny — they contain complex Lively initialization logic that upstream doesn't have, making them the most likely to suffer merge damage.
- **Do NOT remove** any `#ifdef TRANSCODE` guards
- **appData fields must arrive at the same destinations as in v3-lively.** The serialization format (JSON or FBS) is a technology detail — the data must flow to the same C++ code that uses it. If v3-lively extracted a field from appData JSON, the FBS equivalent must exist and be populated.
- Call-creation APIs must continue working exactly as they do now
- If you think a file outside the current task scope needs changes, **stop and ask first**

## FlatBuffers workflow

1. Schema source files: `worker/fbs/*.fbs`
2. Generated C++ headers: `worker/fbs/` (e.g., `FBS/log.h`)
3. Generated TypeScript: `node/src/fbs/` (barrel exports like `node/src/fbs/log.ts`)
4. After editing `.fbs` files, run the FlatBuffers compiler to regenerate both C++ and TypeScript
5. Do not hand-edit generated files

## Channel communication pattern

Node.js and C++ communicate via a FlatBuffers-based channel:
- **Requests** (Node.js -> C++): `channel.request(Method, Body?, offset?)`
- **Notifications** (C++ -> Node.js): `channelNotifier->Emit(handlerId, event, bodyType?, offset?)`
- Worker-level `handlerId` is `std::to_string(pid)` / `String(this.#pid)`
- Extract typed notification body: create empty FBS object, call `data!.body(obj)`, read fields
- Use `safeEmit` for non-critical events, `emit` for lifecycle events
- When an event should be observable externally, emit on both `this` (WorkerEvents) **and** `this.#observer` (WorkerObserverEvents)
