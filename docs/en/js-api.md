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

Initialize the WASM module. Must be called before any other function. Safe to call multiple times — the module loads only once. If initialization fails (e.g., WASM file not found), subsequent calls will retry instead of caching the failure.

```typescript
async function init(): Promise<void>
```

## `generate(input)`

Generate a covering test suite from parameters and options.

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // Required. At least 1 parameter.
  constraints?: string[];        // Constraint expressions.
  strength?: number;             // Interaction strength (positive integer). Default: 2 (pairwise).
  seed?: number;                 // RNG seed: uint32 integer [0, 4294967295]. Default: 0.
  maxTests?: number;             // uint32 integer [0, 4294967295]. 0 = no limit (default).
  weights?: WeightConfig;        // Value weight hints.
  seeds?: TestCase[];            // Existing test cases to build upon.
  subModels?: SubModel[];        // Mixed-strength sub-models.
}
```

### Parameter

```typescript
interface ParameterBase {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

type Parameter =
  | (ParameterBase & { type?: never; range?: never; step?: never })
  | (ParameterBase & { type: 'integer'; range: [number, number]; step?: 1 })
  | (ParameterBase & { type: 'float'; range: [number, number]; step?: number });

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // Mark as invalid for negative testing.
  aliases?: string[];    // Alternate names for this value.
  class?: string;        // Equivalence class name.
}
```

`Parameter` is a discriminated union. A discrete parameter has none of `type`, `range`, or `step`; a boundary parameter must specify both `type` and `range`. `range` is inclusive, float `step` defaults to `1.0`, and integer `step` may only be `1`. Partial or mismatched boundary fields are rejected.

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
    { value: 'Chromium', aliases: ['chromium-browser', 'cr'] },
    { value: 'Firefox', class: 'gecko' },
  ],
}
```

Within one parameter, every value and every alias must resolve to a distinct name
once ASCII case is folded, so `Chrome` cannot be both a value and an alias of
`Chromium`, and `chrome` cannot sit alongside `Chrome`.

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

Every weight must be a finite number greater than `0`. `0`, a negative number,
`Infinity` and `NaN` are rejected: weights are used as proportions of the
largest weight in the group, which only has meaning on a positive scale. To keep
a value out of the suite entirely, leave it out of `values` rather than weighting
it `0`.

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
  negativeTests: TestCase[];        // Negative tests (exactly 1 invalid value each). Empty array if none.
  negativeCoverage?: NegativeCoverage; // Feasible single-fault negative-tuple coverage.
  coverage: number;                 // Coverage ratio (0.0 – 1.0).
  uncovered: UncoveredTuple[];      // Uncovered tuples with reasons.
  uncoveredCount: number;           // Total uncovered tuples before diagnostic truncation.
  omittedUncovered: number;         // Number omitted from `uncovered` by that truncation.
  stats: GenerateStats;
  suggestions: Suggestion[];        // Actionable suggestions.
  warnings: string[];               // Warnings (e.g., coverage < 100%, seed count exceeds maxTests).
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
  indices: Array<[number, number]>; // Exact [parameter index, value index] pairs.
  reason: string;
  display: string;    // Human-readable: "os=Windows, browser=Safari"
}

interface NegativeCoverage {
  totalTuples: number;
  coveredTuples: number;
  omittedTuples: number;
  coverageRatio: number;
}

interface Suggestion {
  description: string;
  testCase: Record<string, string>;
}

interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}
```

## `analyzeCoverage(parameters, tests, strength?, constraints?)`

Analyze the t-wise coverage of an existing test suite. Independent of the generator — validates any set of tests.

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // Default: 2
  constraints?: string[],   // Optional constraint DSL strings
): CoverageReport
```

When `constraints` is supplied, the coverage universe contains only tuples that can be completed into a full test case satisfying every constraint. Tuples that cannot be fully completed are excluded from `totalTuples`, `coveredTuples`, and `uncovered`. This matches generation semantics. Analyzing a generated suite yields `coverageRatio === 1.0` only when generation completed successfully with full coverage; it can be lower when `maxTests` caps generation (or generation otherwise reports incomplete coverage).

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // What's missing.
  uncoveredCount: number;         // Total uncovered tuples before diagnostic truncation.
  omittedUncovered: number;       // Number omitted from `uncovered` by that truncation.
  invalidTests: Array<{ testIndex: number; reason: string }>; // Rows excluded from coverage accounting.
}
```

A failure is thrown, never returned: a `CoverageReport` you hold is always a
completed measurement, so `coverageRatio === 0` means nothing was covered rather
than nothing was measured. Embedders of the C++ library read the same report as
a return value and have to tell those two apart themselves; see the
[C++ API](cpp-api.md) for which fields survive each error exit there.

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
  input: ExtendInput,
): GenerateResult
```

```typescript
interface ExtendInput extends GenerateInput {
  mode?: 'strict'; // Default and only supported mode.
}
```

`strict` keeps every existing test exactly as-is and appends only new tests. Any other `mode` value is rejected.

The returned `result.tests` contains the existing tests followed by new tests. The delta:

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

## `estimateModel(input)`

Preview model statistics without running generation. Useful for estimating complexity before committing to a full generation.

`totalTuples` is a raw tuple upper bound before constraint exclusion, but malformed constraints and unknown parameter references are rejected just as they are by `generate`.

`estimatedTests` is a coarse sizing heuristic derived from the largest value count, the strength and the parameter count, capped at `totalTuples`. It is not a bound in either direction — a generated suite may be smaller or larger.

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
import { init, generate, when, not, allOf } from '@libraz/coverwise';

await init();

const result = generate({
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

## Input Validation

Both entry points run the same validation before any engine is reached, so
`@libraz/coverwise` and `@libraz/coverwise/pure` accept and reject exactly the
same inputs, with the same messages. It throws descriptive errors for:

- `strength`: Must be a positive integer. Non-integer, negative, or zero values are rejected.
- `seed`: Must be a uint32 integer in `[0, 4294967295]`.
- `maxTests`: Must be a uint32 integer in `[0, 4294967295]`; `0` means no limit.
- `parameters`: Must be a non-empty array.
- Parameter names: Must be unique, and must stay unique once ASCII case is folded — `os` and `OS` cannot coexist. The folding half of that rule exists because a constraint resolves the parameter it names case-insensitively, so a case-only difference would leave `OS = Windows` pointing at two parameters.
- Values and aliases: Within one parameter, the values and all of their aliases must remain distinct once ASCII case is folded. This rule exists because value lookup is case-insensitive, so a case-only difference would leave `os = WINDOWS` without a single answer.
- `weights`: Each weight must be a finite number greater than `0`. A weights key names its value the same way a row does, so any ASCII case of the value or of one of its aliases works. Two keys of one parameter may not name the same value, since only one of the two weights could apply — unless one of them is spelled exactly as the model declares that value, which settles it and is what lets a weight keyed by an alias sit beside one keyed by the value itself.
- Resource limits apply to public input: at most 1,024 parameters, 16,384 values per parameter, 100,000 test rows, 256 constraints, 64 KiB per string, and 1 MiB of aggregate string data.

### The aggregate budget binds before the row ceiling

The 1 MiB figure is one budget per call, and every string the call reads is
charged against it once, where it is read: the model's names, values, aliases,
class names and constraint expressions, and the **values** of every row you pass
as `tests`, `seeds` or `existing`. Row keys are not charged — a key is a
parameter name, already counted once as a model string. Only string values are
charged; a numeric or boolean row value costs nothing.

That makes the two numbers above interact, and the byte budget is almost always
the one you meet first. 1 MiB spread over 100,000 rows leaves about 10.5 bytes of
row text per row, which only a very narrow model fits. With 5-byte string values
and one value per parameter per row, the two limits meet like this:

| Parameters per row | Rows accepted |
|--------------------|---------------|
| 2 | 100,000 (the row ceiling binds) |
| 3 | 69,902 |
| 10 | 20,969 |
| 100 | 2,094 |

The ceiling is a function of both dimensions, so these figures assume the model's
own strings are small next to the rows; a model with long names or many values
spends part of the same budget and lowers them.

**Read the 100,000-row figure as a ceiling, not as a promise that a suite of that
size is accepted.** A rejection at 20,000 rows is the byte budget doing its job,
not a bug, and the message names the budget rather than the row count. Every
surface charges the same things against the same budget, so these figures hold
for the CLI and for a C++ embedding as well as for both npm entry points.

Shorter value names buy rows directly. If you need a suite the budget will not
hold, analyze it in slices rather than in one call.

## Error Handling

Functions throw `CoverwiseError` on invalid input:

```typescript
class CoverwiseError extends Error {
  readonly code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  readonly detail?: string;
}
```

`CoverwiseError` extends the native `Error`, so `instanceof` works on both the WASM and pure-TS surfaces:

```typescript
import { CoverwiseError } from '@libraz/coverwise';

try {
  const result = generate({ parameters: [] });
} catch (e) {
  if (e instanceof CoverwiseError) {
    console.error(e.code, e.message, e.detail);
    // INVALID_INPUT "At least one parameter is required"
  }
}
```
