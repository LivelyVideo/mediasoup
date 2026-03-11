# Claude Merge Skills

This folder contains reference documentation for AI-assisted upstream merge auditing. These files guide Claude (or similar AI assistants) through the process of auditing Lively-specific features after merging upstream changes from versatica/mediasoup.

## Key Design Principles

### Mechanical Checklists Over Prose

Each skill file contains **mechanical audit checklists** with:
- Explicit bash commands to run
- Expected output for each command
- Checkbox items to verify
- No ambiguity about what "audited" means

This design prevents the failure mode where an audit reports "PASS" based on component existence without verifying activation chains.

### Silent Failure Modes

Each feature expert documents **Silent Failure Modes** — conditions that cause features to fail without producing any error messages. The audit must explicitly verify these failure modes don't exist.

Example failure modes:
- Timer created but never started → no binlog files
- FBS field defined but not populated → empty values in C++
- Guard added to production code → feature disabled silently

## Usage

When performing an upstream merge audit:

1. Start with `upstream-merge-orchestrator.md` — it coordinates the full audit
2. Use the **Pre-Flight File Manifest** to identify all files requiring audit
3. Run checklists in parallel where possible (use **Parallel Execution Strategy**)
4. For each feature expert, verify against documented **Silent Failure Modes**
5. Use **Coverage Verification** at the end to ensure no gaps

## File Organization

### Orchestrator
- `upstream-merge-orchestrator.md` — Coordinates merge audits, parallel execution, coverage verification

### Audit Procedures (HOW to audit)
- `v3-lively-comparator.md` — Line-by-line diff against v3-lively with mechanical verification commands
- `transcode-guard-auditor.md` — Verify TRANSCODE preprocessor guards with count comparisons
- `data-flow-auditor.md` — Trace data flows with end-to-end verification commands
- `fbs-schema-auditor.md` — Verify FlatBuffers schema completeness field-by-field

### Feature Experts (WHAT to audit)
- `binlogs-expert.md` — Binary stats logging with 6 Silent Failure Modes
- `stats-expert.md` — Periodic producer stats (RND-568) with 7 Silent Failure Modes
- `textlogs-expert.md` — Text logging with 5 Silent Failure Modes
- `appdata-expert.md` — AppData field extraction with 6 Silent Failure Modes
- `transcode-features-expert.md` — ShmTransport guards with 6 Silent Failure Modes
- `mediasoup-lively-features-expert.md` — Catch-all with cross-feature verification

## Checklist Structure

Each skill file follows this structure:

```markdown
# Feature Name Expert

## Silent Failure Modes
### Mode 1: <failure description>
**Symptom**: <what user observes>
**Cause**: <technical cause>
**Detection**: <bash command>
**Expected**: <correct output>
**Failure**: <incorrect output>

## Mechanical Audit Checklist

### 1. Section Name
#### 1.1 Specific Check
```bash
<exact command to run>
```
**Expected**: <what output should show>
- [ ] Checkbox item to verify
```

## Relationship to CLAUDE.md

`CLAUDE.md` in the project root contains:
- Governance rules (priorities, escalation procedures)
- Fork maintenance rules
- Lively data field reference tables

These skill files contain the **detailed verification procedures** that implement those rules through mechanical checklists.
