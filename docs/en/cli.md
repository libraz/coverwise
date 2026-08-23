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
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
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

This is the exact result of the shown input with the bundled generator, wrapped
across lines for readability — the CLI writes each output as a single line. The
constraint makes `os=Windows, browser=Safari` infeasible, leaving eight
requested-strength pairs. `coverage` is a JSON number in shortest round-trip
form, so full coverage arrives as `1` rather than `1.0`. Output ordering is
deterministic for a fixed input and seed, but should not be used as a
cross-version ordering contract.

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
- `--strength` — Interaction strength (default: the `strength` of the `--params` model, otherwise 2)
- `--constraints` — JSON file with constraint strings (optional), replacing the constraints the `--params` model declares. A tuple is excluded from the coverage universe only when it has no completion to a full assignment of valid values satisfying all constraints. Thus a fully covered constrained suite reports 100% without treating a merely partial constraint evaluation as a violation.

`--tests` and `--existing` accept either a bare test array or the schema-v1 envelope emitted by `generate`, so `coverwise generate input.json > tests.json` can be passed to the downstream commands unchanged.

**Where the strength comes from.** When `--params` is a model object rather than a bare parameter
array, its `strength` field defines the coverage universe and is used for the measurement, so the
same model can be piped through `generate` and then `analyze` without restating it. An explicit
`--strength` wins over the model's, because it is an analysis knob chosen for the run rather than a
property of the model. Neither present means pairwise.

A model that declares `subModels` is rejected with exit code `3`. Sub-models give parts of a model
their own strength, which a coverage report cannot express: it measures one universe at one
strength. Analyze each group separately, naming its strength with `--strength`.

**What `--constraints` does to the model's constraints.** An explicit `--constraints` file
*replaces* the constraints declared in `--params`; the two are never merged, so the file always
describes the complete constraint set the measurement uses. The file is either a bare array of
expressions or an object with a `constraints` array. Any other top-level document is rejected with
exit code `3` — including the bare `null` that `jq '.constraints'` writes for a model that has none.
Reading such a document as "no constraints" would measure an unconstrained universe and report a
coverage shortfall with no error output to explain it.

**`--params` file:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ]
}
```

**`--tests` file:**

```json
[
  { "os": "Windows", "browser": "Chrome" },
  { "os": "Windows", "browser": "Firefox" },
  { "os": "macOS", "browser": "Firefox" },
  { "os": "macOS", "browser": "Safari" },
  { "os": "Linux", "browser": "Chrome" },
  { "os": "Linux", "browser": "Firefox" },
  { "os": "Linux", "browser": "Safari" }
]
```

**Output:**

```json
{
  "schemaVersion": 1,
  "totalTuples": 9,
  "coveredTuples": 7,
  "coverageRatio": 0.7777777777777778,
  "uncovered": [
    {
      "tuple": ["os=Windows", "browser=Safari"],
      "params": ["os", "browser"],
      "indices": [[0, 0], [1, 2]],
      "reason": "never covered",
      "display": "os=Windows, browser=Safari"
    },
    {
      "tuple": ["os=macOS", "browser=Chrome"],
      "params": ["os", "browser"],
      "indices": [[0, 1], [1, 0]],
      "reason": "never covered",
      "display": "os=macOS, browser=Chrome"
    }
  ],
  "uncoveredCount": 2,
  "omittedUncovered": 0,
  "invalidTests": []
}
```

Seven of the nine pairs appear in the suite, so `analyze` exits with code `2`.
`uncoveredCount` counts every uncovered tuple, while the `uncovered` array stops
at a fixed diagnostic cap and `omittedUncovered` reports how many were left out;
the array therefore always holds exactly `uncoveredCount - omittedUncovered`
entries. `coverageRatio` is written in shortest round-trip form rather than
rounded for display.

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

**Input:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] },
    { "name": "theme", "values": ["light", "dark"] }
  ],
  "strength": 2,
  "constraints": [
    "IF os = Windows THEN browser != Safari"
  ]
}
```

**Output:**

```json
{
  "schemaVersion": 1,
  "parameterCount": 3,
  "totalValues": 8,
  "strength": 2,
  "totalTuples": 21,
  "estimatedTests": 18,
  "subModelCount": 0,
  "constraintCount": 1,
  "parameters": [
    { "name": "os", "valueCount": 3, "invalidCount": 0 },
    { "name": "browser", "valueCount": 3, "invalidCount": 0 },
    { "name": "theme", "valueCount": 2, "invalidCount": 0 }
  ]
}
```

`totalTuples` counts the pairs before constraint exclusion: 3·3 + 3·2 + 3·2.
`estimatedTests` is a coarse sizing heuristic derived from the largest value
count, the strength and the parameter count, capped at `totalTuples`. It is not
a bound in either direction — `generate` may return fewer test cases than it
reports, as it does for this model, or more.

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
        { "value": "Chromium", "aliases": ["chromium-browser", "cr"] },
        { "value": "Firefox", "class": "gecko" }
      ]
    }
  ]
}
```

Within one parameter, every value and every alias must resolve to a distinct
name once ASCII case is folded. Listing `Chrome` as both a value and an alias of
`Chromium` — or listing `chrome` alongside `Chrome` — is rejected with exit code
`3`, because case-insensitive lookup would have no single answer. The same rule
applies across parameter names.

## Input Limits

Every command applies the same limits to what it reads, and exceeding any of them is exit code `3`:

| Limit | Value |
|-------|-------|
| Parameters per model | 1,024 |
| Values per parameter | 16,384 |
| Rows in a `tests`, `seeds`, or `existing` array | 100,000 |
| Constraint expressions | 256 |
| UTF-8 bytes in one string | 65,536 (64 KiB) |
| UTF-8 bytes in a model's strings, combined | 1,048,576 (1 MiB) |
| Bytes of one JSON document read from a file or standard input | 67,108,864 (64 MiB) |

The combined-bytes budget covers the strings that describe the model — parameter names, values,
aliases, class names, constraint expressions and sub-model parameter names.

The parameter count is what keeps constraint feasibility search bounded: the search walks one
parameter per level, so nothing else limits how deep it can go.

The document bound is a memory guard on reading a file or draining standard input, not part of what
the CLI accepts. It is sized well above what a document meeting the limits above needs, so a real
input reaches one of those limits first and is rejected by the limit it actually exceeded; the
document bound stops a runaway or truncated stream from being read into memory without end.

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
