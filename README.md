# coverwise

Combinatorial test coverage engine. Measure what a test suite already covers, add only the tests that close the gaps, or build a covering suite from scratch — from TypeScript, Python, C++ or the command line.

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![PyPI](https://img.shields.io/pypi/v/coverwise)](https://pypi.org/project/coverwise/)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![TypeScript](https://img.shields.io/badge/TypeScript-6-blue?logo=typescript)](https://www.typescriptlang.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/coverwise)

![The coverwise test design loop: analyze measures an existing suite, extend adds the tests that close its gaps, and generate builds a covering suite from scratch.](docs/images/test-design-loop.svg)

coverwise sits between a system whose behaviour depends on a handful of parameters — an operating system, a browser, a deployment target, a feature flag — and a test suite that may or may not exercise their interactions. The parameters and the suite go in, a coverage report comes out, and the same engine writes the rows that report says are missing.

## What it does

Three operations over one model. `analyze` measures how much of the t-wise interaction space an existing suite covers and names every combination it misses. `extend` adds only the tests that close those gaps, leaving the existing rows in place and in order. `generate` builds a covering suite from scratch. All three are first-class here, and analysis is independent of the generator, so it judges a suite coverwise did not write as readily as one it did.

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'env',     values: ['staging', 'production'] },
];

// A hand-written suite, as a project would already have it:
const myExistingTests = [
  { os: 'Windows', browser: 'Chrome',  env: 'production' },
  { os: 'macOS',   browser: 'Safari',  env: 'production' },
  { os: 'Linux',   browser: 'Firefox', env: 'staging' },
  { os: 'Windows', browser: 'Firefox', env: 'staging' },
];

const report = cw.analyzeCoverage(parameters, myExistingTests);
report.coverageRatio;         // 0.5238095238095238 (11 of the 21 pairs)
report.uncovered.length;      // 10
report.uncovered[0].display;  // 'os=Windows, browser=Safari'

const extended = cw.extendTests(myExistingTests, { parameters });
extended.tests.length;  // 11 — the four rows above, then seven that close the gaps
extended.coverage;      // 1
```

The four hand-written rows are the first four rows of `extended.tests`. `extend` neither reorders nor rewrites what it was given, so the seven additions can be reviewed on their own.

## Use cases

| Starting point | Where coverwise comes in | Guide |
|---|---|---|
| A hand-written suite and no measurement of what it misses | Reports the t-wise coverage ratio and names every absent combination | [Audit an existing suite](docs/en/use-cases/audit-an-existing-suite.md) |
| A suite with known gaps that is not worth rewriting | Adds only the rows that close the gaps, then measures again | [Close the gaps incrementally](docs/en/use-cases/close-the-gaps-incrementally.md) |
| A pytest module parametrized over a full cross-product | Replaces the cross-product with a covering suite over the same parameters | [Replace a cross-product in pytest](docs/en/use-cases/replace-a-cross-product-in-pytest.md) |

## Install

```bash
npm install @libraz/coverwise
```

Node.js 18 or later, ESM only. In a browser the default entry point needs WebAssembly support; the pure TypeScript entry point needs nothing.

```bash
pip install coverwise
```

Python 3.10 or later. Wheels carry the native `coverwise` executable and are built for Linux x86_64 and aarch64 (manylinux_2_28, so glibc 2.28 or newer) and for macOS 14 or later on Apple Silicon. The npm package installs no executable.

Linux x64 archives of the executable are attached to each [GitHub Release](https://github.com/libraz/coverwise/releases). Building from source needs a C++17 compiler with floating-point `std::to_chars` — GCC 11, Clang 10 or AppleClang 14 and later.

## JavaScript and TypeScript

`generate` takes the same parameter list and produces a suite that covers every valid pair. Constraints are written either as expression strings or with the `when` builder, which produces the same strings.

```typescript
import { Coverwise, when } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
  ],
});

result.tests.length;  // 9
result.coverage;      // 1
```

`@libraz/coverwise/pure` is the same engine ported to TypeScript, for runtimes that cannot load WebAssembly or would rather not. The API is identical and `Coverwise.create()` resolves without loading anything.

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  ],
});

result.tests.length;  // 9
```

The two JavaScript engines are close on pairwise models, and the WebAssembly engine pulls ahead as strength and tuple count grow. [Choosing a surface](docs/en/choosing-a-surface.md) compares all five surfaces and says when each is the right one.

## Python

The PyPI package ships the native executable together with a Python API over the same JSON contract. `generate`, `analyze_coverage`, `extend_tests` and `estimate_model` take and return plain dictionaries.

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)

len(result["tests"])  # 8
result["coverage"]    # 1
```

`coverwise.parametrize` turns a model into pytest cases, replacing a hand-written cross-product with a covering suite.

```python
@coverwise.parametrize({"os": ["Windows", "macOS"], "browser": ["Chrome", "Firefox"]})
def test_login(os, browser):
    assert login(os, browser).ok
```

## Command line

`pip install coverwise` installs the same executable the C++ build produces. A source build writes it to `build/bin/coverwise`, and `cmake --install build` places it at `bin/coverwise` under the chosen prefix.

```bash
# Measure the coverage of an existing suite
coverwise analyze --params params.json --tests tests.json

# Add the tests that close the gaps
coverwise extend --existing tests.json input.json

# Build a covering suite from scratch
coverwise generate input.json > tests.json

# Preview the size of a model before generating
coverwise stats input.json
```

Any input path may be `-`, which reads that JSON from standard input, so commands compose without intermediate files.

```bash
coverwise generate input.json | coverwise analyze --params input.json --tests -
```

`coverwise --help` prints the usage above on standard output and exits `0`. There is no `--version` flag.

Exit codes are `0` for success, `1` for a constraint error, `2` for insufficient coverage and `3` for invalid input. Exit `3` also covers a usage error and a failed write to standard output, which is what a reader closing the pipe looks like from inside the process.

## Capabilities

| Capability | Description |
|------------|-------------|
| **Coverage analysis** | Measures any suite's t-wise coverage and lists every uncovered combination. |
| **Incremental extension** | Adds only the tests needed to close the gaps, preserving the existing rows. |
| **Pairwise and t-wise** | Covering arrays from 2-wise upwards, at arbitrary strength. |
| **Constraints** | `IF/THEN/ELSE`, `IMPLIES`, `AND/OR/NOT`, relational (`<`, `>=`), `IN`, `LIKE`. |
| **Negative testing** | Values marked `invalid` become single-fault negative tests. |
| **Mixed strength** | Sub-models raise the strength over a critical group of parameters. |
| **Boundary values** | Numeric ranges expand into edge and near-edge values. |
| **Equivalence classes** | Values group into classes, and coverage is tracked per class. |
| **Seed tests** | Generation starts from mandatory tests instead of from nothing. |
| **Deterministic** | The same valid input and seed produce the same suite on every surface. |

WebAssembly is the C++ core compiled, so those two agree by construction, and the pure TypeScript port is held to the WebAssembly surface by a [parity suite](js/compat.test.ts). [Determinism](docs/en/determinism.md) states what each of those carries as a guarantee and what it does not.

## Performance

Generation is greedy and approximate: it targets complete t-wise coverage rather than the smallest possible suite, and typically produces 1.5 to 2.5 times the theoretical minimum number of tests. Cost is governed by the size of the tuple universe, which grows with strength far faster than with parameter count.

[Performance](docs/en/performance.md) publishes the tuple and test counts the bundled generator produces across a range of configurations, and explains what those figures do and do not predict about a model of your own.

## Documentation

New to combinatorial testing? The [Primer](docs/en/primer/index.md) builds the vocabulary the rest of the documentation assumes — why the full cross-product is untestable, what a tuple and a covering array are, what strength buys and costs, and what a constraint does to the space being covered.

### Start here

- [Introduction](docs/en/introduction.md) — what coverwise is, the loop, and what it leaves to you
- [Getting started](docs/en/getting-started.md) — install and a first complete suite on every surface
- [Use cases](docs/en/use-cases/index.md) — worked guides, each starting from data you already have

### Guides

- [Examples](docs/en/examples.md) — copy-pasteable recipes, one per feature
- [Constraint syntax](docs/en/constraints.md) — the constraint language reference
- [Choosing a surface](docs/en/choosing-a-surface.md) — WebAssembly, pure TypeScript, native C++, CLI, Python
- [Determinism](docs/en/determinism.md) — what the seed guarantees, and across which engines
- [Performance](docs/en/performance.md) — the benchmark tables and how to read them
- [Input limits](docs/en/limits.md) — the input limits and what happens at each

### Reference

- [JavaScript API](docs/en/js-api.md)
- [Python API](docs/en/python-api.md)
- [C++ API](docs/en/cpp-api.md)
- [CLI reference](docs/en/cli.md)
- [Glossary](docs/en/glossary.md)
- [Questions and limitations](docs/en/faq.md)

## What coverwise does not do

- **It does not run your tests.** coverwise produces rows of parameter values. Handing them to a test framework, and deciding what an assertion is, stays on your side. The Python package ships a pytest helper; nothing else in the project assumes a runner, a report format or a directory layout.
- **It does not know what your parameters mean.** A value is an opaque label. Which combinations are dangerous, which are impossible for reasons no constraint states, and which deserve a test of their own regardless of coverage are all domain knowledge the model does not carry.
- **It cannot tell you whether pairwise is strong enough.** Strength is an input. The engine reports what it covered at the strength it was asked for; whether that strength catches the faults your system actually has is a judgement about your system, not about the covering array.
- **It has no opinion about your test framework.** The output is a list of dictionaries. Turning that into parametrized cases, fixtures, table-driven tests or a CSV file is left to the caller, and no surface is privileged over another.

## Build

```bash
# Native C++
make build            # Debug build
make release          # Optimized build
make test             # Python wheel, then the full C++ suite
cmake --install build --prefix ./install   # Library, headers, CMake package and CLI

# WebAssembly and JavaScript
make wasm             # Emscripten build of the WASM module
yarn build            # WASM plus the TypeScript wrapper
yarn test             # Vitest, including the WASM and pure TypeScript suites

# Python binding
make python-wheel     # Build the wheel around the native executable
make test-python      # Build the wheel, then run pytest against it

# Checks
make format           # Auto-fix C++, TypeScript and Python formatting
make format-check     # The same checks, writing nothing
make lint             # Static checks that are not formatting
make coverage         # C++ line coverage, written under build/coverage
make preflight        # Every gate CI runs, cheapest first
```

`make test` builds the Python wheel before running the C++ suite, so it needs `rye` on the path. To run the C++ tests alone, use `ctest --test-dir build` after `make build`.

## License

[coverwise](https://github.com/libraz/coverwise) is released under the [Apache License 2.0](LICENSE).
