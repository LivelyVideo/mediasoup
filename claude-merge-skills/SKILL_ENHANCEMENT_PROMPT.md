# Skill Enhancement Prompt

## Objective

Transform the mediasoup merge audit skills from **prose documentation** into **mechanical execution checklists** that guarantee complete coverage when followed.

## Context

You are enhancing skill files in `claude-merge-skills/`. These skills guide AI assistants through regression audits after upstream merges. The current skills explain *what* to check but lack *specific commands* that ensure completeness.

**Current state**: Guidance-oriented prose
**Target state**: Executable checklists with copy-paste commands and expected outputs

## Input Files

Read these existing skills before making changes:
- `upstream-merge-orchestrator.md`
- `v3-lively-comparator.md`
- `transcode-guard-auditor.md`
- `data-flow-auditor.md`
- `fbs-schema-auditor.md`
- `binlogs-expert.md`
- `stats-expert.md`
- `textlogs-expert.md`
- `appdata-expert.md`
- `transcode-features-expert.md`
- `mediasoup-lively-features-expert.md`

## Transformation Requirements

Apply these 5 enhancements to the skills:

---

### Enhancement 1: Add Mechanical Checklists

**For each skill**, convert prose procedures into checkbox checklists with explicit commands.

**Transform FROM**:
```markdown
### Step 1: Trace Activation Chains
For each Lively feature, verify the complete chain from configuration
to initialization to execution to output. Verify three things...
```

**Transform TO**:
```markdown
### Checklist: Binary Logging Activation

#### 1.1 Timer Creation
```bash
# Command:
grep -n "binLogTimer = new" worker/src/RTC/Transport.cpp

# Expected output pattern:
# <line>:   this->binLogTimer = new TimerHandle(this);
# Must NOT have #ifdef TRANSCODE in the 2 lines above
```
- [ ] Timer created unconditionally (no TRANSCODE guard)

#### 1.2 Timer Started
```bash
# Command:
grep -n "binLogTimer->Start" worker/src/RTC/Transport.cpp

# Expected: At least 2 matches (constructor + OnTimer)
```
- [ ] Timer started in appropriate locations
```

**Rules**:
- Every verification step must have a concrete command
- Every command must have expected output or pattern to match
- Use checkbox `- [ ]` for items to verify
- Group related checks under numbered headings

---

### Enhancement 2: Add Pre-Flight File List to Orchestrator

Add a **hardcoded manifest** of all Lively-modified files to `upstream-merge-orchestrator.md`.

**Add this section**:
```markdown
## Pre-Flight: Lively File Manifest

Before starting any audit, verify coverage of ALL these files:

### Tier 1: Critical (must audit on ANY merge)
| File | Expert | Checked |
|------|--------|---------|
| worker/src/RTC/Transport.cpp | binlogs, stats, appdata | [ ] |
| worker/src/RTC/WebRtcTransport.cpp | binlogs | [ ] |
| worker/src/RTC/Producer.cpp | binlogs, stats | [ ] |
| worker/src/RTC/SimpleConsumer.cpp | binlogs | [ ] |
| worker/src/RTC/SimulcastConsumer.cpp | binlogs | [ ] |
| worker/src/RTC/SvcConsumer.cpp | binlogs | [ ] |
| worker/src/RTC/PipeConsumer.cpp | binlogs | [ ] |
| worker/include/RTC/Transport.hpp | binlogs, stats | [ ] |
| worker/include/RTC/Producer.hpp | binlogs | [ ] |
| worker/fbs/transport.fbs | appdata, fbs-schema | [ ] |
| worker/fbs/producer.fbs | stats, fbs-schema | [ ] |
| node/src/Transport.ts | appdata | [ ] |
| node/src/Producer.ts | stats | [ ] |
| node/src/Router.ts | appdata | [ ] |
| node/src/Worker.ts | textlogs | [ ] |

### Tier 2: Secondary (audit if changed)
| File | Expert | Checked |
|------|--------|---------|
| worker/src/RTC/PlainTransport.cpp | appdata | [ ] |
| worker/src/RTC/PipeTransport.cpp | appdata | [ ] |
| worker/src/RTC/DirectTransport.cpp | appdata | [ ] |
| worker/src/Logger.cpp | textlogs | [ ] |
| worker/include/Logger.hpp | textlogs | [ ] |
| worker/include/Settings.hpp | textlogs | [ ] |
| worker/src/LivelyBinLogs.cpp | binlogs | [ ] |
| worker/include/LivelyBinLogs.hpp | binlogs | [ ] |
| worker/include/Lively.hpp | appdata | [ ] |
| worker/fbs/notification.fbs | stats, fbs-schema | [ ] |
| worker/fbs/log.fbs | textlogs, fbs-schema | [ ] |

### Coverage Verification Command
```bash
# Generate list of changed files that are in our manifest
git diff --name-only origin/v3-lively...HEAD | grep -E "(Transport|Producer|Consumer|Logger|Lively|\.fbs)" | sort
```
```

---

### Enhancement 3: Add Parallel Execution Guidance

Add a section to `upstream-merge-orchestrator.md` showing how to parallelize comparisons.

**Add this section**:
```markdown
## Parallel Execution Strategy

### Independent Checks (run simultaneously)
These checks have no dependencies and should be called in a SINGLE tool message:

**Example: Audit Transport.cpp guards**
```
# Call ALL of these in one parallel tool invocation:
1. git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "TRANSCODE"
2. grep -n "TRANSCODE" worker/src/RTC/Transport.cpp
3. git show origin/v3-lively:worker/src/RTC/Transport.cpp | grep -n "binLogTimer"
4. grep -n "binLogTimer" worker/src/RTC/Transport.cpp
```

### Dependent Checks (run sequentially)
These require results from previous checks:
- First identify changed files → Then audit each file
- First verify FBS schema has field → Then check C++ extracts it

### Parallelization Matrix

| Check Type | Can Parallelize With |
|------------|---------------------|
| v3-lively grep | Current branch grep (same file) |
| File A audit | File B audit (different files) |
| FBS schema check | Node.js population check |
| TRANSCODE guard count | Data flow trace |

| Check Type | Must Be Sequential |
|------------|-------------------|
| Schema exists → C++ extracts | Schema must exist first |
| Node.js populates → C++ receives | Population must happen first |
```

---

### Enhancement 4: Add Failure Mode Sections to Feature Experts

Add a **"Silent Failure Modes"** section to each feature expert skill.

**Template to add to each expert**:
```markdown
## Silent Failure Modes

These conditions cause the feature to fail WITHOUT errors or warnings:

### Mode 1: [Name]
**Symptom**: [What you won't see]
**Cause**: [What's broken]
**Detection command**:
```bash
[specific grep/check command]
```
**Expected vs Failure**:
- Expected: [output pattern]
- Failure: [different pattern or empty]

### Mode 2: [Name]
...
```

**Specific failure modes to add**:

**binlogs-expert.md**:
```markdown
## Silent Failure Modes

### Mode 1: Timer Never Created
**Symptom**: No binlog files appear, no errors
**Cause**: TRANSCODE guard around timer creation
**Detection**:
```bash
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: 0 (no guard)
**Failure**: 1 or more (guard present)

### Mode 2: InitLog Called with Empty CallId
**Symptom**: Files not created, no error logged
**Cause**: callId not extracted from FBS or empty in appData
**Detection**:
```bash
grep -A10 "consumersBinLog.InitLog" worker/src/RTC/Transport.cpp | grep "callId"
```
**Expected**: `std::string const callId = this->lively.callId;`
**Failure**: `std::string const callId = "";` or missing

### Mode 3: FillBinLogStats Never Called
**Symptom**: Files created but empty/stale
**Cause**: TRANSCODE guard around OnTimer binlog section
**Detection**:
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | grep -c "ifdef TRANSCODE"
```
**Expected**: 0
**Failure**: 1 or more

### Mode 4: producerBinLogEnabled Not Set
**Symptom**: Producer binlogs not created (consumer binlogs work)
**Cause**: WebRtcTransport doesn't set the flag
**Detection**:
```bash
grep "producerBinLogEnabled = true" worker/src/RTC/WebRtcTransport.cpp
```
**Expected**: At least 1 match
**Failure**: No matches
```

**stats-expert.md**:
```markdown
## Silent Failure Modes

### Mode 1: producerStats Option Not in FBS
**Symptom**: Stats never emitted even with appData.producerStats=true
**Cause**: FBS schema missing field
**Detection**:
```bash
grep "producer_stats" worker/fbs/transport.fbs
```
**Expected**: `producer_stats: bool`
**Failure**: No match

### Mode 2: lastProducerStatsReport Never Initialized
**Symptom**: 10-second check always false
**Cause**: Initialization code missing or conditional wrong
**Detection**:
```bash
grep -A3 "producerStats()" worker/src/RTC/Transport.cpp | grep "lastProducerStatsReport"
```
**Expected**: `this->lastProducerStatsReport = DepLibUV::GetTimeMs();`
**Failure**: No match or different assignment

### Mode 3: Node.js Handler Missing
**Symptom**: C++ emits but Node.js never fires event
**Cause**: PRODUCER_STATS case missing in Producer.ts
**Detection**:
```bash
grep "PRODUCER_STATS" node/src/Producer.ts
```
**Expected**: `case Event.PRODUCER_STATS:`
**Failure**: No match
```

**Add similar sections to**: textlogs-expert.md, appdata-expert.md, transcode-features-expert.md

---

### Enhancement 5: Add Data Field Verification Commands to data-flow-auditor.md

Replace the conceptual data flow diagrams with **concrete verification commands**.

**Add this section**:
```markdown
## Field Verification Commands

### Verify: callId
```bash
# 1. FBS Schema
grep "call_id" worker/fbs/transport.fbs
# Expected: call_id: string; (in Options table)

# 2. Node.js Population (Router.ts)
grep "callId" node/src/Router.ts | grep -E "appData|OptionsT"
# Expected: (appData as any)?.callId ?? null

# 3. C++ Extraction (Transport.cpp)
grep -A2 "options->callId()" worker/src/RTC/Transport.cpp
# Expected: this->lively.callId.assign(options->callId()->str());

# 4. Usage in Feature
grep "lively.callId" worker/src/RTC/Transport.cpp
# Expected: Multiple matches including ConsumerFileName lambda
```
- [ ] Schema defines field
- [ ] Node.js populates field
- [ ] C++ extracts field
- [ ] Feature uses field

### Verify: userId
```bash
# 1. FBS Schema
grep "user_id" worker/fbs/transport.fbs
# Expected: user_id: string; (in ProduceRequest table)

# 2. Node.js Population (Transport.ts)
grep -A5 "userId" node/src/Transport.ts | grep "addUserId"
# Expected: ProduceRequest.addUserId(builder, userIdOffset)

# 3. C++ Extraction (Producer.cpp)
grep -A2 "data->userId()" worker/src/RTC/Producer.cpp
# Expected: userId = data->userId()->str();

# 4. Usage in Feature
grep "userId" worker/src/RTC/Producer.cpp | grep -E "binLog|FileName"
# Expected: Used in ProducerFileName lambda
```
- [ ] Schema defines field
- [ ] Node.js populates field
- [ ] C++ extracts field
- [ ] Feature uses field

### Verify: clientReferrer
```bash
# 1. FBS Schema (appears in TWO tables)
grep "client_referrer" worker/fbs/transport.fbs
# Expected: 2 matches - Options AND ProduceRequest

# 2. Node.js Population - Transport Options
grep "clientReferrer" node/src/Router.ts
# Expected: (appData as any)?.clientReferrer ?? null

# 3. Node.js Population - ProduceRequest
grep "clientReferrer" node/src/Transport.ts | grep -E "Offset|add"
# Expected: clientReferrerOffset and addClientReferrer

# 4. C++ Extraction - Transport
grep -A2 "options->clientReferrer()" worker/src/RTC/Transport.cpp
# Expected: clientReferrer.assign(options->clientReferrer()->str());

# 5. C++ Extraction - Producer
grep -A2 "data->clientReferrer()" worker/src/RTC/Producer.cpp
# Expected: clientReferrer = data->clientReferrer()->str();
```
- [ ] Schema defines field (both tables)
- [ ] Node.js populates in Router.ts
- [ ] Node.js populates in Transport.ts
- [ ] C++ extracts in Transport
- [ ] C++ extracts in Producer

### Verify: producerStats
```bash
# 1. FBS Schema
grep "producer_stats" worker/fbs/transport.fbs
# Expected: producer_stats: bool = false;

# 2. Node.js Population
grep "producerStats" node/src/Router.ts
# Expected: (appData as any)?.producerStats ?? false

# 3. C++ Extraction
grep -A3 "options->producerStats()" worker/src/RTC/Transport.cpp
# Expected: if (options->producerStats()) { this->lastProducerStatsReport = ...

# 4. Feature Activation
grep "lastProducerStatsReport" worker/src/RTC/Transport.cpp | head -5
# Expected: Initialization AND 10-second check in OnTimer
```
- [ ] Schema defines field
- [ ] Node.js populates field
- [ ] C++ extracts and uses field
- [ ] Feature activates based on field
```

---

## Output Requirements

1. **Update each skill file in place** - Don't create new files
2. **Preserve existing content** - Add sections, don't remove useful prose
3. **Use consistent formatting**:
   - `## Section` for major additions
   - `### Subsection` for checklist groups
   - ` ```bash ` for commands
   - `- [ ]` for verification checkboxes
4. **Test commands** - All bash commands must be syntactically correct
5. **Add "Last Updated" timestamp** to each modified file

## Constraints

- Do NOT modify CLAUDE.md (only skill files)
- Do NOT change the fundamental purpose of any skill
- Do NOT remove the conceptual explanations (they help understanding)
- Assume the user runs commands from the mediasoup repository root
- Commands must work on macOS and Linux (use portable grep flags)

## Execution Order

1. Read all 11 skill files first
2. Start with `upstream-merge-orchestrator.md` (add Enhancements 2, 3)
3. Update `data-flow-auditor.md` (Enhancement 5)
4. Update feature experts with failure modes (Enhancement 4):
   - binlogs-expert.md
   - stats-expert.md
   - textlogs-expert.md
   - appdata-expert.md
   - transcode-features-expert.md
5. Add mechanical checklists to all skills (Enhancement 1)
6. Update README.md to reflect the new checklist-oriented approach

## Success Criteria

After enhancement, an auditor should be able to:
1. Open `upstream-merge-orchestrator.md` and see exact file list to audit
2. Follow checkbox items mechanically without inventing commands
3. Copy-paste verification commands directly into terminal
4. Compare actual output to documented expected output
5. Identify silent failures by running failure mode detection commands
6. Know which checks can run in parallel vs must be sequential

---

## Example: Before and After

### Before (binlogs-expert.md excerpt):
```markdown
### Audit Checklist

#### Transport.cpp
- [ ] Timer created unconditionally (no TRANSCODE guard)
- [ ] Timer started in constructor
- [ ] `FillBinLogStats()` called in OnTimer unconditionally
```

### After (binlogs-expert.md excerpt):
```markdown
### Audit Checklist: Transport.cpp

#### 1. Timer Creation
```bash
# Check for TRANSCODE guard above timer creation
grep -B3 "binLogTimer = new" worker/src/RTC/Transport.cpp
```
**Expected output**:
```
        // Initialize binary logging timer if not disabled
        if (!Settings::configuration.logBinStatsDisabled)
        {
            this->binLogTimer = new TimerHandle(this);
```
**Failure indicator**: `#ifdef TRANSCODE` appears in output

- [ ] No TRANSCODE guard above timer creation

#### 2. Timer Start in Constructor
```bash
grep -n "binLogTimer->Start" worker/src/RTC/Transport.cpp
```
**Expected**: Line in Connected() or constructor region
- [ ] Timer started appropriately

#### 3. FillBinLogStats in OnTimer
```bash
grep -B5 "FillBinLogStats" worker/src/RTC/Transport.cpp | head -20
```
**Expected output** (no TRANSCODE guard):
```
        else if (!Settings::configuration.logBinStatsDisabled && timer == this->binLogTimer)
        {
            // Collect stats from all Producers
            for (auto& kv : this->mapProducers)
            {
                auto* producer = kv.second;
                if (producer != nullptr)
                {
                    producer->FillBinLogStats();
```
- [ ] FillBinLogStats called unconditionally (no TRANSCODE guard)
```

---

Begin implementation by reading all skill files, then systematically applying each enhancement.
