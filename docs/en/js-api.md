# JavaScript API

coverwise is ESM-only. Two API styles are available:

**Class-based (recommended):**

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [...] });
```

**Function-based:**

```typescript
import { init, generate, analyzeCoverage, extendTests, estimateModel } from '@libraz/coverwise';

await init();
const result = generate({ parameters: [...] });
```

Both styles share the same WASM singleton and are interchangeable.

## `init()`

Initialize the WASM module. Must be called before any other function. Safe to call multiple times — the module loads only once.

```typescript
async function init(): Promise<void>
```

## `generate(input)`

Generate a minimal covering array from parameters and options.

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // Required. At least 1 parameter.
  constraints?: string[];        // Constraint expressions.
  strength?: number;             // Interaction strength. Default: 2 (pairwise).
  seed?: number;                 // RNG seed for determinism. Default: 0.
  maxTests?: number;             // Max test count. 0 = no limit (default).
  weights?: WeightConfig;        // Value weight hints.
  seeds?: TestCase[];            // Existing test cases to build upon.
  subModels?: SubModel[];        // Mixed-strength sub-models.
}
```

### Parameter

```typescript
interface Parameter {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // Mark as invalid for negative testing.
  aliases?: string[];    // Alternate names for this value.
  class?: string;        // Equivalence class name.
}
```

**Simple values:**

```typescript
{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }
```

**Rich values:**

```typescript
{
  name: 'browser',
  values: [
    'Chrome',
    { value: 'IE', invalid: true },
    { value: 'Chromium', aliases: ['Chrome', 'Edge'] },
    { value: 'Firefox', class: 'gecko' },
  ],
}
```

**Numeric and boolean values:**

```typescript
{ name: 'version', values: [1, 2, 3] }
{ name: 'debug', values: [true, false] }
{ name: 'setting', values: ['auto', 0, true] }  // mixed types
```

Numbers and booleans are automatically converted to strings internally.

### WeightConfig

Hint the generator to prefer certain values when coverage is otherwise equivalent. Higher weight = more likely to appear.

```typescript
interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}
```

```typescript
generate({
  parameters: [/* ... */],
  weights: {
    os: { Windows: 2.0, macOS: 1.0, Linux: 1.0 },
  },
});
```

### SubModel

Override strength for specific parameter groups:

```typescript
interface SubModel {
  parameters: string[];  // Parameter names.
  strength: number;      // Strength for this group.
}
```

```typescript
generate({
  parameters: [/* ... */],
  strength: 2,  // Default pairwise.
  subModels: [
    { parameters: ['os', 'browser', 'arch'], strength: 3 },  // 3-wise for critical group.
  ],
});
```

### GenerateResult

```typescript
interface GenerateResult {
  tests: TestCase[];                // Positive test cases (no invalid values).
  negativeTests?: TestCase[];       // Negative tests (exactly 1 invalid value each).
  coverage: number;                 // Coverage ratio (0.0 – 1.0).
  uncovered: UncoveredTuple[];      // Uncovered tuples with reasons.
  stats: GenerateStats;
  suggestions: Suggestion[];        // Actionable suggestions.
  warnings: string[];               // Performance/configuration warnings.
  strength: number;                 // Actual strength used.
  classCoverage?: ClassCoverage;    // Present when equivalence classes are defined.
}

interface TestCase {
  [parameterName: string]: string | number | boolean;
}

interface GenerateStats {
  totalTuples: number;
  coveredTuples: number;
  testCount: number;
}

interface UncoveredTuple {
  tuple: string[];    // e.g. ["os=Windows", "browser=Safari"]
  params: string[];   // e.g. ["os", "browser"]
  reason: string;
  display: string;    // Human-readable: "os=Windows, browser=Safari"
}

interface Suggestion {
  description: string;
  testCase?: Record<string, string>;
}

interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}
```

## `analyzeCoverage(parameters, tests, strength?)`

Analyze the t-wise coverage of an existing test suite. Independent of the generator — validates any set of tests.

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // Default: 2
): CoverageReport
```

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // What's missing.
}
```

**Example:**

```typescript
const report = analyzeCoverage(
  [
    { name: 'os', values: ['Windows', 'macOS'] },
    { name: 'browser', values: ['Chrome', 'Firefox'] },
  ],
  [{ os: 'Windows', browser: 'Chrome' }],
);
// report.coverageRatio === 0.25  (1 of 4 pairs covered)
// report.uncovered.length === 3
```

## `extendTests(existing, input)`

Extend an existing test suite with additional tests to improve coverage. Existing tests are preserved as-is.

```typescript
function extendTests(
  existing: TestCase[],
  input: GenerateInput,
): GenerateResult
```

The returned `result.tests` contains the existing tests followed by new tests. The delta:

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

## `estimateModel(input)`

Preview model statistics without running generation. Useful for estimating complexity before committing to a full generation.

```typescript
function estimateModel(input: GenerateInput): ModelStats
```

### ModelStats

```typescript
interface ModelStats {
  parameterCount: number;
  totalValues: number;
  strength: number;
  totalTuples: number;
  estimatedTests: number;
  subModelCount: number;
  constraintCount: number;
  parameters: Array<{
    name: string;
    valueCount: number;
    invalidCount: number;
  }>;
}
```

## Constraint Builder

Build constraints programmatically instead of writing strings:

```typescript
import { when, not, allOf, anyOf } from '@libraz/coverwise';

const result = cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    not(allOf(when('os').eq('win'), when('browser').eq('safari'))).toString(),
    when('env').in('staging', 'prod').toString(),
    when('version').gt(3).toString(),
    when('browser').like('chrome*').toString(),
  ],
});
```

See [Constraint Syntax](constraints.md) for the full builder API reference.

## Error Handling

Functions throw `CoverwiseError` on invalid input:

```typescript
interface CoverwiseError {
  code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  message: string;
  detail?: string;
}
```

```typescript
try {
  const result = generate({ parameters: [] });
} catch (e) {
  console.error(e.code, e.message);
  // INVALID_INPUT "At least one parameter is required"
}
```
