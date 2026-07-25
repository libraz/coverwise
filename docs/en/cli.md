# CLI Reference

The `coverwise` command-line tool reads JSON input and writes JSON output.

Install the native CLI from PyPI on Linux (x86_64 / aarch64) or macOS 14+ Apple Silicon:

```bash
pip install coverwise
```

The npm package does not install the native executable. Linux x64 archives are also available from
[GitHub Releases](https://github.com/libraz/coverwise/releases); source builds place it at
`build/bin/coverwise`, and CMake installs it to `bin/coverwise` under the selected prefix.

Any input path may be `-`, which reads that JSON from standard input. Since
standard input can be consumed only once, a command that takes two inputs accepts
`-` for one of them.

All command outputs use CLI schema version `1` and include `"schemaVersion": 1`.
The v1 migration makes empty array fields explicit, represents suggestions as
`{ description, testCase }`, and uses `subModelCount`, `constraintCount`, and
`parameters` in `stats` output.

## Commands

### `generate`

Generate a covering test suite from a JSON specification.

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
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ]
}
```

Only `parameters` is required. All other fields are optional.

**Output format:**

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Linux", "browser": "Firefox" },
    { "os": "Windows", "browser": "Chrome" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "macOS", "browser": "Chrome" },
    { "os": "macOS", "browser": "Safari" },
    { "os": "Linux", "browser": "Safari" },
    { "os": "Linux", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" }
  ],
  "negativeTests": [],
  "coverage": 1.0,
  "uncovered": [],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "stats": {
    "totalTuples": 8,
    "coveredTuples": 8,
    "testCount": 8
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

This is the exact result of the shown input with the bundled generator. The
constraint makes `os=Windows, browser=Safari` infeasible, leaving eight
requested-strength pairs. Output ordering is deterministic for a fixed input
and seed, but should not be used as a cross-version ordering contract.

Values marked `"invalid": true` are excluded from positive coverage and are
handled as separate negative tests. When invalid values are present, output also
includes `negativeCoverage` (`totalTuples`, `coveredTuples`, `omittedTuples`,
and `coverageRatio`). `maxTests` limits the combined positive and negative
suite, so negative generation can be incomplete; inspect `negativeCoverage` and
`warnings` rather than assuming every negative tuple was emitted.

### `analyze`

Analyze the t-wise coverage of an existing test suite.

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
```

- `--params` — JSON file with parameter definitions
- `--tests` — JSON file with test cases
- `--strength` — Interaction strength (default: 2)
- `--constraints` — JSON file with constraint strings (optional). A tuple is excluded from the coverage universe only when it has no completion to a full assignment of valid values satisfying all constraints. Thus a fully covered constrained suite reports 100% without treating a merely partial constraint evaluation as a violation.

`--tests` and `--existing` accept either a bare test array or the schema-v1 envelope emitted by `generate`, so `coverwise generate input.json > tests.json` can be passed to the downstream commands unchanged.

**Output:**

```json
{
  "schemaVersion": 1,
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "indices": [[0, 0], [1, 2]],
      "reason": "never covered",
      "display": "os=Windows, browser=Safari"
    }
  ],
  "uncoveredCount": 2,
  "omittedUncovered": 0,
  "invalidTests": []
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

`stats` validates constraint syntax and parameter references before reporting a raw (pre-constraint-exclusion) tuple estimate.

```bash
coverwise stats <input.json>
```

**Output:**

```json
{
  "schemaVersion": 1,
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

Standard Unix piping works in both directions. `-` in place of an input path
reads that JSON from standard input:

```bash
# Generate and analyze in one pipeline
coverwise generate input.json | jq '.tests | length'

# Feed output to other tools
coverwise generate input.json | my-test-runner --from-stdin

# Build a model on the fly and generate from it
jq '{parameters: .matrix}' config.json | coverwise generate -

# Measure a generated suite without an intermediate file
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

Standard input is consumable only once, so passing `-` for both inputs of one
command is rejected rather than silently read as empty.

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
