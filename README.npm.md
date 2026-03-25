# coverwise

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/github/license/libraz/coverwise)](https://github.com/libraz/coverwise/blob/main/LICENSE)

Combinatorial test coverage engine via WebAssembly. Analyzes existing tests for coverage gaps, generates minimal test suites, and extends tests incrementally — no native dependencies.

## Install

```bash
npm install @libraz/coverwise
```

## Usage

### Analyze existing tests

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const report = cw.analyzeCoverage({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  tests: myExistingTests,
});

report.coverageRatio;  // 0.72
report.uncovered;      // ["os=Linux, browser=Safari", "os=Linux, env=production", ...]
```

### Extend with missing coverage

```typescript
const result = cw.extendTests({
  parameters,
  existing: myExistingTests,
});

result.tests.length - myExistingTests.length;  // 3 tests added
result.coverage;   // 1.0
result.uncovered;  // []
```

### Generate from scratch

```typescript
import { when } from '@libraz/coverwise';

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
```

### Browser (CDN)

```html
<script type="module">
  import { Coverwise } from 'https://esm.sh/@libraz/coverwise';
  const cw = await Coverwise.create();
  const result = cw.generate({ parameters: [/* ... */] });
</script>
```

## API

| Method | Description |
|--------|-------------|
| `Coverwise.create()` | Create instance (loads WASM once) |
| `cw.analyzeCoverage(params, tests, strength?)` | Measure t-wise coverage, list uncovered combinations |
| `cw.extendTests(existing, input)` | Add only the tests needed to close coverage gaps |
| `cw.generate(input)` | Generate minimal covering array from scratch |
| `cw.estimateModel(input)` | Preview model statistics |

Function-based API (`init()` + `generate()`, `analyzeCoverage()`, ...) is also available.

## Features

- **Coverage analysis** — Measure any test suite's t-wise coverage and list every uncovered combination
- **Incremental extension** — Add only the tests needed to close coverage gaps
- **Pairwise & t-wise** — 2-wise through arbitrary strength
- **Constraints** — `IF/THEN/ELSE`, `AND/OR/NOT`, relational, `IN`, `LIKE`
- **Negative testing** — Auto-generate single-fault tests from `invalid` values
- **Mixed strength** — Sub-models for critical parameter groups
- **Boundary values** — Auto-expand integer/float ranges
- **Equivalence classes** — Class-level coverage tracking
- **Seed tests** — Build on existing tests incrementally
- **Deterministic** — Same input + seed = same output

## Requirements

- Node.js >= 18 or modern browser with WASM support
- ESM only (`"type": "module"`)

## License

[Apache License 2.0](https://github.com/libraz/coverwise/blob/main/LICENSE)
