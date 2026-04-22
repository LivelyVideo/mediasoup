# Upstream Merge Orchestrator

**Last Updated**: 2026-03-11

You are coordinating an upstream merge from `versatica/mediasoup` into the Lively fork. Your job is to ensure **zero regressions** to Lively-specific features while incorporating upstream changes.

---

## Phase 0: Permission Request (REQUIRED FIRST STEP)

Before running a comprehensive audit, request batch permission for read-only operations.

### Permission Request Dialog

Use `AskUserQuestion` with the following:

```
This audit will run many read-only commands (grep, git show, git diff, etc.).
Would you like to allow all read-only analysis commands for this audit session?

Options:
1. "Yes, allow all read-only commands" (Recommended) — No confirmations for grep, git show, diff, etc.
2. "No, ask me each time" — Confirm each command individually
```

### Commands That Will Run Without Confirmation (if approved)

| Category | Commands |
|----------|----------|
| Search | `grep`, `rg`, `ag`, `find` |
| Read | `cat`, `head`, `tail`, `less`, `wc` |
| Compare | `diff`, `sort`, `uniq`, `cut` |
| Git (read) | `git show`, `git log`, `git diff`, `git status`, `git branch`, `git fetch` |
| File info | `ls`, `stat`, `file` |

### Commands That Will ALWAYS Require Confirmation

| Category | Commands |
|----------|----------|
| File changes | `rm`, `mv`, `cp`, `mkdir` |
| Git (write) | `git add`, `git commit`, `git push`, `git reset`, `git checkout` |
| Build | `make`, `npm`, `meson`, `ninja` |
| System | `sudo`, `chmod`, `chown` |

### Implementation

When user selects "Yes, allow all read-only commands", proceed directly with all audit phases.
When user selects "No", pause for confirmation on each bash command.

---

## Pre-Flight: Lively File Manifest

Before starting any audit, verify coverage of ALL these files.

### Tier 1: Critical (MUST audit on ANY merge)

| File | Expert(s) | Auditor(s) | Checked |
|------|-----------|------------|---------|
| `worker/src/RTC/Transport.cpp` | binlogs, stats, appdata | v3-lively-comparator, transcode-guard-auditor, data-flow-auditor | [ ] |
| `worker/src/RTC/WebRtcTransport.cpp` | binlogs | v3-lively-comparator, transcode-guard-auditor | [ ] |
| `worker/src/RTC/Producer.cpp` | binlogs, stats | v3-lively-comparator, transcode-guard-auditor, data-flow-auditor | [ ] |
| `worker/src/RTC/SimpleConsumer.cpp` | binlogs | v3-lively-comparator, transcode-guard-auditor | [ ] |
| `worker/src/RTC/SimulcastConsumer.cpp` | binlogs | v3-lively-comparator, transcode-guard-auditor | [ ] |
| `worker/src/RTC/SvcConsumer.cpp` | binlogs | v3-lively-comparator, transcode-guard-auditor | [ ] |
| `worker/src/RTC/PipeConsumer.cpp` | binlogs | v3-lively-comparator, transcode-guard-auditor | [ ] |
| `worker/include/RTC/Transport.hpp` | binlogs, stats | v3-lively-comparator | [ ] |
| `worker/include/RTC/Producer.hpp` | binlogs | v3-lively-comparator | [ ] |
| `worker/fbs/transport.fbs` | appdata, fbs-schema | data-flow-auditor | [ ] |
| `worker/fbs/producer.fbs` | stats, fbs-schema | fbs-schema-auditor | [ ] |
| `node/src/Transport.ts` | appdata | fbs-schema-auditor, data-flow-auditor | [ ] |
| `node/src/Producer.ts` | stats | fbs-schema-auditor | [ ] |
| `node/src/Router.ts` | appdata | data-flow-auditor | [ ] |
| `node/src/Worker.ts` | textlogs | v3-lively-comparator | [ ] |

### Tier 2: Secondary (audit if changed in merge)

| File | Expert(s) | Checked |
|------|-----------|---------|
| `worker/src/RTC/PlainTransport.cpp` | appdata | [ ] |
| `worker/src/RTC/PipeTransport.cpp` | appdata | [ ] |
| `worker/src/RTC/DirectTransport.cpp` | appdata | [ ] |
| `worker/src/Logger.cpp` | textlogs | [ ] |
| `worker/include/Logger.hpp` | textlogs | [ ] |
| `worker/include/Settings.hpp` | textlogs | [ ] |
| `worker/src/LivelyBinLogs.cpp` | binlogs | [ ] |
| `worker/include/LivelyBinLogs.hpp` | binlogs | [ ] |
| `worker/include/Lively.hpp` | appdata | [ ] |
| `worker/fbs/notification.fbs` | stats, fbs-schema | [ ] |
| `worker/fbs/log.fbs` | textlogs, fbs-schema | [ ] |
| `worker/src/RTC/ShmTransport.cpp` | transcode-features | [ ] |
| `worker/include/RTC/ShmTransport.hpp` | transcode-features | [ ] |

### Pre-Flight Commands

```bash
# 1. List all files changed in this merge
git diff --name-only origin/v3-lively...HEAD > /tmp/changed_files.txt

# 2. Filter to Lively-critical files
grep -E "(Transport|Producer|Consumer|Logger|Lively|Settings|Worker|Router|\.fbs)" /tmp/changed_files.txt

# 3. Count coverage
echo "Total changed: $(wc -l < /tmp/changed_files.txt)"
echo "Lively-critical: $(grep -cE '(Transport|Producer|Consumer|Logger|Lively)' /tmp/changed_files.txt)"
```

---

## Parallel Execution Strategy

### Independent Checks (run in SINGLE tool message)

These checks have no dependencies - execute ALL simultaneously:

**Example: Audit Transport.cpp**
```
# Call ALL 4 in one parallel tool invocation:
1. git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "TRANSCODE"
2. grep -n "TRANSCODE" worker/src/RTC/Transport.cpp
3. git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "binLogTimer"
4. grep -n "binLogTimer" worker/src/RTC/Transport.cpp
```

**Example: Audit multiple files simultaneously**
```
# Parallel file audits (no dependencies between files):
1. grep -n "producerBinLogEnabled" worker/src/RTC/WebRtcTransport.cpp
2. grep -n "rtpStreamBinLogRecord" worker/src/RTC/SimpleConsumer.cpp
3. grep -n "EmitProducerStats" worker/src/RTC/Producer.cpp
4. grep -n "PRODUCER_STATS" node/src/Producer.ts
```

### Parallelization Matrix

| Can Run In Parallel | Description |
|---------------------|-------------|
| v3-lively grep + current grep | Same file, both directions |
| File A audit + File B audit | Different files, no dependency |
| FBS schema check + Node.js check | Both are source verification |
| TRANSCODE guard count + data flow trace | Independent analyses |
| All Tier 1 file initial greps | First pass on all files |

| Must Run Sequentially | Description |
|-----------------------|-------------|
| Find field in schema → Verify C++ extracts | Schema must exist first |
| Identify changed files → Audit each file | Need file list first |
| Find regression → Verify fix | Fix depends on finding issue |

### Optimal Parallel Batches

**Batch 1: Initial reconnaissance (all parallel)**
```bash
# Run ALL these in one tool message:
git diff --name-only origin/v3-lively...HEAD | grep -E "Transport|Producer|Consumer"
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "TRANSCODE"
grep -n "TRANSCODE" worker/src/RTC/Transport.cpp
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "binLogTimer"
grep -n "binLogTimer" worker/src/RTC/Transport.cpp
```

**Batch 2: Feature verification (all parallel)**
```bash
# Run ALL these in one tool message:
grep "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
grep "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp
grep "binLog.InitLog" worker/src/RTC/Producer.cpp
grep "lastProducerStatsReport" worker/src/RTC/Transport.cpp
```

**Batch 3: Data flow verification (all parallel)**
```bash
# Run ALL these in one tool message:
grep "call_id" worker/fbs/transport.fbs
grep "callId" node/src/Router.ts
grep "options->callId" worker/src/RTC/Transport.cpp
grep "lively.callId" worker/src/RTC/Transport.cpp
```

---

## Orchestration Workflow

### Phase 1: Pre-Merge Assessment (after Phase 0 approval)

1. **Run Pre-Flight Commands** (above)
2. **Check off Tier 1 files** that appear in changed list
3. **Check off Tier 2 files** that appear in changed list
4. **Identify any changed files NOT in manifest** → assign to catch-all expert

### Phase 2: Dispatch to Experts

For each checked file, invoke appropriate skill:

| File Pattern | Primary Expert | Required Auditors |
|--------------|----------------|-------------------|
| `**/Transport.cpp`, `**/Transport.hpp` | binlogs, stats, appdata | v3-lively-comparator, transcode-guard-auditor, data-flow-auditor |
| `**/WebRtcTransport.cpp` | binlogs-expert | v3-lively-comparator, transcode-guard-auditor |
| `**/Producer.cpp`, `**/Producer.hpp` | binlogs-expert, stats-expert | v3-lively-comparator, transcode-guard-auditor, data-flow-auditor |
| `**/*Consumer.cpp` | binlogs-expert | v3-lively-comparator, transcode-guard-auditor |
| `**/Logger.cpp`, `**/Logger.hpp` | textlogs-expert | v3-lively-comparator |
| `**/LivelyBinLogs.cpp` | binlogs-expert | transcode-guard-auditor |
| `**/ShmTransport*` | transcode-features-expert | transcode-guard-auditor |
| `**/transport.fbs` | fbs-schema-auditor, appdata-expert | data-flow-auditor |
| `node/src/Transport.ts` | appdata-expert | fbs-schema-auditor |
| `node/src/Producer.ts` | stats-expert, appdata-expert | fbs-schema-auditor |
| Any other Lively-modified file | mediasoup-lively-features-expert | v3-lively-comparator |

### Phase 3: Auditor Execution Order

Run auditors in this sequence (each may reveal issues for the next):

1. **v3-lively-comparator** — Establishes baseline: what v3-lively looked like
2. **transcode-guard-auditor** — Verifies TRANSCODE guards match v3-lively
3. **fbs-schema-auditor** — Verifies FBS fields exist for all Lively data
4. **data-flow-auditor** — Verifies data flows from source to destination

### Phase 4: Coverage Verification

```markdown
## Coverage Report

Files changed in merge: ___
Files in Tier 1 manifest: 15
Files in Tier 2 manifest: 13

### Tier 1 Coverage
- [ ] Transport.cpp
- [ ] WebRtcTransport.cpp
- [ ] Producer.cpp
- [ ] SimpleConsumer.cpp
- [ ] SimulcastConsumer.cpp
- [ ] SvcConsumer.cpp
- [ ] PipeConsumer.cpp
- [ ] Transport.hpp
- [ ] Producer.hpp
- [ ] transport.fbs
- [ ] producer.fbs
- [ ] Transport.ts
- [ ] Producer.ts
- [ ] Router.ts
- [ ] Worker.ts

Coverage: ___/15 Tier 1 files (must be 100%)

### Uncovered Files
(List any Lively-modified files not in manifest)
```

### Phase 5: Regression Report Template

```markdown
# Upstream Merge Regression Audit

**Date**: YYYY-MM-DD
**Branch**: <branch-name>
**Auditor**: <name>

## Summary
- Files audited: X
- Regressions found: Y
- Regressions fixed: Z

## By Category

### TRANSCODE Guard Regressions
| File | Line | Issue | Status |
|------|------|-------|--------|

### Data Flow Regressions
| Field | Break Point | Issue | Status |
|-------|-------------|-------|--------|

### FBS Schema Regressions
| Field | Table | Issue | Status |
|-------|-------|-------|--------|

### Feature-Specific Regressions
| Feature | File | Issue | Status |
|---------|------|-------|--------|

## Remaining Issues
(Items requiring human decision)
```

---

## Quick Reference Commands

### Check if file changed
```bash
git diff --name-only origin/v3-lively...HEAD | grep "Transport.cpp"
```

### Compare TRANSCODE guards
```bash
# v3-lively count
git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"

# Current count
grep -c "ifdef TRANSCODE" worker/src/RTC/Transport.cpp
```

### Verify activation chain element
```bash
# Timer creation
grep -n "binLogTimer = new" worker/src/RTC/Transport.cpp

# InitLog call
grep -n "InitLog" worker/src/RTC/Transport.cpp worker/src/RTC/Producer.cpp

# FillBinLogStats call
grep -n "FillBinLogStats" worker/src/RTC/Transport.cpp
```

---

## Invocation

To run this orchestrator:
```
/upstream-merge-orchestrator
```

Then follow the phases above, using parallel execution where possible.
