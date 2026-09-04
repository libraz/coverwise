# Examples

One recipe per feature, each with the numbers the engine actually returns. This page is not a tour of applications: an end-to-end flow that starts from a suite you already have lives under [Use cases](use-cases/index.md), and the vocabulary the recipes use — tuple, coverage unit, strength — is taught in [Primer](primer/index.md).

Every TypeScript recipe below is a fragment that reuses one shared setup. Put the following in the same module, then run a recipe after it. The Python recipes are complete on their own and repeat their imports.

```typescript
import { Coverwise } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## Basic pairwise generation

The most common case. Every pair of values drawn from two different parameters appears in at least one row.

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',       values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser',  values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'language', values: ['en', 'ja', 'de'] },
    { name: 'theme',    values: ['light', 'dark'] },
  ],
});

result.stats.totalTuples;  // 53 pairs: 12 + 9 + 6 + 12 + 8 + 6 over the six parameter pairs
result.tests.length;       // 15 rows, against a cross-product of 3 * 4 * 3 * 2 = 72
result.coverage;           // 1
```

## Excluding invalid combinations

Constraints keep impossible rows out of the suite. They also shrink what the suite has to cover, because a pair no valid row can hold is not a gap.

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'iOS', 'Android'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'device',  values: ['desktop', 'tablet', 'phone'] },
  ],
  constraints: [
    'IF os = iOS THEN browser = Safari',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});

result.stats.totalTuples;  // 35 required pairs, down from 40 without the constraints
result.tests.length;       // 17 rows
result.coverage;           // 1
```

Five pairs left the required set: `os=iOS` with each of Chrome, Firefox and Edge, and `device=phone` with each of Windows and macOS. See [Constraint syntax](constraints.md) for the expression language and [Constraints and the required universe](primer/constraints-and-the-universe.md) for why those five are excluded rather than reported uncovered.

## Negative testing

Mark a value `invalid` and coverwise builds a second suite from it. Each negative row carries exactly one invalid value, so a rejection can be attributed to that value alone.

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

result.tests.length;                   // 3 positive rows
result.stats.totalTuples;              // 7 positive pairs: 1 + 3 + 3 over the valid values only
result.negativeTests.length;           // 12 negative rows
result.negativeCoverage?.totalTuples;  // 16 negative tuples, each one invalid value beside one valid one
```

Invalid values never count toward positive coverage, which is why the positive universe here is 7 pairs and not the 27 the full value lists would give.

## Mixed strength for a critical group

`subModels` raises the strength of a named group without paying for that strength across the whole model.

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
  subModels: [
    { parameters: ['protocol', 'auth', 'cache'], strength: 3 },
  ],
});

result.stats.totalTuples;  // 138: the 120 pairs of the whole model plus 18 triples over the group
result.tests.length;       // 18 rows, where the same model at plain pairwise needs 14
```

The 18 triples are the full cross-product of the group (3 protocols, 3 auth modes, 2 cache states), so they alone put a floor of 18 rows under the suite; everything else is covered inside those rows.

## Equivalence classes

Reach for class-level coverage when several values are expected to take the same code path and the pairs you care about are between the classes, not between the values. Value-level coverage is the stricter goal, and it can be far more expensive than the behaviour warrants.

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

result.stats.totalTuples;                  // 20 value pairs, from 5 ages and 4 plans
result.tests.length;                       // 20 rows, because value-level pairwise is the cross-product here
result.classCoverage?.totalClassTuples;    // 6 class pairs, from 3 age classes and 2 plan classes
result.classCoverage?.classCoverageRatio;  // 1
```

Six class pairs against twenty value pairs is the size of the decision. `classCoverage` reports both, so a suite can be judged against the level that matches what the system distinguishes.

## Weight hints

Weights break ties. When several values would close the same number of gaps, a higher weight makes one likelier to be chosen; it does not change how many rows the suite needs.

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari', 'Edge'] },
    { name: 'theme',   values: ['light', 'dark'] },
  ],
  weights: {
    os: { Windows: 3.0, macOS: 1.0, Linux: 1.0 },
    browser: { Chrome: 2.0 },
  },
});

result.stats.totalTuples;  // 26 pairs: 12 + 6 + 8
result.tests.length;       // 12 rows, the same count the model produces with no weights at all
```

## Seed tests

`seeds` are rows that must appear in the output. The generator keeps them, in order, and fills the remaining gaps around them.

```typescript
const result = cw.generate({
  parameters: [
    { name: 'os',      values: ['Windows', 'macOS', 'Linux'] },
    { name: 'browser', values: ['Chrome', 'Firefox', 'Safari'] },
    { name: 'env',     values: ['staging', 'production'] },
  ],
  seeds: [
    { os: 'Windows', browser: 'Chrome', env: 'production' },
    { os: 'macOS',   browser: 'Safari', env: 'production' },
  ],
});

result.tests.slice(0, 2);  // the two seed rows, in the order they were given
result.stats.totalTuples;  // 21 pairs: 9 + 6 + 6
result.tests.length;       // 9 rows, where the same model without seeds produces 10
```

Seeds belong to a model you are about to generate. To keep an existing suite and append only what closes its gaps, use `extendTests` instead — [Close the gaps incrementally](use-cases/close-the-gaps-incrementally.md) runs that loop end to end.

## Boundary value expansion

A numeric range expands into the values worth testing at each edge: one immediately outside, the edge itself, and one immediately inside.

```typescript
const result = cw.generate({
  parameters: [
    { name: 'port',    values: [], type: 'integer', range: [1, 65535], step: 1 },
    { name: 'timeout', values: [], type: 'float',   range: [0.1, 30.0], step: 0.1 },
  ],
});

result.stats.totalTuples;  // 36 pairs: each range expands to 6 values, so 6 * 6
result.tests.length;       // 36 rows
```

`port` expands to `0`, `1`, `2`, `65534`, `65535`, `65536`, and `timeout` to `0`, `0.1`, `0.2`, `29.9`, `30`, `30.1`. A boundary parameter still declares `values`; anything listed there is merged into the expanded set rather than replaced. Integer ranges accept only `step: 1`. The same four fields appear under a parameter in a CLI model document — see [CLI reference](cli.md).

## Model estimation

`estimateModel` sizes a model without building a suite for it.

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

stats.parameterCount;  // 4
stats.totalTuples;     // 500: 4 parameter triples, each with 5 * 5 * 5 = 125 value combinations
stats.estimatedTests;  // 250
```

Generating that model produces 165 rows. `estimatedTests` is a sizing figure and bounds nothing in either direction — it over-estimates here and under-estimates on other models. Read `stats.totalTuples` to judge cost, and the coverage of a generated suite to judge the result.

## Python

Every model field above is the same in Python, passed to `coverwise.generate` as keyword arguments or as one mapping. The two recipes here are shaped by the test suite rather than by the model. See the [Python API](python-api.md) for the full reference, and [Replace a cross-product in pytest](use-cases/replace-a-cross-product-in-pytest.md) for the migration of a parametrized suite.

### Positive and negative suites as separate tests

Negative rows carry one invalid value each, so the two suites want opposite expectations and belong in two tests.

```python
import coverwise
import pytest

LOGIN_MODEL = {
    "parameters": [
        {"name": "email", "values": [
            "user@example.com",
            {"value": "", "invalid": True},
            {"value": "not-an-email", "invalid": True},
        ]},
        {"name": "password", "values": [
            "Str0ng!Pass",
            {"value": "", "invalid": True},
            {"value": "short", "invalid": True},
        ]},
        {"name": "role", "values": ["admin", "user", "guest"]},
    ]
}
_suite = coverwise.generate(LOGIN_MODEL)
# _suite["tests"] holds 3 rows; _suite["negativeTests"] holds 12.


@pytest.mark.parametrize("case", _suite["tests"])
def test_login_accepts_valid_input(case):
    assert login(**case).ok


@pytest.mark.parametrize("case", _suite["negativeTests"])
def test_login_rejects_invalid_input(case):
    with pytest.raises(ValidationError):
        login(**case)
```

Passing a whole row as one `case` argument keeps both tests working when the model grows a parameter. `coverwise.parametrize(..., include_negative=True)` runs the two suites through a single test instead, which suits a test that derives the expected outcome from the values it receives.

### Sub-models through the decorator

Any model field passes through the decorator, `subModels` included.

```python
import coverwise


@coverwise.parametrize(
    {
        "protocol": ["HTTP/1.1", "HTTP/2", "HTTP/3"],
        "auth": ["none", "basic", "oauth"],
        "cache": ["enabled", "disabled"],
        "region": ["us", "eu", "ap"],
    },
    subModels=[{"parameters": ["protocol", "auth", "cache"], "strength": 3}],
)
def test_request_path(protocol, auth, cache, region):
    assert request(protocol, auth, cache, region).ok
```

The universe is 63 tuples: 45 pairs across the four parameters plus the 18 triples of the networking group. The run is 18 cases, against a cross-product of 54 — the group's 18 combinations set the floor, and `region` is covered inside them.

## Coverage as a CI gate

The CLI reports coverage through its exit code, so a gap can fail a job with no extra scripting.

```bash
# 0 when the hand-written suite covers every pair, 2 when it does not.
coverwise analyze --params params.json --tests tests.json

# 0 at full coverage, 2 when a maxTests ceiling stopped generation short.
coverwise generate input.json > tests.json
```

Exit code 3 means the input was rejected, which for `analyze` includes a test row the parameter model does not describe. [CLI reference](cli.md) lists every code, and [Audit an existing suite](use-cases/audit-an-existing-suite.md) reads the report the gate produces and decides what to do about it.

```yaml
- uses: actions/setup-python@v7
  with:
    python-version: '3.12'
- name: Install coverwise
  run: pip install coverwise
- name: Check pairwise coverage
  run: coverwise analyze --params params.json --tests tests.json
```

## Where to go next

- [Use cases](use-cases/index.md) — end-to-end flows that start from a suite or a cross-product you already have.
- [Constraint syntax](constraints.md) — the full expression language behind the `constraints` field.
- [Determinism](determinism.md) — what `seed` guarantees, and across which surfaces.
- [JavaScript API](js-api.md) — every field and result type these recipes touch.
- [Python API](python-api.md) — the same models from Python, plus `parametrize`.
- [Questions and limitations](faq.md) — why a suite is larger than the theoretical minimum, and what `maxTests` does when it binds.
