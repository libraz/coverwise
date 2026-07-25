# Examples

Practical recipes for common testing scenarios.

The recipes below are TypeScript fragments that use the class-based API; the
[Python](#python) section covers the same models from a pytest suite. Put the
following setup in the same module, then run a recipe after it:

```typescript
import { Coverwise, when, allOf } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## Basic Pairwise Generation

The most common case — generate a compact test set that targets all parameter pairs:

```typescript
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
console.log('Negative tests:', result.negativeTests.length);

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

Influence value selection when multiple candidates offer equal coverage. Weights are
preferences, not a frequency guarantee:

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
// Higher-weight values are preferred when coverage is otherwise equivalent.
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

Auto-expand numeric ranges into edge and near-edge values:

```json
{
  "parameters": [
    {
      "name": "port",
      "values": [],
      "type": "integer",
      "range": [1, 65535],
      "step": 1
    },
    {
      "name": "timeout",
      "values": [],
      "type": "float",
      "range": [0.1, 30.0],
      "step": 0.1
    }
  ]
}
```

This expands `port` to `0`, `1`, `2`, `65534`, `65535`, `65536`: one value just
outside each edge, each edge itself, and one value just inside each edge. Existing
numeric values are merged into the same set.

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

## Python

Every recipe above works from Python unchanged: the model fields are the same,
passed to `coverwise.generate` as keyword arguments or a mapping. The recipes
below are the ones shaped by a Python test suite rather than by the model.
See the [Python API](python-api.md) for the full reference.

### Parametrizing a pytest test

`coverwise.parametrize` replaces a hand-written cross-product. Each parameter
arrives as a same-named argument:

```python
import coverwise

@coverwise.parametrize(
    {
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "language": ["en", "ja", "de"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)
def test_page_renders(os, browser, language):
    assert render(os, browser, language).ok
```

The constraint leaves 24 valid combinations; the pairwise suite covers every pair
in 13 cases, each with a readable id (`os=macOS-browser=Chrome-language=ja`).

### Positive and negative suites as separate tests

Invalid values produce negative tests with exactly one invalid value each, so the
two suites belong in two tests with opposite expectations:

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
            {"value": "short", "invalid": True},
        ]},
    ]
}
_suite = coverwise.generate(LOGIN_MODEL)


@pytest.mark.parametrize("case", _suite["tests"])
def test_login_accepts_valid_input(case):
    assert login(**case).ok


@pytest.mark.parametrize("case", _suite["negativeTests"])
def test_login_rejects_invalid_input(case):
    with pytest.raises(ValidationError):
        login(**case)
```

Passing whole test cases as one `case` argument keeps this working when the model
grows a parameter. `coverwise.parametrize(..., include_negative=True)` runs both
suites through a single test instead, which suits a test that derives the expected
outcome from the values it receives.

### Guarding a hand-written suite's coverage

Coverage of an existing suite is a normal assertion, so a gap fails the test run
with the missing combinations named:

```python
PARAMETERS = {"os": ["Windows", "macOS"], "browser": ["Chrome", "Firefox"]}


def test_manual_suite_covers_every_pair():
    report = coverwise.analyze_coverage(PARAMETERS, MANUAL_TESTS)

    assert report["uncovered"] == [], [item["display"] for item in report["uncovered"]]
```

### Higher strength for a critical group

Any model field passes through the decorator, including `subModels`:

```python
@coverwise.parametrize(
    {
        "protocol": ["HTTP/1.1", "HTTP/2", "HTTP/3"],
        "auth": ["none", "basic", "oauth"],
        "cache": ["enabled", "disabled"],
        "region": ["us", "eu", "ap"],
    },
    strength=2,
    subModels=[{"parameters": ["protocol", "auth", "cache"], "strength": 3}],
)
def test_request_path(protocol, auth, cache, region):
    assert request(protocol, auth, cache, region).ok
```

The networking trio gets exhaustive 3-wise coverage while `region` stays
pairwise: 18 cases instead of the 54-case cross-product.

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
