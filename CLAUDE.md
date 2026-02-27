# Lively Fork of mediasoup

## Repository and branches

This is the Lively fork of [versatica/mediasoup](https://github.com/versatica/mediasoup).

- **origin**: `LivelyVideo/mediasoup` (this fork)
- **upstream**: `versatica/mediasoup` (original project by versatica)

Branches:
- `origin/v3-lively` — **the production branch**. This is effectively Lively's "master". The `master` branch in this fork exists but is not used for anything.
- `RND-*` branches — feature/fix/merge branches. Always branched from `v3-lively`, merged back into `v3-lively`.

## Upstream merge process

Versatica continues developing mediasoup upstream (new features, bug fixes, performance improvements). Lively periodically adopts these upstream changes (e.g., the FBS migration). Meanwhile, Lively maintains its own features in the same codebase.

The merge workflow:
1. Create `RND-*` branch from `v3-lively`
2. Pull from `upstream` (versatica) into that branch
3. Resolve merge conflicts, fix regressions, test
4. Deploy and validate
5. Push into `v3-lively`

**The biggest risk during upstream merges is regressions.** The second risk is unwelcome side effects — upstream changes that inadvertently alter Lively-specific behavior. This is why the rules below exist.

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
2. **Binary stats logging** — Controlled by `binStatsDisabled` and `binStatsPath` settings.
3. **Developer log levels** — `LogDevLevel` enum (`LOG_DEV_DEBUG=3`, `LOG_DEV_WARN=2`, `LOG_DEV_NONE=0`) and `logTraceEnabled` flag.
4. **SHM transport** — Shared-memory transport for transcoding (`ROUTER_CREATE_SHMTRANSPORT`), transcode flavor only.

## Fork maintenance rules (ordered by priority)

**Every regression is equally important.** Do not classify regressions as "minor" or "low-risk." A regression is a regression — it must be identified and fixed regardless of perceived likelihood or impact. Do not rationalize away a rule violation by reasoning about call paths, usage frequency, or probability. The rules below are bright-line compliance rules, not risk assessments.

### 1. CONTRACTS MATCH V3-LIVELY (highest priority)

External contracts must match `origin/v3-lively` exactly. External contracts are:
- Command-line option names and their short-option letters (e.g., `'b'` for binStatsDisabled)
- Node.js API: method signatures, event names, event payloads, type exports
- FBS wire format: enum values and their integer assignments, table field names, notification event IDs
- Environment variable names

**Copy the exact value from v3-lively. Do not derive, infer, or "improve" it.**

These contracts have external exposure — other systems and people depend on them. Do not assume any contract is "internal only" or "rarely used." If a value differs from v3-lively, it is a regression that must be fixed.

### 2. INTERNALS FOLLOW UPSTREAM (medium priority)

Internal implementation should follow upstream patterns where possible:
- Use FBS for request/notification bodies (not raw JSON)
- Use `absl::flat_hash_map` (not `std::map`)
- Follow upstream class hierarchies and include patterns

**But never when it conflicts with Rule 1.**

### 3. NO UNNECESSARY CHANGES (scope discipline)

Do not change function signatures, type definitions, or public APIs unless the change is **required to make the current task compile and run correctly**. Do not:
- Add parameters to existing functions
- Widen or narrow existing types
- Rename existing fields or methods
- Change return types
- Pass `appData` into transport options or other places it wasn't before

If you believe a signature change is necessary, **stop and state**: (a) what won't compile/work without it, and (b) what the exact change is. Wait for approval.

### 4. NO REGRESSIONS (behavioral correctness)

Any behavioral change from v3-lively is a regression unless explicitly approved. This includes:
- Log output format changes (e.g., adding/removing characters, changing delimiters)
- Log destination changes (e.g., stdout vs log file)
- Thread safety changes (e.g., removing `thread_local` qualifiers)
- Cleanup/shutdown behavior changes (e.g., removing flush-on-abort)
- Any observable difference in output, timing, or side effects

Do not dismiss a behavioral change as "minor" or "cosmetic." Report it and fix it.

### 5. VERIFY AGAINST V3-LIVELY (mandatory procedure)

After implementing each file, run `git show origin/v3-lively:<filepath>` and check every external-facing value character-by-character.

## Constraints for all tasks

- **Do NOT touch** without explicit approval: `Router.ts`, `Transport.hpp`, `Transport.cpp`, `WebRtcTransport.cpp`, `transport.fbs`
- **Do NOT remove** any `#ifdef TRANSCODE` guards
- **Do NOT pass** `appData` fields into transport options
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
