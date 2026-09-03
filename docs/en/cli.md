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

Only `parameters` is required. All other fields are optional, and they are the
same fields the [JavaScript API](js-api.md) documents as `GenerateInput`:

| Field | Domain |
|-------|--------|
| `strength` | Positive integer. Default `2` (pairwise). |
| `seed` | uint32 integer in `[0, 4294967295]`. Default `0`. |
| `maxTests` | uint32 integer in `[0, 4294967295]`. `0` means no limit, and is the default. Caps the combined positive and negative suite. |
| `constraints` | Array of constraint expressions. See [Constraint Syntax](constraints.md). |
| `weights` | Object mapping a parameter name to an object mapping a value to its weight. Each weight is a finite number greater than `0`. |
| `seeds` | Array of test cases to build upon, in the same object form as the `tests` output. |
| `subModels` | Array of `{ "parameters": [...], "strength": n }`, giving a named group its own strength. |

A parameter is either discrete or a boundary parameter. A boundary parameter
carries `"type": "integer"` or `"type": "float"` together with an inclusive
`"range": [min, max]`, and coverwise expands that range into edge and near-edge
values. `"values"` is still required — usually as an empty array, since the range
supplies the values; anything listed there is kept alongside the expansion.
`"step"` defaults to `1`, and for `"type": "integer"` it may only be `1`.
Declaring `"type"` without `"range"`, or the other way round, is rejected with
exit code `3`.

Individual values may be objects rather than strings; see
[Parameter Value Formats](#parameter-value-formats).

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

**Rows the model does not describe.** A test case naming a parameter or a value
the `--params` model does not declare is not analyzed. It is reported in
`invalidTests` with the reason it was excluded, and it contributes no coverage,
so the ratio is measured over the remaining rows. The full report is still
written to standard output, and `analyze` then exits with code `3` — an invalid
row outranks insufficient coverage, because a suite that does not match its model
has to be corrected before a coverage number about it means anything. Read
`invalidTests` before reading `coverageRatio`.

### `extend`

Extend an existing test suite to improve coverage.

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

- `--existing` — JSON file with current test cases
- `<input.json>` — the same model `generate` reads

**`--existing` file:**

```json
[
  { "os": "Windows", "browser": "Chrome" },
  { "os": "macOS", "browser": "Firefox" }
]
```

**Input format:**

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "strength": 2,
  "seed": 42
}
```

**Output format:**

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Windows", "browser": "Chrome" },
    { "os": "macOS", "browser": "Firefox" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "macOS", "browser": "Chrome" },
    { "os": "Linux", "browser": "Firefox" },
    { "os": "Windows", "browser": "Safari" },
    { "os": "macOS", "browser": "Safari" },
    { "os": "Linux", "browser": "Safari" },
    { "os": "Linux", "browser": "Chrome" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 9,
    "coveredTuples": 9,
    "testCount": 9
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

`extend` returns the same envelope `generate` returns, so the two are
interchangeable downstream. The existing tests come first, unchanged and in the
order they were given, and the new tests follow: the rows after the first
`--existing` count are what the run added. `stats` and `coverage` describe the
combined suite, not the addition.

`extend` reads rows from two places — `--existing` and the model's `seeds` — and
both are charged against the one combined-bytes budget the invocation has. Either
one may fit on its own while the two together do not; see
[Input Limits](#input-limits).

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

The combined-bytes budget is one budget per command, and every string the command reads is charged
against it once, where it is read: parameter names, values, aliases, class names, constraint
expressions, sub-model parameter names, the names a `weights` object spells out, and the values of
every row in a `tests`, `seeds` or `existing` array.

A row is charged for its **values**, never for its keys — a key is a parameter name, already charged
once as a model string, and charging it again per row would count the same text once per row.
Only **string** values are charged; a numeric or boolean row value costs nothing.

**The byte budget binds before the row ceiling for most models.** 100,000 rows is a ceiling, not a
promise that a suite of that size is accepted: 1 MiB spread over 100,000 rows leaves about 10.5
bytes of row text per row, which only a very narrow model fits. With 5-byte string values and one
value per parameter per row, the two limits meet like this:

| Parameters per row | Rows accepted |
|--------------------|---------------|
| 2 | 100,000 (the row ceiling binds) |
| 3 | 69,902 |
| 10 | 20,969 |
| 100 | 2,094 |

The ceiling is a function of both dimensions, so these figures assume the model's own strings are
small next to the rows; a model with long names or many values spends part of the same budget and
lowers them. Exceeding the budget is exit code `3` with `Input strings exceed 1048576 UTF-8 bytes`,
which names the budget rather than the row count, so a rejection well under 100,000 rows is this
limit and not a bug. Shorter value names buy rows directly; a suite the budget will not hold has to
be analyzed in slices.

**The document guard is a third bound, and it normally sits far outside the other two.** It is
applied to a file or a standard-input stream as it is read, before any of it is parsed, so it counts
JSON syntax — every brace, quote, colon and repeated key — while the byte budget counts only the
text a caller supplied. For a wide model the syntax dominates: 100 parameters at 100,002 rows is
about 133 MiB of JSON carrying far less than 1 MiB of row text, and it is refused with
`file '<path>' exceeds the maximum of 67108864 bytes` rather than by either limit above. That is the
one shape where the guard is what a caller meets first, and the message names the document rather
than the rows, so read it as "this file is too large to read", not as a statement about the suite.

The parameter count is what keeps constraint feasibility search bounded: the search walks one
parameter per level, so nothing else limits how deep it can go.

The document bound is a memory guard on reading a file or draining standard input, not part of what
the CLI accepts: it stops a runaway or truncated stream from being read into memory without end. It
sits far outside the other limits for every model of ordinary width, so a caller normally meets one
of those first and is rejected by the limit actually exceeded — with the exception described above,
where a wide model's JSON syntax reaches the guard while its row text is still well inside the byte
budget.

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
