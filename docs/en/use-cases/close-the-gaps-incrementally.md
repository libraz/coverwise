# Close the gaps incrementally

`extendTests` takes the rows a suite already has, leaves every one of them exactly as it was, and appends only the rows coverage still needs. Analysis before, extension, analysis after: the same model answers all three, and the third step measures what the second produced.

The vocabulary this page assumes — tuple, coverage ratio, required universe — is taught in [Tuples and coverage](../primer/tuples-and-coverage.md). To read a coverage report field by field, start with [Audit an existing suite](audit-an-existing-suite.md).

## Analyze

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'device',  values: ['phone', 'desktop'] },
];

const existing = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'macOS',   browser: 'Safari',  device: 'phone' },
  { os: 'Linux',   browser: 'Firefox', device: 'desktop' },
];

const before = cw.analyzeCoverage(parameters, existing);

before.totalTuples;    // 21
before.coveredTuples;  // 9
before.coverageRatio;  // 0.42857142857142855 (9 of 21 pairs)
before.uncoveredCount; // 12
```

The 21 is the pair count [Use cases](index.md) derives for this model at strength 2. Three rows, each contributing one pair per parameter pair with no repetition between them, reach 9 of those, and twelve pairs are missing.

## Extend

Continuing in the same module, `extendTests` takes the existing rows and the model, and returns a full suite.

```typescript
const result = cw.extendTests(existing, { parameters, mode: 'strict' });

result.tests.length; // 10
result.coverage;     // 1

const added = result.tests.slice(existing.length);
added.length;        // 7
```

The existing rows are preserved verbatim as the prefix of `result.tests`, in the order they were given, so `result.tests.slice(existing.length)` is exactly the delta and nothing has to be diffed to find it. Those seven rows are the ones to add to the file:

```text
os=Windows, browser=Firefox, device=phone
os=macOS, browser=Firefox, device=desktop
os=macOS, browser=Chrome, device=phone
os=macOS, browser=Safari, device=desktop
os=Linux, browser=Chrome, device=phone
os=Windows, browser=Safari, device=phone
os=Linux, browser=Safari, device=phone
```

Seven rows close twelve gaps because a single row carries one pair per parameter pair, three here, and the construction picks rows that close several at once.

## Re-analyze

```typescript
const after = cw.analyzeCoverage(parameters, result.tests);

after.totalTuples;    // 21
after.coveredTuples;  // 21
after.coverageRatio;  // 1
after.uncoveredCount; // 0
```

`analyzeCoverage` enumerates the required universe on its own rather than reading anything the generator recorded, so a full report on the combined suite is an independent confirmation. Run it against the file the tests are actually written into, once the seven rows have been transcribed, and it also catches a transcription mistake.

## Extend or regenerate

Extension never removes a row. When the existing rows are redundant, that redundancy is carried into the result, and a suite generated from scratch can be smaller.

```typescript
const redundant = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'Windows', browser: 'Chrome',  device: 'phone' },
  { os: 'Windows', browser: 'Firefox', device: 'desktop' },
  { os: 'Windows', browser: 'Safari',  device: 'desktop' },
  { os: 'Windows', browser: 'Safari',  device: 'phone' },
];

cw.extendTests(redundant, { parameters }).tests.length; // 12
cw.generate({ parameters }).tests.length;               // 10
```

Extend when the existing rows carry something the model does not: a known regression, a scenario with a hand-written assertion, a case someone in support asked for. Nothing in the model can reproduce those rows, so the two extra rows are what keeping them costs.

Regenerate when the existing rows carry nothing beyond their values — a suite that was itself generated, or one written by cross-product and never revisited. Regeneration is also the right move after the model changes shape, since rows written against the old model tend to arrive as `invalidTests` rather than as coverage.

## What `strict` mode keeps

`'strict'` is the only supported mode and the default. It keeps every existing row exactly as-is and only appends. Any other value is rejected as invalid input rather than being interpreted, so a suite can never be silently rewritten by a typo in the mode field.

The one shape of extension request that is refused outright is a budget smaller than the suite it is given:

```typescript
cw.extendTests(existing, { parameters, maxTests: 2 });
// CoverwiseError, code INVALID_INPUT:
// maxTests cannot be smaller than the existing test count
```

`maxTests` caps the whole result, not the appended part. Three rows cannot fit in a budget of two without dropping one, and `'strict'` never drops a row.

## Extending under a constraint

`extendTests` takes the same model fields as `generate`, constraints included, and appended rows respect them.

```typescript
const constraints = ['IF os = Windows THEN browser != Safari'];

const guarded = cw.extendTests(existing, { parameters, constraints, mode: 'strict' });

guarded.tests.length;      // 9
guarded.coverage;          // 1
guarded.stats.totalTuples; // 20
```

The constraint removes `os=Windows, browser=Safari` from the required universe, which is why the total is 20 rather than 21. Six rows are appended instead of seven, and none of the nine rows pairs Windows with Safari — including the `os=Windows, browser=Safari, device=phone` row the unconstrained run above appended. A suite that reaches full coverage under a constraint is smaller than one that reaches it without, because there is less to cover.

Constraints are not checked against the rows that were passed in. An existing row that violates a constraint stays in the result, because `'strict'` keeps what it is given; the analysis step is where such a row shows up.

## Where to go next

- [Audit an existing suite](audit-an-existing-suite.md) — the report fields this page reads, one at a time, including `invalidTests`.
- [Constraint syntax](../constraints.md) — the expression language, and what a constraint removes from the required universe.
- [Determinism](../determinism.md) — why the same model and seed produce the same seven rows on every surface.
- [JavaScript API](../js-api.md) — the full shape of `ExtendInput` and `GenerateResult`.
- [CLI reference](../cli.md) — the same loop as `coverwise analyze` and `coverwise extend`, for suites kept in JSON.
