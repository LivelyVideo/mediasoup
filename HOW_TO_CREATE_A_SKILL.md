# How to Create a Skill

This document defines the canonical structure for skills in this repository.

## Skill File Structure

Skills are stored in `.claude/skills/` as markdown files.

### Required Sections

```markdown
# Skill Name

## Overview

This skill provides procedural knowledge for [capability]. Use this skill when [triggers].

**Scope Boundaries:**
- IN SCOPE: what this handles
- OUT OF SCOPE: what to use other skills for

## Input Contract

| Input | Type | Required | Description |
|-------|------|----------|-------------|
| `param` | type | Yes/No | description |

**Validation Rules:**
- Rule 1
- Rule 2

## Output Contract

| Output | Type | Description |
|--------|------|-------------|
| `field` | type | description |

**Output Structure:**
```json
{
  "field": "value"
}
```

## Methodology

### Step 1: Name

Detailed instructions.

```bash
# Command to run
command --flag value
```

**Expected output**: What success looks like
**Failure indicator**: What failure looks like

### Step 2: Name

More instructions.

## Error Handling

| Type | Code | Description | Recovery |
|------|------|-------------|----------|
| VALIDATION | E001 | description | recovery |

## Examples

### Example 1: Success Case

**Scenario:** Description

**Input:**
```json
{input}
```

**Output:**
```json
{output}
```

### Example 2: Failure Case

**Scenario:** What goes wrong

**Output:**
```json
{error output}
```

## Constraints

### Operational Limits
- Limit 1
- Limit 2

### Prohibited Actions
- NEVER action 1
- NEVER action 2

## Output Template

```markdown
## Skill Results

### Summary
{summary}

### Findings
| # | Column | Status |
|---|--------|--------|
| 1 | value | PASS/FAIL |
```
```

### Required Elements

1. **Overview**: Purpose and trigger conditions
2. **Input Contract**: Parameters with types and validation
3. **Output Contract**: Return structure with JSON schema
4. **Methodology**: Step-by-step with commands
5. **Error Handling**: Typed errors with recovery
6. **Examples**: At least 2 (success + failure)
7. **Constraints**: Limits and prohibitions
8. **Output Template**: Formatted result structure

### Audit Skill Pattern

For audit/verification skills, include:

```markdown
## Silent Failure Modes

### Mode 1: Name
**Symptom**: What user observes
**Cause**: Technical cause
**Detection**:
```bash
command to detect
```
**Expected**: Correct output
**Failure**: Incorrect output

## Mechanical Checklist

### 1. Section
#### 1.1 Check Name
```bash
exact command
```
**Expected**: output
- [ ] Checkbox item
```

### Validation Checklist

- [ ] Overview with trigger conditions
- [ ] Input contract with validation rules
- [ ] Output contract with JSON schema
- [ ] Step-by-step methodology
- [ ] Error handling with typed errors
- [ ] 2+ examples including error case
- [ ] Constraints section
- [ ] Output template
- [ ] Single responsibility (no god skills)
- [ ] No emojis
