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

After implementing each file, run `git show origin/v3-lively:<filepath>` and diff every external-facing value character-by-character.

### Rule 6: INTERNALS FOLLOW UPSTREAM (when safe)

Internal implementation should follow upstream patterns. **Exception**: When upstream patterns would cause a regression (Rules 1-2), preserve the v3-lively pattern.

---

## PART TWO: FILE RESTRICTIONS

### Restricted Files (Do NOT Modify Without Approval)

| File | Sensitivity | Why restricted |
|------|-------------|----------------|
| `transport.fbs` | Highest | Wire format - changes break Node.js ↔ C++ compatibility |
| `Transport.cpp` | High | Contains all Lively timer logic, binlog init, producer stats |
| `Transport.hpp` | High | Lively member declarations |
| `WebRtcTransport.cpp` | High | Sets `producerBinLogEnabled`, Lively initialization |
| `Router.ts` | Medium | Transport factory, appData forwarding |

**During audits**: These files require the **most scrutiny**. Reading and examining is required; modifying requires approval.

### Other Restrictions

- **Do NOT remove** any `#ifdef TRANSCODE` guards from SHM-related code
- **Do NOT add** `#ifdef TRANSCODE` guards around binary logging, text logging, or producer stats
- **Do NOT modify** files outside the current task scope without asking first

---

## PART THREE: TASK BOUNDARIES

The user defines each task explicitly. If boundaries are unclear, ask. If completing a task requires changes outside its defined scope, stop and report before proceeding.

---

## PART FOUR: REGRESSION AUDIT PROCEDURE

**For detailed audit procedures, see `claude-merge-skills/` folder.**

### Audit Skills (How to Audit)

| Skill File | Purpose |
|------------|---------|
| `v3-lively-comparator.md` | Line-by-line comparison against v3-lively |
| `transcode-guard-auditor.md` | Verify TRANSCODE guards match v3-lively |
| `data-flow-auditor.md` | Trace data from source to destination |
| `fbs-schema-auditor.md` | Verify FBS fields exist for all Lively data |

### Feature Expert Skills (What to Audit)

| Skill File | Covers |
|------------|--------|
| `binlogs-expert.md` | Binary stats logging (`ms_p_*`, `ms_c_*` files) |
| `stats-expert.md` | Periodic producer stats (RND-568) |
| `textlogs-expert.md` | Text logging, log levels, log macros |
| `appdata-expert.md` | AppData field extraction and forwarding |
| `transcode-features-expert.md` | ShmTransport and transcode-only code |
| `mediasoup-lively-features-expert.md` | Catch-all for other Lively features |

### Orchestration

Use `upstream-merge-orchestrator.md` to coordinate a full upstream merge audit. It assigns files to experts and ensures 100% coverage.

### Quick Reference: Audit Steps

1. **Trace activation chains** — configuration → initialization → execution → output
2. **Diff against v3-lively** — constructors, OnTimer, request handlers
3. **Verify data values** — not just execution paths, but actual data flowing through
4. **Check TRANSCODE guards** — match v3-lively exactly
5. **Document results** — PASS / FAIL / ESCALATED for each feature

---

## PART FIVE: LIVELY-SPECIFIC FEATURES (Summary)

Features not present in upstream mediasoup. **See feature expert skills for details.**

| Feature | Skill File | Key Files |
|---------|------------|-----------|
| Text logging | `textlogs-expert.md` | Logger.cpp, Logger.hpp |
| Binary stats logging | `binlogs-expert.md` | Transport.cpp, Producer.cpp, *Consumer.cpp |
| Developer log levels | `textlogs-expert.md` | Logger.hpp |
| SHM transport | `transcode-features-expert.md` | ShmTransport.cpp (transcode only) |
| Periodic producer stats | `stats-expert.md` | Transport.cpp, Producer.cpp, Producer.ts |

### Lively Data Field Reference

| Field | FBS Field | Used By |
|-------|-----------|---------|
| callId | `Options.call_id` | Binlog filenames, diagnostics |
| peerId | `Options.peer_id` | Diagnostics |
| mirrorId | `Options.mirror_id` | Diagnostics |
| streamName | `Options.stream_name` | Diagnostics |
| clientReferrer | `Options.client_referrer` | Binlog subdirectory |
| producerStats | `Options.producer_stats` | Enables stats emission |
| userId | `ProduceRequest.user_id` | Producer binlog filename |

---

## PART SIX: REFERENCE MATERIAL

### Glossary

| Term | Definition |
|------|------------|
| **Activation chain** | The sequence of calls from configuration to output that enables a feature |
| **External contract** | Any interface visible to code outside this repository (CLI, API, wire format) |
| **Binlog** | Binary log files (`ms_p_*`, `ms_c_*`) containing stats data |
| **v3-lively** | The `origin/v3-lively` branch - the production reference |

### Repository Structure

```
origin:     LivelyVideo/mediasoup (this fork)
upstream:   versatica/mediasoup (original)

Branches:
  v3-lively     Production branch (Lively's "master")
  RND-*         Feature/fix/merge branches
  master        Exists but unused
```

### Build Flavors

| Flavor | Guard | Status | Use |
|--------|-------|--------|-----|
| Without transcode | (default) | Production | SFU package |
| With transcode | `#ifdef TRANSCODE` | Secondary | SHM transport, transcoding |

### TRANSCODE Guard Rules

**MUST be guarded** (transcode-only): `ShmTransport`, `ShmConsumer`, `ROUTER_CREATE_SHMTRANSPORT`

**MUST NOT be guarded** (both builds): Binary logging, text logging, producer stats, appData extraction

See `claude-merge-skills/transcode-guard-auditor.md` for detailed rules.

### Project Layout

```
worker/                 C++ mediasoup-worker process
  worker/fbs/           FlatBuffers schema files (.fbs)
  worker/src/           C++ implementation
  worker/include/       C++ headers

node/                   Node.js library
  node/src/fbs/         Generated TypeScript (DO NOT HAND-EDIT)
  node/src/Worker.ts    WorkerImpl class
  node/src/index.ts     Public API
```

### FlatBuffers Workflow

1. Edit schema: `worker/fbs/*.fbs`
2. Run FlatBuffers compiler
3. Generated files in `worker/fbs/` (C++) and `node/src/fbs/` (TS)
4. Do not hand-edit generated files

### Channel Communication Pattern

**Requests** (Node.js → C++): `channel.request(Method, Body?, offset?)`

**Notifications** (C++ → Node.js): `channelNotifier->Emit(handlerId, event, bodyType?, offset?)`
