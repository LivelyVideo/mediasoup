# Merge Audit Command

Invoke the merge-auditor agent to audit upstream merges for Lively regressions.

## Usage

```
/merge-audit [skill]
```

## Arguments

| Argument | Description |
|----------|-------------|
| (none) | Run full audit with all skills in sequence |
| `binlogs` | Audit binary stats logging activation chain |
| `stats` | Audit periodic producer stats (RND-568) |
| `textlogs` | Audit text logging system |
| `appdata` | Audit appData field extraction |
| `guards` | Audit TRANSCODE guard correctness |
| `fbs` | Audit FBS schema completeness |
| `compare` | Compare against v3-lively baseline |
| `dataflow` | Trace data flow from source to destination |

## Examples

```
# Full regression audit after upstream merge
/merge-audit

# Check only TRANSCODE guards (most common regression)
/merge-audit guards

# Verify binlog system after Transport.cpp changes
/merge-audit binlogs

# Compare specific external contracts
/merge-audit compare
```

## What This Does

1. **Full Audit**: Runs all 8 skills in optimal order:
   - compare-v3-lively (establish baseline)
   - audit-transcode-guards (highest risk)
   - audit-fbs-schema (silent failures)
   - trace-data-flow (data correctness)
   - audit-binlogs (binary logging)
   - audit-stats (periodic stats)
   - audit-textlogs (text logging)
   - audit-appdata (field handling)

2. **Focused Audit**: Runs only the specified skill for targeted investigation.

## Output

Each audit produces a structured report with:
- Silent Failure Mode checks (PASS/FAIL)
- Mechanical checklist results
- v3-lively comparison (where applicable)
- PASS/FAIL/REGRESSION status

## Reference

Full documentation: `claude-merge-skills/README.md`
Agent definition: `.claude/agents/merge-auditor.md`
Skills: `.claude/skills/merge-audit/`
