# CLI reference

Reference for the `coverwise` executable — its four commands, the JSON each one reads and writes, and the exit code each one ends on. It is for a reader driving coverwise from a shell script, a CI job, or a language with no coverwise binding, and it assumes the vocabulary of [Tuples and coverage](primer/tuples-and-coverage.md) and [Strength](primer/strength.md) rather than re-explaining it. [Getting started](getting-started.md) installs the executable and generates a first suite.

Any input path may be `-`, which reads that JSON from standard input. Standard input can be consumed only once, so a command that takes two inputs accepts `-` for one of them, and a second `-` in the same invocation is rejected rather than read as empty.

## Installing the executable

The PyPI package carries the native executable together with a thin Python wrapper. It needs Python 3.10 or newer, and on Linux glibc 2.28 or newer, because the wheels are built as `manylinux_2_28`:

```bash
pip install coverwise
```

Wheels exist for Linux x86_64, Linux aarch64, and macOS 14+ Apple Silicon. The npm package does not carry the executable at all.

Without a Python installation, take the Linux x64 archive from [GitHub Releases](https://github.com/libraz/coverwise/releases). It is a complete install tree rather than a lone binary: `bin/coverwise`, the library, the headers, and the CMake package described in the [C++ API](cpp-api.md). Unpack it and run `bin/coverwise` from where it lands, or copy the tree over a prefix.

On any other platform, build from source. The executable lands at `build/bin/coverwise`, and `cmake --install` places it at `bin/coverwise` under the selected prefix:

```bash
make release
```

## Reading the usage text

`--help` and `-h` write the usage text to standard output and exit `0`, so it can be redirected or piped:

```bash
coverwise --help
```

```text
Usage:
  coverwise generate <input.json>
  coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
  coverwise extend --existing <tests.json> <input.json>
  coverwise stats <input.json>

Any input path may be '-' to read that JSON from standard input.

Exit codes:
  0 = OK (coverage 100%)
  1 = Constraint error
  2 = Insufficient coverage
  3 = Invalid input
```

A wrong invocation writes the same text to standard error and exits `3`.

There is no `--version`. `coverwise --version` is an unknown command, so it writes `Unknown command: --version` followed by the usage text to standard error and exits `3`.

## Commands

### `generate`

Generate a covering test suite from a JSON model.

```bash
coverwise generate <input.json> [> output.json]
```

The model is the single positional argument; `generate` takes no flags.

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

Only `parameters` is required, and it holds at least one parameter. The remaining fields are optional, and they are the same fields the [JavaScript API](js-api.md) documents as `GenerateInput`:

| Field | Domain |
|-------|--------|
| `strength` | Positive integer up to 4294967295, and never more than the parameter count. Default `2` (pairwise). |
| `seed` | uint32 integer in `[0, 4294967295]`. Default `0`. |
| `maxTests` | uint32 integer in `[0, 4294967295]`. `0` means no limit, and is the default. Caps the combined positive and negative suite. |
| `constraints` | Array of constraint expressions. See [Constraint syntax](constraints.md). |
| `weights` | Object mapping a parameter name to an object mapping a value to its weight. Each weight is a finite number greater than `0`. |
| `seeds` | Array of test cases to build upon, in the same object form as the `tests` output. |
| `subModels` | Array of `{ "parameters": [...], "strength": n }`, giving a named group its own strength. |

A parameter object carries `name` and `values`, and a boundary parameter adds `type`, `range` and `step`. A parameter is either discrete or a boundary parameter. A boundary parameter carries `"type": "integer"` or `"type": "float"` together with an inclusive `"range": [min, max]`, and coverwise expands that range into edge and near-edge values. `"values"` is still required — usually as an empty array, since the range supplies the values; anything listed there is kept alongside the expansion. `"step"` defaults to `1`, and for `"type": "integer"` it may only be `1`. Declaring `"type"` without `"range"`, or the other way round, is rejected with exit code `3`.

Individual values may be objects rather than strings; see [Parameter value formats](#parameter-value-formats).

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

This is the exact result of the shown input with the bundled generator, wrapped across lines for readability — the CLI writes each output as a single line. The constraint makes `os=Windows, browser=Safari` infeasible, leaving eight requested-strength pairs. `coverage` is a JSON number in shortest round-trip form, so full coverage arrives as `1` rather than `1.0`. Output ordering is deterministic for a fixed input and seed, but is not a cross-version ordering contract.

Values marked `"invalid": true` are excluded from positive coverage and are handled as separate negative tests. When invalid values are present, output also includes `negativeCoverage` (`totalTuples`, `coveredTuples`, `omittedTuples`, and `coverageRatio`). `maxTests` limits the combined positive and negative suite, so negative generation can be incomplete; inspect `negativeCoverage` and `warnings` rather than assuming every negative tuple was emitted.

### `analyze`

Analyze the t-wise coverage of an existing test suite.

```bash
coverwise analyze --params <params.json> --tests <tests.json> [--strength <n>] [--constraints <file.json>]
```

| Flag | Argument | Default |
|------|----------|---------|
| `--params` | Path, or standard input. A bare parameter array, or an object with `parameters` and optionally `constraints` and `strength`. Required. | — |
| `--tests` | Path, or standard input. A bare test array, or the schema-v1 envelope `generate` writes. Required. | — |
| `--strength` | Positive integer up to 4294967295. | The `strength` the `--params` object declares, otherwise `2` |
| `--constraints` | Path, or standard input. A bare array of expressions, or an object with a `constraints` array. | The constraints the `--params` object declares |

Any other argument is rejected with `unknown argument` and exit code `3`. A tuple is excluded from the coverage universe only when it has no completion to a full assignment of valid values satisfying all constraints, so a fully covered constrained suite reports 100% rather than treating a merely partial constraint evaluation as a violation.

`--tests` and `--existing` accept either a bare test array or the schema-v1 envelope emitted by `generate`, so `coverwise generate input.json > tests.json` can be passed to the downstream commands unchanged.

**Where the strength comes from.** When `--params` is a model object rather than a bare parameter array, its `strength` field defines the coverage universe and is used for the measurement, so the same model can be piped through `generate` and then `analyze` without restating it. An explicit `--strength` wins over the model's, because it is an analysis knob chosen for the run rather than a property of the model. Neither present means pairwise.

A model that declares `subModels` is rejected with exit code `3`. Sub-models give parts of a model their own strength, which a coverage report cannot express: it measures one universe at one strength. Analyze each group separately, naming its strength with `--strength`.

**What `--constraints` does to the model's constraints.** An explicit `--constraints` file *replaces* the constraints declared in `--params`; the two are never merged, so the file always describes the complete constraint set the measurement uses. Any top-level document that is neither a bare array nor an object with a `constraints` array is rejected with exit code `3` — including the bare `null` that `jq '.constraints'` writes for a model that has none. Reading such a document as "no constraints" would measure an unconstrained universe and report a coverage shortfall with no error output to explain it.

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

Seven of the nine pairs appear in the suite, so `analyze` exits with code `2`. `uncoveredCount` counts every uncovered tuple, while the `uncovered` array stops at a fixed diagnostic cap and `omittedUncovered` reports how many were left out; the array therefore always holds exactly `uncoveredCount - omittedUncovered` entries. `coverageRatio` is written in shortest round-trip form rather than rounded for display.

**Rows the model does not describe.** A test case naming a parameter or a value the `--params` model does not declare is not analyzed. It is reported in `invalidTests` with the reason it was excluded, and it contributes no coverage, so the ratio is measured over the remaining rows. The full report is still written to standard output, and `analyze` then exits with code `3` — an invalid row outranks insufficient coverage, because a suite that does not match its model has to be corrected before a coverage number about it means anything. Read `invalidTests` before reading the ratio.

### `extend`

Extend an existing test suite to improve coverage.

```bash
coverwise extend --existing <tests.json> <input.json> [> output.json]
```

| Argument | Value | Default |
|----------|-------|---------|
| `--existing` | Path, or standard input. A bare test array, or the schema-v1 envelope `generate` writes. Required. | — |
| `<input.json>` | Path, or standard input. The same model document `generate` reads. Required. | — |

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

`extend` returns the same envelope `generate` returns, so the two are interchangeable downstream. The existing tests come first, unchanged and in the order they were given, and the new tests follow: the rows after the first `--existing` count are what the run added. `stats` and `coverage` describe the combined suite, not the addition.

`extend` reads rows from two places — `--existing` and the model's `seeds` — and both are charged against the one combined-bytes budget the invocation has. Either one may fit on its own while the two together do not; see [Input limits](limits.md).

### `stats`

Preview model statistics without running generation.

```bash
coverwise stats <input.json>
```

The model is the single positional argument; `stats` takes no flags. It validates constraint syntax and parameter references before reporting a raw, pre-constraint-exclusion tuple estimate.

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

`totalTuples` counts the pairs before constraint exclusion: 3·3 + 3·2 + 3·2. `estimatedTests` is a coarse sizing heuristic derived from the largest value count, the strength and the parameter count, capped at `totalTuples`. It is not a bound in either direction — `generate` may return fewer test cases than it reports, as it does for this model, or more.

## Parameter value formats

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

A value object carries `value` — a string, number or boolean — and optionally `invalid`, `aliases` and `class`. Within one parameter, every value and every alias must resolve to a distinct name once ASCII case is folded. Listing `Chrome` as both a value and an alias of `Chromium` — or listing `chrome` alongside `Chrome` — is rejected with exit code `3`, because case-insensitive lookup would have no single answer. The same rule applies across parameter names.

Case-insensitive lookup is what resolves every value name you write: a value in a `seeds`, `tests` or `existing` row, a `weights` key, and a constraint operand all reach the same value whichever ASCII case they are spelled in, and whether they name the value or one of its aliases. Two `weights` keys of one parameter may not name the same value — only one of the two weights could apply — unless one of them is spelled exactly as the model declares that value, which settles it. `{"Windows": 5, "wINdows": 9}` is accepted and weights `Windows` by `5`; `{"wINdows": 5, "WINDOWS": 9}` is exit code `3`.

## Output fields

Every document above opens with `"schemaVersion": 1`, the CLI's own output schema version. The v1 shape writes empty array fields explicitly, represents suggestions as `{ description, testCase }`, and names the `stats` fields `subModelCount`, `constraintCount` and `parameters`.

`generate` and `extend` write, in this order, `schemaVersion`, `tests`, `uncoveredCount`, `omittedUncovered`, `negativeTests`, `negativeCoverage` when the model declares an invalid value, `coverage`, `uncovered`, `stats`, `classCoverage` when the model declares a class, `suggestions`, `warnings`, `strength`, and `error` when the run failed. `analyze` writes `schemaVersion`, `totalTuples`, `coveredTuples`, `coverageRatio`, `uncovered`, `uncoveredCount`, `omittedUncovered` and `invalidTests`. `stats` writes `schemaVersion`, `parameterCount`, `totalValues`, `strength`, `totalTuples`, `estimatedTests`, `subModelCount`, `constraintCount` and `parameters`.

### Class coverage

A value may declare a `class`, which puts it in an equivalence class with the other values of the same parameter that declare the same one. When any value declares one, `generate` and `extend` add `classCoverage` to the output:

```json
{
  "parameters": [
    {
      "name": "browser",
      "values": [
        { "value": "Chrome", "class": "blink" },
        { "value": "Edge", "class": "blink" },
        { "value": "Firefox", "class": "gecko" }
      ]
    },
    {
      "name": "filesystem",
      "values": [
        { "value": "NTFS", "class": "journaling" },
        { "value": "FAT32", "class": "flat" }
      ]
    }
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [
    { "browser": "Edge", "filesystem": "NTFS" },
    { "browser": "Edge", "filesystem": "FAT32" },
    { "browser": "Firefox", "filesystem": "NTFS" },
    { "browser": "Chrome", "filesystem": "FAT32" },
    { "browser": "Chrome", "filesystem": "NTFS" },
    { "browser": "Firefox", "filesystem": "FAT32" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 6,
    "coveredTuples": 6,
    "testCount": 6
  },
  "classCoverage": {
    "totalClassTuples": 4,
    "coveredClassTuples": 4,
    "classCoverageRatio": 1
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

Class coverage is measured only over the parameters that declare a class, at the model's strength capped to how many of them there are. Both parameters above carry two classes, so the class universe is 2·2 = 4 pairs against the 3·2 = 6 value pairs `stats` counts.

### Negative tests and the test count

`stats.testCount` counts the positive and the negative cases together, while `stats.totalTuples` and `stats.coveredTuples` describe the positive suite alone. A model with one invalid value shows the difference:

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", { "value": "IE", "invalid": true }] }
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [
    { "os": "Linux", "browser": "Chrome" },
    { "os": "Windows", "browser": "Firefox" },
    { "os": "Windows", "browser": "Chrome" },
    { "os": "Linux", "browser": "Firefox" }
  ],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [
    { "os": "Windows", "browser": "IE" },
    { "os": "Linux", "browser": "IE" }
  ],
  "negativeCoverage": {
    "totalTuples": 2,
    "coveredTuples": 2,
    "omittedTuples": 0,
    "coverageRatio": 1
  },
  "coverage": 1,
  "uncovered": [],
  "stats": {
    "totalTuples": 4,
    "coveredTuples": 4,
    "testCount": 6
  },
  "suggestions": [],
  "warnings": [],
  "strength": 2
}
```

`testCount` is 6 — the four rows of `tests` plus the two of `negativeTests` — while `totalTuples` is the 2·2 = 4 pairs over the valid values only. A caller sizing a run from `testCount` is counting both suites; one comparing coverage is reading the positive one.

### The error object

When a `generate` or `extend` run fails, the report is still written and carries a final `error` object. Two constraints that cannot both hold produce one:

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "constraints": [
    "os = Windows",
    "os != Windows"
  ]
}
```

```json
{
  "schemaVersion": 1,
  "tests": [],
  "uncoveredCount": 0,
  "omittedUncovered": 0,
  "negativeTests": [],
  "coverage": 0,
  "uncovered": [],
  "stats": {
    "totalTuples": 0,
    "coveredTuples": 0,
    "testCount": 0
  },
  "suggestions": [],
  "warnings": [
    "Constraints are unsatisfiable: No complete assignment using valid values satisfies all constraints"
  ],
  "strength": 2,
  "error": {
    "code": 1,
    "message": "Constraints are unsatisfiable: No complete assignment using valid values satisfies all constraints"
  }
}
```

The same text also goes to standard error, prefixed with `error: `, and the run exits with code `1`.

`error.code` is a **number**, not the string vocabulary the JavaScript and Python surfaces use: `1` for a constraint error, `2` for insufficient coverage, `3` for invalid input, and `4` for tuple explosion. It is the number the process exits with, except for tuple explosion, which the CLI reports as exit code `3`.

Read `error.message` when a run failed and `warnings` when it did not. The two carry the same text for a failure, so a consumer that reads only `warnings` sees the diagnostic either way; a consumer that branches on success reads `error` and treats `warnings` as advisory.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | The command completed. For `generate`, `analyze` and `extend` that also means coverage reached 100%; for `stats` and `--help` it carries no coverage meaning. |
| `1` | Constraint error. An expression did not parse, or no assignment of valid values satisfies the set. |
| `2` | Insufficient coverage, whatever the reason — a `maxTests` cap, an infeasible tuple, or a suite that leaves pairs uncovered. |
| `3` | Invalid input, a usage error, or a failed write to standard output. |

Exit code `3` covers more than a malformed model. A wrong invocation exits `3`, `analyze` exits `3` when the suite holds rows the model does not describe, and a write to standard output that fails — a reader that closed the pipe, a full disk, a closed descriptor — is reported as `error: cannot write to standard output` and exits `3`.

## Input limits

Every command applies the same limits to what it reads, and exceeding any of them is exit code `3`. The limits, the arithmetic that decides which one binds first, and the message each produces are documented in [Input limits](limits.md).

## Piping

Standard Unix piping works in both directions. `-` in place of an input path reads that JSON from standard input:

```bash
# Generate and count the suite
coverwise generate input.json | jq '.tests | length'

# Feed output to another tool
coverwise generate input.json | my-test-runner --from-stdin

# Build a model on the fly and generate from it
jq '{parameters: .matrix}' config.json | coverwise generate -

# Measure a generated suite without an intermediate file
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

Standard input is consumable only once, so passing `-` for both inputs of one command is rejected rather than silently read as empty.

A reader that stops early ends the run at exit code `3`. `coverwise generate big.json | head -c 200`, a `jq -e` that exits on its first match, or quitting a `less` at the end of the pipe all close the pipe while the report is still being written, and the CLI reports that as `error: cannot write to standard output` with exit code `3`. A whole-line reader such as a bare `head` does not: the CLI writes the document as one line, so the first newline the reader sees is already the end of the output. Read a `3` from a pipeline as "the output did not get written" before reading it as "the input was wrong".

## Examples

```bash
# Basic pairwise generation
coverwise generate input.json

# 3-wise coverage analysis
coverwise analyze --params params.json --tests tests.json --strength 3

# Extend an existing suite against a model
coverwise extend --existing tests.json input.json > updated.json

# Quick model size check
coverwise stats input.json | jq '.totalTuples'
```

## Where to go next

- [Getting started](getting-started.md) — install and generate a first suite on every surface.
- [Constraint syntax](constraints.md) — the expression language the `constraints` array accepts.
- [Input limits](limits.md) — what the CLI accepts, and what it reports at each ceiling.
- [Python API](python-api.md) — the same executable, driven from Python.
- [C++ API](cpp-api.md) — linking the engine instead of running the executable.
- [Glossary](glossary.md) — the vocabulary this page uses.
