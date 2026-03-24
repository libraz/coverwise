# CLI Reference

The `coverwise` command-line tool reads JSON input and writes JSON output.

## Commands

### `generate`

Generate a minimal covering array from a JSON specification.

```bash
coverwise generate <input.json> [> output.json]
```

**Input format:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "strength": 2,
  "seed": 42,
  "maxTests": 0,
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ],
  "weights": {
    "os": { "Windows": 2.0 }
  },
  "seeds": [
    { "os": "Windows", "browser": "Chrome" }
  ],
  "subModels": [
    { "parameters": ["os", "browser"], "strength": 3 }
  ]
}
```

Only `parameters` is required. All other fields are optional.

**Output format:**

```json
{
  "tests": [
    { "os": "Windows", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" }
  ],
  "negativeTests": [],
  "coverage": 1.0,
  "uncovered": [],
  "stats": {
    "totalTuples": 9,
    "coveredTuples": 9,
    "testCount": 6
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

### `analyze`

Analyze the t-wise coverage of an existing test suite.

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>]
```

- `--params` — JSON file with parameter definitions
- `--tests` — JSON file with test cases
- `--strength` — Interaction strength (default: 2)

**Output:**

```json
{
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "display": "os=Windows, browser=Safari"
    }
  ]
}
```

### `extend`

Extend an existing test suite to improve coverage.

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

- `--existing` — JSON file with current test cases
- Output includes original tests + new tests appended

### `stats`

Preview model statistics without running generation.

```bash
coverwise stats <input.json>
```

**Output:**

```json
{
  "parameterCount": 3,
  "totalValues": 8,
  "strength": 2,
  "totalTuples": 29,
  "estimatedTests": 10,
  "subModelCount": 0,
  "constraintCount": 1,
  "parameters": [
    { "name": "os", "valueCount": 3, "invalidCount": 0 },
    { "name": "browser", "valueCount": 3, "invalidCount": 0 },
    { "name": "theme", "valueCount": 2, "invalidCount": 0 }
  ]
}
```

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success. 100% coverage achieved. |
| `1` | Constraint error. |
| `2` | Insufficient coverage (e.g., `maxTests` limit reached). |
| `3` | Invalid input. |

## Parameter Value Formats

Values can be simple strings or objects:

```json
{
  "parameters": [
    {
      "name": "browser",
      "values": [
        "Chrome",
        { "value": "IE", "invalid": true },
        { "value": "Chromium", "aliases": ["Chrome", "Edge"] },
        { "value": "Firefox", "class": "gecko" }
      ]
    }
  ]
}
```

## Piping

Standard Unix piping works:

```bash
# Generate and analyze in one pipeline
coverwise generate input.json | jq '.tests | length'

# Feed output to other tools
coverwise generate input.json | my-test-runner --from-stdin
```

## Examples

```bash
# Basic pairwise generation
coverwise generate input.json

# 3-wise coverage analysis
coverwise analyze --params params.json --tests tests.json --strength 3

# Extend existing tests with constraints
coverwise extend --existing current.json input.json > updated.json

# Quick model size check
coverwise stats input.json | jq '.totalTuples'
```
