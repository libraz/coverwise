# coverwise

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/coverwise/ci.yml?branch=main&label=CI)](https://github.com/libraz/coverwise/actions)
[![codecov](https://codecov.io/gh/libraz/coverwise/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/coverwise)

A modern combinatorial testing engine for designing high-quality test suites with full coverage guarantees.

Generate, analyze, and evolve test cases — in browsers, Node.js, and native C++.

## Why Coverwise?

Most bugs come from unexpected interactions between components. Coverwise ensures those interactions are systematically covered.

- **Prove your test quality** — exact coverage metrics with every missing combination identified
- **Design better tests** — don't just generate, analyze existing suites and extend them incrementally
- **Works everywhere** — browser, CI, backend — zero native dependencies via WASM

For QA engineers, SDETs, and developers who need systematic coverage without combinatorial explosion.

## How It Works

```mermaid
graph LR
    A["Parameters\n+ Constraints\n+ Strength"] --> B["Coverwise\nEngine"]
    B --> C["generate\nMinimal tests"]
    B --> D["analyze\nCoverage gaps"]
    D --> E["extend\nDelta tests"]
```

## Quick Start

### JavaScript / TypeScript

```bash
npm install @libraz/coverwise
```

```typescript
import { Coverwise, when } from '@libraz/coverwise';

const cw = await Coverwise.create();

// Generate a minimal test suite with full pairwise coverage
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
console.log(result.tests);    // 10 tests, 100% coverage
console.log(result.uncovered); // [] — nothing missing

// Already have tests? Measure what they actually cover
const report = cw.analyzeCoverage(parameters, myExistingTests);
console.log(report.coverageRatio); // 0.72
console.log(report.uncovered);     // ["os=Linux, browser=Safari", ...]

// Fill only the gaps — no need to regenerate from scratch
const extended = cw.extendTests(myExistingTests, { parameters, constraints });
console.log(extended.tests.length - myExistingTests.length); // 3 new tests added
```

## What You Can Do

| Capability | Description |
|-----------|-------------|
| **Pairwise & t-wise** | 2-wise through arbitrary strength covering arrays |
| **Constraints** | `IF/THEN/ELSE`, `AND/OR/NOT`, relational (`<`, `>=`), `IN`, `LIKE` |
| **Negative testing** | Mark values as `invalid` to auto-generate single-fault negative tests |
| **Mixed strength** | Sub-models let critical parameter groups get higher coverage |
| **Boundary values** | Auto-expand integer/float ranges into boundary classes |
| **Equivalence classes** | Group values into classes and track class-level coverage |
| **Seed tests** | Build on existing tests instead of starting from scratch |
| **Weight hints** | Prefer specific values when coverage is otherwise equivalent |
| **Coverage analysis** | Validate any test suite's t-wise coverage independently |
| **Deterministic** | Same input + seed = identical output, every time |

## CLI

```bash
# Generate tests from a JSON spec
coverwise generate input.json > tests.json

# Analyze existing test coverage
coverwise analyze --params params.json --tests tests.json

# Extend existing tests
coverwise extend --existing tests.json input.json

# Preview model statistics
coverwise stats input.json
```

Exit codes: `0` OK, `1` constraint error, `2` insufficient coverage, `3` invalid input.

## Performance

Both generation time and test suite size scale well across configurations:

| Scenario | Parameters | Values | Strength | Tests | Time |
|----------|-----------|--------|----------|-------|------|
| Small | 10 | 3 | 2-wise | 23 | 18 ms |
| Medium | 20 | 5 | 2-wise | 71 | 23 ms |
| Large | 30 | 5 | 2-wise | 76 | 28 ms |
| 3-wise | 15 | 3 | 3-wise | 101 | 31 ms |
| 4-wise | 8 | 3 | 4-wise | 228 | 26 ms |
| High cardinality | 5 | 20 | 2-wise | 515 | 29 ms |
| Many parameters | 50 | 3 | 2-wise | 35 | 27 ms |

Measured on Apple M-series. All scenarios complete in under 35 ms with 100% coverage guaranteed.

## Build

```bash
# Native (C++)
make build            # Debug build
make test             # Run tests
make release          # Optimized build

# WebAssembly
make wasm             # Build WASM via Emscripten

# JavaScript
yarn build            # Build WASM + TypeScript
yarn test             # Run JS/WASM tests
```

## License

[Apache License 2.0](LICENSE)
