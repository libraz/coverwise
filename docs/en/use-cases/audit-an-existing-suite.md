# Audit an existing suite

A hand-written suite covers whatever combinations its author happened to write down. `analyzeCoverage` measures which t-tuples that suite reaches against a parameter model, and reports the ones it does not. It does not change the suite, and it does not decide whether the model is the right one: the model is a statement about what the system varies over, and it comes from the reader.

The vocabulary this page assumes — tuple, coverage unit, coverage ratio, required universe — is taught in [Tuples and coverage](../primer/tuples-and-coverage.md).

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();

const parameters = [
  { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
  { name: 'device',  values: ['phone', 'desktop'] },
];

const existingTests = [
  { os: 'Windows', browser: 'Chrome',  device: 'desktop' },
  { os: 'macOS',   browser: 'Safari',  device: 'phone' },
  { os: 'Linux',   browser: 'Firefox', device: 'desktop' },
  { os: 'Windows', browser: 'Edge',    device: 'phone' },
  { os: 'macOS',   browser: 'Chrome' },
];

const report = cw.analyzeCoverage(parameters, existingTests);

report.totalTuples;   // 21
report.coveredTuples; // 9
report.coverageRatio; // 0.42857142857142855 (9 of 21 pairs)
```

## Reading the ratio

The denominator is the 21 value pairs this model has at strength 2, derived in [Use cases](index.md). It depends only on the model, never on the suite.

The numerator is 9 because only three of the five rows count. Each of those three rows contributes three pairs — one per parameter pair — and none of the nine repeats, so the suite reaches 9 of 21. The other two rows are excluded before any counting happens, for reasons the report names in `invalidTests`.

The ratio is a fraction of the model, not a grade. A suite at 0.43 against a model that omits half the system is worth less than one at 0.43 against a model that describes it.

## Reading `uncovered`

Continuing in the same module, `uncovered` names each pair the suite never reaches.

```typescript
report.uncoveredCount;       // 12
report.omittedUncovered;     // 0
report.uncovered.length;     // 12
report.uncovered[0].params;  // ['os', 'browser']
report.uncovered[0].tuple;   // ['os=Windows', 'browser=Firefox']
report.uncovered[0].reason;  // 'never covered'
report.uncovered[0].display; // 'os=Windows, browser=Firefox'
```

`params` names the parameters the tuple is drawn from and `tuple` names the value of each, both as arrays, so the entry can be read by code without splitting a string. `display` is the same tuple as one line of text, for a report a person reads. `reason` records why the tuple is not covered; it is `'never covered'` for every entry, because a tuple a constraint makes impossible is removed from the required universe rather than reported here.

`uncovered` is truncated when a model produces more gaps than a diagnostic can usefully carry. `uncoveredCount` is the true total and `omittedUncovered` is how many entries were dropped, so `uncovered.length` equals `uncoveredCount - omittedUncovered`. Read the count when reporting a number and the array when listing gaps.

Each entry is a decision. A pair that matters is a test to write. A pair that cannot occur in the running system is a constraint the model is missing — write it down and the pair leaves the denominator instead of sitting in the gap list forever.

## Rows the model does not recognise

```typescript
report.invalidTests.length;       // 2
report.invalidTests[0].testIndex; // 3
report.invalidTests[0].reason;    // "value 'Edge' is not declared by parameter browser"
report.invalidTests[1].testIndex; // 4
report.invalidTests[1].reason;    // 'missing value for parameter device'
```

A row lands in `invalidTests` when it names a value the model does not declare, when it does not name a value for a parameter the model declares, or when it violates a constraint the analysis was given. `testIndex` is the row's position in the array that was passed in, and `reason` is written to be read by a person. These rows contribute nothing to `coveredTuples`, which is why the ratio above measures three rows rather than five.

A field the model does not declare is not a reason. A row carrying a `locale` key against a model with no `locale` parameter is read for the parameters the model does declare and counted normally, so a suite whose rows hold extra bookkeeping does not have to be stripped before it can be analyzed.

Both reasons point at the same question: which of the two is out of date. Row 3 exercises a browser the model never declared — either the model is missing a value and should gain `Edge`, or the row tests something outside the scope the model describes. Row 4 omits `device` — either the suite predates the parameter, in which case the row needs a value, or the parameter does not belong in this model.

Read `invalidTests` before reading the ratio. Until every row is either recognised or deliberately dropped, the coverage figure describes a suite smaller than the one on disk.

## Analyzing under a constraint

A model that carries constraints is analyzed with them, and the denominator changes.

```typescript
const constraints = ['IF os = Windows THEN browser != Safari'];

const guarded = cw.analyzeCoverage(parameters, existingTests, 2, constraints);

guarded.totalTuples;    // 20
guarded.coveredTuples;  // 9
guarded.coverageRatio;  // 0.45 (9 of 20 pairs)
guarded.uncoveredCount; // 11
```

The constraint makes `os=Windows, browser=Safari` impossible, so that pair leaves the required universe: 21 becomes 20, and the gap list drops from 12 entries to 11. The suite did not change and neither did what it covers — only the denominator did.

Analyzed without them, a suite is charged for pairs the system can never produce, and its ratio can never reach 1.

## Where to go next

- [Close the gaps incrementally](close-the-gaps-incrementally.md) — take this report and append the rows it asks for, without touching the ones already there.
- [Tuples and coverage](../primer/tuples-and-coverage.md) — where the 21 comes from, worked from the beginning.
- [Constraint syntax](../constraints.md) — the expression language, and what a constraint removes from the required universe.
- [JavaScript API](../js-api.md) — the full shape of `CoverageReport` and the rest of the surface.
- [CLI reference](../cli.md) — the same analysis as `coverwise analyze`, for a suite that lives in JSON.
