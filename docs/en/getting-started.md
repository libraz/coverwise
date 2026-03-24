# Getting Started

## Installation

### JavaScript / TypeScript

```bash
npm install @libraz/coverwise
# or
yarn add @libraz/coverwise
```

### C++ (Native)

```bash
git clone https://github.com/libraz/coverwise.git
cd coverwise
make build
```

### CLI

Build from source:

```bash
make build
# Binary at build/coverwise
```

## Your First Test Suite

### JavaScript

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
});

console.log(`Generated ${result.tests.length} tests`);
console.log(`Coverage: ${result.coverage * 100}%`);

for (const test of result.tests) {
  console.log(test);
}
// { os: 'Windows', browser: 'Chrome', theme: 'light' }
// { os: 'macOS', browser: 'Firefox', theme: 'dark' }
// ...
```

### C++

```cpp
#include "coverwise.h"
#include <iostream>

int main() {
  coverwise::model::GenerateOptions opts;
  opts.parameters = {
    {"os",      {"Windows", "macOS", "Linux"}},
    {"browser", {"Chrome", "Firefox", "Safari"}},
    {"theme",   {"light", "dark"}},
  };
  opts.strength = 2;

  auto result = coverwise::core::Generate(opts);

  std::cout << "Tests: " << result.tests.size() << std::endl;
  std::cout << "Coverage: " << result.coverage << std::endl;

  return 0;
}
```

### CLI

Create `input.json`:

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] },
    { "name": "theme", "values": ["light", "dark"] }
  ],
  "strength": 2
}
```

Run:

```bash
coverwise generate input.json > tests.json
```

## Adding Constraints

Real systems have invalid combinations. Add constraints to exclude them:

```typescript
import { when } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'IE'] },
  ],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('os').eq('Linux').then(when('browser').ne('Safari')).toString(),
    when('os').eq('macOS').then(when('browser').ne('IE')).toString(),
    when('os').eq('Linux').then(when('browser').ne('IE')).toString(),
  ],
});
```

coverwise will never produce a test case that violates these constraints.

## Checking Coverage of Existing Tests

If you already have tests, check their pairwise coverage:

```typescript
const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome' },
  { os: 'macOS',   browser: 'Firefox' },
  { os: 'Linux',   browser: 'Safari' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

console.log(`Coverage: ${report.coverageRatio * 100}%`);

if (report.uncovered.length > 0) {
  console.log('Missing combinations:');
  for (const u of report.uncovered) {
    console.log(`  ${u.display}`);
  }
}
```

## Extending Tests

Don't regenerate from scratch — extend what you have:

```typescript
const result = cw.extendTests(existingTests, {
  parameters: [/* ... */],
  constraints: [/* ... */],
});

// result.tests includes your existing tests + new ones
const newTests = result.tests.slice(existingTests.length);
console.log(`Added ${newTests.length} new tests to reach 100% coverage`);
```

## Deterministic Output

Set a seed to get reproducible results:

```typescript
const result = cw.generate({
  parameters: [/* ... */],
  seed: 42,
});
// Same parameters + seed = identical test suite every time
```

## Next Steps

- [Examples](examples.md) — Negative testing, mixed strength, boundary values, and more
- [JavaScript API](js-api.md) — Full API reference
- [Constraint Syntax](constraints.md) — Complete constraint language reference
