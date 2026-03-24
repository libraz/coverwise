# Examples

Practical recipes for common testing scenarios.

## Basic Pairwise Generation

The most common case — generate a minimal test set covering all parameter pairs:

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'language', values: ['en', 'ja', 'de'] },
    { name: 'theme',    values: ['light', 'dark'] },
  ],
});

console.log(`${result.tests.length} tests cover all ${result.stats.totalTuples} pairs`);
```

## Constraints: Excluding Invalid Combinations

Prevent impossible combinations from appearing in tests:

```typescript
import { when, not, allOf } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'iOS', 'Android'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'device',  values: ['desktop', 'tablet', 'phone'] },
  ],
  constraints: [
    when('os').eq('iOS').then(when('browser').eq('Safari')).toString(),
    when('os').eq('iOS').then(when('device').ne('desktop')).toString(),
    when('os').eq('Android').then(
      allOf(when('browser').ne('Safari'), when('browser').ne('Edge'))
    ).toString(),
    when('device').eq('desktop').then(
      allOf(when('os').ne('iOS'), when('os').ne('Android'))
    ).toString(),
  ],
});
```

See the [Constraint Syntax](constraints.md) reference for the full expression language.

## Negative Testing

Mark values as `invalid` to auto-generate negative test cases. Each negative test contains exactly one invalid value, ensuring single-fault isolation:

```typescript
const result = cw.generate({
  parameters: [
    { name: 'email', values: [
      'user@example.com',
      { value: '', invalid: true },
      { value: 'not-an-email', invalid: true },
    ]},
    { name: 'password', values: [
      'Str0ng!Pass',
      { value: '', invalid: true },
      { value: 'short', invalid: true },
    ]},
    { name: 'role', values: ['admin', 'user', 'guest'] },
  ],
});

console.log('Positive tests:', result.tests.length);
console.log('Negative tests:', result.negativeTests?.length);

// Positive tests cover valid combinations only.
// Negative tests each have exactly 1 invalid value.
```

## Mixed Strength (Sub-Models)

Apply higher coverage to critical parameter groups while keeping pairwise elsewhere:

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'protocol', values: ['HTTP/1.1', 'HTTP/2', 'HTTP/3'] },
    { name: 'auth',     values: ['none', 'basic', 'oauth'] },
    { name: 'cache',    values: ['enabled', 'disabled'] },
    { name: 'compress', values: ['gzip', 'br', 'none'] },
  ],
  strength: 2,  // Default: pairwise.
  subModels: [
    // 3-wise for the critical networking trio.
    { parameters: ['protocol', 'auth', 'cache'], strength: 3 },
  ],
});
```

## Equivalence Classes

Group values into classes to track class-level coverage:

```typescript
const result = cw.generate({
  parameters: [
    { name: 'age', values: [
      { value: '5',  class: 'child' },
      { value: '10', class: 'child' },
      { value: '25', class: 'adult' },
      { value: '40', class: 'adult' },
      { value: '70', class: 'senior' },
    ]},
    { name: 'plan', values: [
      { value: 'free',    class: 'unpaid' },
      { value: 'trial',   class: 'unpaid' },
      { value: 'monthly', class: 'paid' },
      { value: 'annual',  class: 'paid' },
    ]},
  ],
});

if (result.classCoverage) {
  console.log(`Class coverage: ${result.classCoverage.classCoverageRatio * 100}%`);
  // Tracks coverage at the class level (child×unpaid, child×paid, etc.)
}
```

## Weight Hints

Influence value selection when multiple candidates offer equal coverage:

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
  ],
  weights: {
    os: { Windows: 3.0, macOS: 1.0, Linux: 1.0 },
    browser: { Chrome: 2.0 },
  },
});
// Windows and Chrome will appear more frequently in the output.
```

## Seed Tests: Building on Existing Tests

Start from mandatory tests and fill in the gaps:

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  seeds: [
    // These tests must be in the output.
    { os: 'Windows', browser: 'Chrome', env: 'production' },
    { os: 'macOS',   browser: 'Safari', env: 'production' },
  ],
});
// Seeds are included first, then additional tests fill coverage gaps.
```

## Boundary Value Expansion

Auto-expand numeric ranges into boundary value classes:

```json
{
  "parameters": [
    {
      "name": "port",
      "type": "integer",
      "range": [1, 65535],
      "step": 1
    },
    {
      "name": "timeout",
      "type": "float",
      "range": [0.1, 30.0],
      "step": 0.1
    }
  ]
}
```

This auto-generates values like `1`, `2`, `65534`, `65535` (boundaries) for `port`.

## Model Estimation

Check complexity before generating:

```typescript
const stats = cw.estimateModel({
  parameters: [
    { name: 'a', values: ['1', '2', '3', '4', '5'] },
    { name: 'b', values: ['1', '2', '3', '4', '5'] },
    { name: 'c', values: ['1', '2', '3', '4', '5'] },
    { name: 'd', values: ['1', '2', '3', '4', '5'] },
  ],
  strength: 3,
});

console.log(`Parameters: ${stats.parameterCount}`);
console.log(`Total 3-wise tuples: ${stats.totalTuples}`);
console.log(`Estimated tests: ${stats.estimatedTests}`);
```

## CI Integration

Use the CLI in CI pipelines:

```bash
# Generate tests and fail CI if coverage is insufficient.
coverwise generate input.json > tests.json
# Exit code 2 = insufficient coverage (when maxTests is set).

# Validate that manually written tests have full pairwise coverage.
coverwise analyze --params params.json --tests tests.json
# Exit code 0 = 100% coverage, 2 = gaps found.
```

```yaml
# GitHub Actions example
- name: Validate test coverage
  run: |
    coverwise analyze --params params.json --tests tests.json
```
