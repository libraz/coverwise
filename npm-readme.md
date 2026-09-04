# coverwise

Combinatorial test coverage engine, compiled to WebAssembly with a pure TypeScript engine alongside it. Measure what a test suite already covers, add only the tests that close the gaps, or build a covering suite from scratch. No native dependencies.

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)

coverwise sits between a system whose behaviour depends on a handful of parameters — an operating system, a browser, a deployment target, a feature flag — and a test suite that may or may not exercise their interactions. The parameters and the suite go in, a coverage report comes out, and the same engine writes the rows that report says are missing.

## Install

```bash
npm install @libraz/coverwise
```

Node.js 18 or later, ESM only. In a browser the default entry point needs WebAssembly support; the pure TypeScript entry point needs nothing.

## What it does

`analyze` measures how much of the t-wise interaction space an existing suite covers and names every combination it misses. `extend` adds only the tests that close those gaps, leaving the existing rows in place and in order. `generate` builds a covering suite from scratch.

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

### Generate from scratch

Constraints are written either as expression strings or with the `when` builder, which produces the same strings.

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

### Pure TypeScript (no WASM)

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

The two engines are close on pairwise models, and the WebAssembly engine pulls ahead as strength and tuple count grow.

### Browser (CDN)

```html
<script type="module">
  import { Coverwise } from 'https://cdn.jsdelivr.net/npm/@libraz/coverwise/dist/js/index.js';
  const cw = await Coverwise.create();
  const result = cw.generate({ parameters: [/* ... */] });
</script>
```

Load the package files as published. A CDN that overlays a Node compatibility layer on browser code makes the WASM loader take its Node path, and `Coverwise.create()` then fails to initialize. The pure TypeScript entry point loads no WASM and has no such requirement.

## API

| Method | Description |
|--------|-------------|
| `Coverwise.create()` | Create an instance, loading the WASM module once |
| `cw.analyzeCoverage(params, tests, strength?, constraints?)` | Measure t-wise coverage and list every uncovered combination; constraint-excluded tuples are removed from the universe |
| `cw.extendTests(existing, input)` | Add only the tests needed to close the coverage gaps |
| `cw.generate(input)` | Build a covering suite from scratch |
| `cw.estimateModel(input)` | Preview the size of a model before generating |

A function-based API — `init()` followed by `generate()`, `analyzeCoverage()` and the rest — is also exported, as are the constraint builders from `@libraz/coverwise/constraint`.

## Features

- **Coverage analysis** — Measures any suite's t-wise coverage and lists every uncovered combination
- **Incremental extension** — Adds only the tests needed to close the gaps, preserving the existing rows
- **Pairwise and t-wise** — Covering arrays from 2-wise upwards, at arbitrary strength
- **Constraints** — `IF/THEN/ELSE`, `IMPLIES`, `AND/OR/NOT`, relational, `IN`, `LIKE`
- **Negative testing** — Values marked `invalid` become single-fault negative tests
- **Mixed strength** — Sub-models raise the strength over a critical group of parameters
- **Boundary values** — Numeric ranges expand into edge and near-edge values
- **Equivalence classes** — Values group into classes, and coverage is tracked per class
- **Seed tests** — Generation starts from mandatory tests instead of from nothing
- **Deterministic** — The same valid input and seed produce the same suite on every surface

Generation targets complete coverage, and an independent validator checks the result. It falls short only when the model is unsatisfiable, over a resource limit, or capped by `maxTests`. Tuples that no valid test can reach are excluded from the required universe rather than counted as missing.

WebAssembly is the C++ core compiled, so those two agree by construction, and the pure TypeScript port is held to the WebAssembly surface by a [parity suite](https://github.com/libraz/coverwise/blob/main/js/compat.test.ts).

## What coverwise does not do

- **It does not run your tests.** coverwise produces rows of parameter values. Handing them to a test framework, and deciding what an assertion is, stays on your side.
- **It does not know what your parameters mean.** A value is an opaque label; which combinations are dangerous is domain knowledge the model does not carry.
- **It cannot tell you whether pairwise is strong enough.** Strength is an input. Whether it catches the faults your system actually has is a judgement about your system.
- **It has no opinion about your test framework.** The output is a list of objects, and no runner, report format or directory layout is assumed.

## Documentation

New to combinatorial testing? The [Primer](https://github.com/libraz/coverwise/blob/main/docs/en/primer/index.md) builds the vocabulary the rest of the documentation assumes. Otherwise start at [Introduction](https://github.com/libraz/coverwise/blob/main/docs/en/introduction.md) and [Getting started](https://github.com/libraz/coverwise/blob/main/docs/en/getting-started.md), which index the guides and the reference pages.

- [JavaScript API](https://github.com/libraz/coverwise/blob/main/docs/en/js-api.md) — every published entry point
- [Constraint syntax](https://github.com/libraz/coverwise/blob/main/docs/en/constraints.md) — the constraint language reference
- [Choosing a surface](https://github.com/libraz/coverwise/blob/main/docs/en/choosing-a-surface.md) — WebAssembly, pure TypeScript, native C++, CLI, Python
- [Examples](https://github.com/libraz/coverwise/blob/main/docs/en/examples.md) — copy-pasteable recipes, one per feature
- [Questions and limitations](https://github.com/libraz/coverwise/blob/main/docs/en/faq.md) — what the engine will not guess

## License

[coverwise](https://github.com/libraz/coverwise) is released under the [Apache License 2.0](https://github.com/libraz/coverwise/blob/main/LICENSE).
