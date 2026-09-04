# JavaScript API

Reference for the npm package `@libraz/coverwise` — its four published entry points, every function and type they export, and the rules an input is accepted under. It is for a reader driving coverwise from JavaScript or TypeScript, and it assumes the vocabulary of [Tuples and coverage](primer/tuples-and-coverage.md) and [Strength](primer/strength.md) rather than re-explaining it. [Getting started](getting-started.md) installs the package and generates a first suite.

## Entry points

The package publishes four entry points. Every one of them is ESM; there is no CommonJS build, and Node.js 18 or newer is required.

| Entry point | What it is | What it needs |
|---|---|---|
| `@libraz/coverwise` | The engine compiled to WebAssembly, plus the constraint builder. The default. | A runtime that can load WASM, and one initialization call before the first engine call |
| `@libraz/coverwise/pure` | The same API, implemented in TypeScript. Same names, same results. | Nothing |
| `@libraz/coverwise/constraint` | The constraint builder alone, with no engine behind it. | Nothing |
| `@libraz/coverwise/wasm` | The `.wasm` binary itself, for a bundler to treat as an asset. | A bundler that handles binary assets |

Which one to reach for is its own question; [Choosing a surface](choosing-a-surface.md) answers it, including for the CLI and the Python package.

### The root entry point

Two API styles are available, and they are interchangeable — both drive the same WASM instance.

Class-based:

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [/* ... */] });
```

Function-based:

```typescript
import { init, generate, analyzeCoverage, extendTests, estimateModel } from '@libraz/coverwise';

await init();
const result = generate({ parameters: [/* ... */] });
```

The engine exposes exactly four operations — `generate`, `analyzeCoverage`, `extendTests`, `estimateModel` — plus `init` and the constraint builder. `Coverwise` has a private constructor, and each of its methods delegates to the free function of the same name, so the two styles cannot drift apart.

### `@libraz/coverwise/pure`

The engine ported to TypeScript, for anywhere WASM cannot be loaded or is not wanted. It exports the same names as the root entry point — the same functions, the same class, the same types, out of the same declarations — and `js/export-parity.test.ts` compiles both entry points and asserts the two name sets are identical. A program moves between the two by swapping the import specifier and changing nothing else.

```typescript
import { Coverwise } from '@libraz/coverwise/pure';

const cw = await Coverwise.create();
const result = cw.generate({ parameters: [/* ... */] });
```

Three differences are behavioural rather than nominal. `init()` exists but is a no-op, present so source written against the WASM surface keeps compiling. `Coverwise.create()` is `async` in shape and returns an already-resolved promise; nothing is loaded. Everything after that is fully synchronous and needs no initialization at all.

Results agree across the two engines, not merely the shapes: `js/compat.test.ts` compares whole results for `generate`, `analyzeCoverage`, `extendTests` and `estimateModel`, along with the `code`, `message` and `detail` of every error, and `js/acceptance-parity.test.ts` compares what each one accepts and rejects. The same seed therefore produces the same suite from either entry point. WASM is the C++ core compiled, so the WASM and native results agree by construction rather than by test; it is the TypeScript port that the parity suite holds to that surface. [Determinism](determinism.md) states what this does and does not guarantee.

### `@libraz/coverwise/constraint`

The constraint builder without the engine: `when`, `not`, `allOf`, `anyOf`, and the `Condition`, `ConditionStart`, `Constraint` and `IfConstraint` types. Importing from here pulls in no WASM and no generator, which is what makes it usable in code that only assembles expressions — a rule library, a test-model editor, a script that emits JSON for the CLI.

```typescript
import { when } from '@libraz/coverwise/constraint';

const rule = when('os').eq('Windows').then(when('browser').ne('Safari'));
rule.toString(); // 'IF os = "Windows" THEN browser != "Safari"'
```

This entry point throws `CoverwiseError` — from a non-finite number, from a relational operand that cannot be written as one bare token, and from an empty `in()`, `allOf()` or `anyOf()` — but it does not re-export the class. Code that needs an `instanceof` check on what the builder throws imports `CoverwiseError` from `@libraz/coverwise` or `@libraz/coverwise/pure` as well.

### `@libraz/coverwise/wasm`

The compiled binary itself, with no JavaScript and no types. It exists so a bundler can resolve the `.wasm` file as an asset and hand back its URL, rather than leaving the loader to find it at runtime.

```typescript
// Vite and other bundlers that support the `?url` suffix.
import wasmUrl from '@libraz/coverwise/wasm?url';

// Webpack asset modules and anything else that understands `import.meta.url`.
const wasmUrlFromMeta = new URL('@libraz/coverwise/wasm', import.meta.url);
```

## `init()`

Load the WASM module. It must be called before any other function on the root entry point, and it is safe to call repeatedly — the module loads once.

```typescript
async function init(): Promise<void>
```

A failed load is not cached: the rejected promise is discarded and the next `init()` tries again. Both the failure to load and the failure to have called it are reported as `CoverwiseError` with code `INVALID_INPUT`; see [Error handling](#error-handling).

## `generate(input)`

Build a covering suite from a model.

```typescript
function generate(input: GenerateInput): GenerateResult
```

### GenerateInput

```typescript
interface GenerateInput {
  parameters: Parameter[];       // Required.
  constraints?: string[];        // Constraint expressions.
  strength?: number;             // Interaction strength. Default: 2 (pairwise).
  seed?: number;                 // RNG seed: uint32 integer [0, 4294967295]. Default: 0.
  maxTests?: number;             // uint32 integer [0, 4294967295]. 0 = no limit (default).
  weights?: WeightConfig;        // Value weight hints.
  seeds?: TestCase[];            // Test cases the suite must contain.
  subModels?: SubModel[];        // Mixed-strength sub-models.
}
```

`strength` must be an integer between 1 and the number of parameters. A model with one parameter is therefore rejected at the default strength of 2, and the fix is `strength: 1` rather than an unwanted second parameter. The same bound applies to the `strength` argument of `analyzeCoverage`.

`seed` selects one of the deterministic suites the model admits: the same input and the same seed produce the same suite, on both entry points and on every other surface. `maxTests` caps the suite; a capped run does not throw, it returns with `coverage < 1` and a warning.

### Parameter

```typescript
interface ParameterBase {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

type Parameter =
  | PlainParameter          // ParameterBase & { type?: never; range?: never; step?: never }
  | IntegerBoundaryParameter // ParameterBase & { type: 'integer'; range: [number, number]; step?: 1 }
  | FloatBoundaryParameter;  // ParameterBase & { type: 'float';   range: [number, number]; step?: number }

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;     // Mark as invalid for negative testing.
  aliases?: string[];    // Alternate names for this value.
  class?: string;        // Equivalence class name.
}
```

`Parameter` is a discriminated union of three exported interfaces. A discrete parameter has none of `type`, `range` or `step`; a boundary parameter must specify both `type` and `range`. `range` is inclusive, float `step` defaults to `1.0`, and integer `step` may only be `1`. Partial or mismatched boundary fields are rejected.

Simple values:

```typescript
{ name: 'os', values: ['Windows', 'macOS', 'Linux'] }
```

Rich values:

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

Within one parameter, every value and every alias must resolve to a distinct name once ASCII case is folded, so `Chrome` cannot be both a value and an alias of `Chromium`, and `chrome` cannot sit alongside `Chrome`.

Numeric and boolean values:

```typescript
{ name: 'version', values: [1, 2, 3] }
{ name: 'debug', values: [true, false] }
{ name: 'setting', values: ['auto', 0, true] }  // mixed types
```

Numbers and booleans are converted to strings internally.

`values` is required on every parameter, boundary parameters included. A boundary parameter draws its values from `range`, so the key is written as `values: []` — the empty array is the intended spelling, not an oversight, and it is what satisfies the `Parameter` union. A discrete parameter with an empty `values` is rejected.

### WeightConfig

Hint the generator to prefer certain values when coverage is otherwise equivalent. Higher weight means more likely to appear.

```typescript
interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}
```

Every weight must be a finite number greater than `0`. `0`, a negative number, `Infinity` and `NaN` are rejected: weights are used as proportions of the largest weight in the group, which only has meaning on a positive scale. To keep a value out of the suite entirely, leave it out of `values` rather than weighting it `0`.

```typescript
generate({
  parameters: [/* ... */],
  weights: {
    os: { Windows: 2.0, macOS: 1.0, Linux: 1.0 },
  },
});
```

### SubModel

Raise the strength of one group of parameters without raising it for the whole model.

```typescript
interface SubModel {
  parameters: string[];  // Parameter names.
  strength: number;      // Strength for this group.
}
```

A sub-model's `parameters` must be non-empty, must name parameters the model declares, and must not name one twice. Its `strength` is bounded by its own group, not by the model: an integer between 1 and the length of that `parameters` array. A three-name group therefore accepts `strength: 3` and rejects `strength: 4`, whatever the model's own parameter count is.

```typescript
generate({
  parameters: [/* ... */],
  strength: 2,  // Default pairwise.
  subModels: [
    { parameters: ['os', 'browser', 'arch'], strength: 3 },  // 3-wise for a critical group.
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
  uncovered: UncoveredTuple[];      // Uncovered tuples with reasons, capped for diagnostics.
  uncoveredCount: number;           // Total uncovered tuples before that cap.
  omittedUncovered: number;         // How many the cap left out of `uncovered`.
  stats: GenerateStats;
  suggestions: Array<{ description: string; testCase: Record<string, string> }>;
  warnings: string[];               // e.g. coverage below 100%, seeds dropped at maxTests.
  strength: number;                 // Actual strength used.
  classCoverage?: {                 // Present when equivalence classes are defined.
    totalClassTuples: number;
    coveredClassTuples: number;
    classCoverageRatio: number;
  };
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
```

`uncovered` is a diagnostic list, not an inventory. Past a fixed number of entries it stops growing and the rest are counted instead: `uncoveredCount` is the true total and `omittedUncovered` is what the list left out, so `uncovered.length + omittedUncovered === uncoveredCount` always holds. The cap is a tuning value that bounds what a report costs to build, not part of what coverwise accepts, so it is deliberately not published as a number here — read `omittedUncovered > 0` as "there are more", never `uncovered.length` as "there are exactly this many".

A suite that did not reach full coverage returns rather than throwing. `coverage < 1` is the signal, and `warnings` says why in prose — `Generation stopped at maxTests (…) before reaching 100% coverage` when the cap bound the run, `Generation stopped before reaching 100% coverage` otherwise, and `Seed test count (…) exceeds maxTests (…); some seeds were dropped` when the seeds alone overran the cap.

## `analyzeCoverage(parameters, tests, strength?, constraints?)`

Measure the t-wise coverage of a suite that already exists. It is independent of the generator and enumerates the tuple universe itself, so it will judge a hand-written suite as readily as a generated one.

```typescript
function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,        // Default: 2. Between 1 and the parameter count.
  constraints?: string[],   // Optional constraint DSL strings.
): CoverageReport
```

When `constraints` is supplied, the coverage universe contains only tuples that can be completed into a full test case satisfying every constraint. Tuples that cannot be completed are excluded from `totalTuples`, `coveredTuples` and `uncovered`. This matches generation semantics. Analyzing a generated suite yields `coverageRatio === 1.0` only when generation completed with full coverage; it is lower when `maxTests` capped the run, or when generation otherwise reported incomplete coverage.

### CoverageReport

```typescript
interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;          // 0.0 – 1.0
  uncovered: UncoveredTuple[];    // What is missing, capped for diagnostics.
  uncoveredCount: number;         // Total uncovered tuples before that cap.
  omittedUncovered: number;       // How many the cap left out of `uncovered`.
  invalidTests: Array<{ testIndex: number; reason: string }>; // Rows excluded from coverage accounting.
}
```

A failure is thrown, never returned: a returned `CoverageReport` is always a completed measurement, so `coverageRatio === 0` means nothing was covered rather than nothing was measured. Embedders of the C++ library read the same report as a return value and have to tell those two apart themselves; see the [C++ API](cpp-api.md) for which fields survive each error exit there.

### Rows that land in `invalidTests`

`analyzeCoverage` accepts a suite as it stands, which means it accepts rows it cannot score. Such a row is not an error and does not stop the measurement — it is dropped from the accounting and recorded in `invalidTests` with the index it arrived at and a reason written to be read:

- `missing value for parameter <name>` — the row does not mention a parameter the model declares.
- `value '<text>' is not declared by parameter <name>` — the row names a value the model does not have, usually a spelling the model has since renamed.
- `value <name>=<value> is marked invalid` — the row uses a value declared with `invalid: true`, which belongs in `negativeTests` rather than in a coverage measurement.
- `violates constraint #<n> (constraint evaluation is false or indeterminate)` — the row contradicts the constraints passed alongside it.

The counts are computed over the surviving rows, so a report with a large `invalidTests` and a low `coverageRatio` is describing drift between the suite and the model, not a gap in the suite. Read `invalidTests` before reading the ratio.

Example:

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

Extend a suite with the tests needed to close its gaps. Existing tests are preserved exactly.

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

The returned `result.tests` holds the existing tests followed by the new ones, so the delta is a slice:

```typescript
const result = extendTests(existing, input);
const newTests = result.tests.slice(existing.length);
```

`maxTests` counts the whole result, existing rows included, so a `maxTests` below `existing.length` asks for a suite that cannot contain its own input and is rejected outright — code `INVALID_INPUT`, message `maxTests cannot be smaller than the existing test count`. A `maxTests` equal to or above it is honoured and caps how many rows may be appended.

## `estimateModel(input)`

Preview the size of a model without generating from it.

`totalTuples` is a raw tuple upper bound before constraint exclusion, but malformed constraints and unknown parameter references are rejected exactly as `generate` rejects them.

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

## Constraint builder

`when`, `not`, `allOf` and `anyOf` build a constraint expression instead of spelling one out as a string. They are exported from all three JavaScript entry points, and the types they trade in are what makes the chain safe to write:

```typescript
interface ConditionStart {
  eq(value: string | number | boolean): Condition;
  ne(value: string | number | boolean): Condition;
  gt(value: number | string): Condition;
  gte(value: number | string): Condition;
  lt(value: number | string): Condition;
  lte(value: number | string): Condition;
  in(...values: (string | number | boolean)[]): Condition;
  like(pattern: string): Condition;
}

interface Condition {
  and(other: Condition): Condition;
  or(other: Condition): Condition;
  then(consequence: Condition): IfConstraint;
  implies(consequence: Condition): Constraint;
  toString(): string;
}

interface Constraint {
  toString(): string;
}

interface IfConstraint extends Constraint {
  else(alternative: Condition): Constraint;
}
```

`when()` returns a `ConditionStart`, every operator returns a `Condition`, and `then()` returns an `IfConstraint` while `implies()` returns a plain `Constraint`. That is why `else()` is reachable only after `then()`: the grammar gives no reading to a second `ELSE`, so the type that carries `else()` is produced by nothing else, and a mistaken chain fails to compile rather than emitting an expression the parser will reject.

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

See [Constraint syntax](constraints.md) for the operators, the quoting rules and the full builder reference.

## Exported types

Every type below is exported from `@libraz/coverwise` and from `@libraz/coverwise/pure` alike, and can be imported by name to annotate surrounding code.

| Type | What it names |
|---|---|
| `GenerateInput` | The model and its options, the argument of `generate` and the base of `ExtendInput` |
| `ExtendInput` | `GenerateInput` plus `mode` |
| `Parameter` | The discriminated union of the three parameter shapes |
| `PlainParameter` | A discrete parameter, with `type`, `range` and `step` all `never` |
| `IntegerBoundaryParameter` | A boundary parameter over integers |
| `FloatBoundaryParameter` | A boundary parameter over floats |
| `BoundaryParameter` | Deprecated. The union of the two boundary shapes; name the specific one instead |
| `ParameterValue` | The rich value form, with `invalid`, `aliases` and `class` |
| `SubModel` | One per-group strength override |
| `WeightConfig` | Preference weights, keyed by parameter and then by value |
| `TestCase` | One test as a readable parameter-to-value map |
| `GenerateResult` | What `generate` and `extendTests` return |
| `GenerateStats` | The tuple and test counts inside a `GenerateResult` |
| `CoverageReport` | What `analyzeCoverage` returns |
| `UncoveredTuple` | One gap, with the names, the indices and a display string |
| `NegativeCoverage` | Single-fault negative-tuple metrics |
| `ClassCoverage` | Equivalence-class metrics, the shape of `GenerateResult.classCoverage` |
| `Suggestion` | The shape of one element of `GenerateResult.suggestions` |
| `ModelStats` | What `estimateModel` returns |
| `ParamStats` | The shape of one element of `ModelStats.parameters` |
| `CoverwiseErrorCode` | The union of the four error codes |
| `ConditionStart` | What `when()` returns, before an operator is chosen |
| `Condition` | A composable condition |
| `Constraint` | A finished expression |
| `IfConstraint` | An `IF … THEN` expression, the only one that accepts `else()` |

`ClassCoverage`, `Suggestion` and `ParamStats` name shapes that `GenerateResult` and `ModelStats` declare inline. The shapes are identical, so a value assigns either way; the names exist for annotating surrounding code.

`CoverwiseError` is a class rather than a type, and its constructor is public — `new CoverwiseError(code, message, detail?)` — so an embedder wrapping coverwise can raise the same error type its callers already handle.

## Input validation

Both engine-backed entry points run the same validation before any engine is reached, so `@libraz/coverwise` and `@libraz/coverwise/pure` accept and reject exactly the same inputs, with the same messages. The checks that run there are:

- `strength`: must be a positive integer. Non-integer, negative and zero values are rejected. The upper bound — no greater than the parameter count — is enforced a layer later, by the engine.
- `seed`: must be a uint32 integer in `[0, 4294967295]`.
- `maxTests`: must be a uint32 integer in `[0, 4294967295]`; `0` means no limit.
- `parameters`: must be an array. That it must also be non-empty is the engine's rule, not this layer's, and arrives with the message `At least one parameter is required`.
- Parameter names: must be unique, and must stay unique once ASCII case is folded — `os` and `OS` cannot coexist. The folding half of that rule exists because a constraint resolves the parameter it names case-insensitively, so a case-only difference would leave `OS = Windows` pointing at two parameters.
- Values and aliases: within one parameter, the values and all of their aliases must remain distinct once ASCII case is folded. Value lookup is case-insensitive, so a case-only difference would leave `os = WINDOWS` without a single answer.
- `weights`: each weight must be a finite number greater than `0`. A weights key names its value the same way a row does, so any ASCII case of the value or of one of its aliases works. Two keys of one parameter may not name the same value, since only one of the two weights could apply — unless one of them is spelled exactly as the model declares that value, which settles it and is what lets a weight keyed by an alias sit beside one keyed by the value itself.
- Resource limits: the counts and byte budgets that bound a call — parameters, values per parameter, test rows, constraints, per-string bytes and aggregate string bytes — are documented once, in [Input limits](limits.md), and apply identically to both entry points.

The bounds the engine adds on top are the ones stated with each option above: `strength` against the parameter count, sub-model strength against its own group, and `maxTests` against `existing.length` in `extendTests`.

## Error handling

Every failure arrives as a thrown `CoverwiseError`. Nothing is reported by return value.

```typescript
class CoverwiseError extends Error {
  readonly code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  readonly detail?: string;
}
```

`CoverwiseError` extends the native `Error`, and the prototype chain is restored in the constructor, so `instanceof` survives transpilation and works on both engine-backed entry points:

```typescript
import { CoverwiseError, generate } from '@libraz/coverwise';

try {
  const result = generate({ parameters: [] });
} catch (e) {
  if (e instanceof CoverwiseError) {
    console.error(e.code, e.message, e.detail);
    // INVALID_INPUT "At least one parameter is required"
  }
}
```

`detail` carries secondary context when there is any — the offending fragment, the two numbers that conflicted — and is `undefined` when there is none, on every surface.

### Which codes a JavaScript caller can branch on

`CoverwiseErrorCode` has four members because it mirrors the C++ error enum one for one, and the CLI needs all four. Only three of them are ever thrown by a JavaScript engine.

| Code | Reachable from JavaScript | What produces it |
|---|---|---|
| `INVALID_INPUT` | Yes | Anything the validation above rejects, plus the two initialization failures below |
| `CONSTRAINT_ERROR` | Yes | A constraint expression that does not parse, names an unknown parameter, or leaves the model unsatisfiable |
| `TUPLE_EXPLOSION` | Yes | A model whose tuple universe or parameter-combination count exceeds the engine's internal work budget |
| `INSUFFICIENT_COVERAGE` | No | Nothing in either JavaScript engine. It exists to mirror the C++ vocabulary and the CLI exit code |

Branching on `INSUFFICIENT_COVERAGE` writes dead code. A suite that fell short is a returned result, not a throw: check `coverage < 1` and read `warnings`.

`TUPLE_EXPLOSION` is reached by size, not by malformed input, and both of its triggers are internal work budgets rather than published limits — the number of t-wise tuples the model would materialize, and the number of parameter combinations `C(n, t)` produces. Raising `strength` on a wide model is by far the most common way to meet it, because both grow combinatorially in `t`. [Input limits](limits.md) covers what the acceptance limits are, and how they differ from these; the message and `detail` name which budget was exceeded.

Two failures on the root entry point arrive as `INVALID_INPUT` rather than as codes of their own. Calling any function before `init()` throws `coverwise WASM module not initialized. Call await init() first.`, and a WASM module that fails to load throws `coverwise WASM module failed to initialize: <cause>`. The second one is not cached — the next `init()` retries — so a load failure caused by a transient fetch is recoverable without a reload.

### Foreign throws from property access

Reading a field of the object passed in runs the caller's own code — a getter, or a proxy trap on a reactive store or a component's state object. Both the validation pass and the engine call run inside a frame that converts anything foreign into a `CoverwiseError` with code `INVALID_INPUT` and a message naming what the property read threw, so such an exception arrives as a `CoverwiseError` like any other failure. The module handle is reachable nowhere else, so an entry point added later inherits the conversion.

Describing the thrown value is itself defensive: a value that cannot be described through `instanceof`, `message` or `String` is reported as undescribable rather than replaced by a second foreign throw. A single `catch (e) { if (e instanceof CoverwiseError) … }` therefore covers every failure the call can produce, including one raised by a Proxy the model was read through.

## Where to go next

- [Choosing a surface](choosing-a-surface.md) — which of WASM, pure TypeScript, native C++, the CLI or Python to reach for, and what each costs.
- [Constraint syntax](constraints.md) — the expression language, the quoting rules, and the full builder reference.
- [Input limits](limits.md) — the counts and byte budgets a call is accepted under, in one place.
- [Examples](examples.md) — copy-pasteable recipes per feature.
- [Determinism](determinism.md) — what the seed guarantees, across which engines, and what it does not.
- [Glossary](glossary.md) — covering array, tuple universe, strength, coverage unit.
