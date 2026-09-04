# Replace a cross-product in pytest

A pytest module that parametrizes over every combination of its inputs runs a number of cases that multiplies as the module grows. `coverwise.parametrize` is a drop-in replacement for `pytest.mark.parametrize` in that situation: it generates a covering suite from the same lists and hands each case to the test as same-named arguments, so the test body does not change.

The vocabulary this page assumes — covering array, strength, pair — is taught in the [Primer](../primer/index.md). For the rest of the Python surface, see the [Python API](../python-api.md).

## Installing

`parametrize` is the one part of the package that needs pytest, and it needs it where the tests run rather than where the package is installed, so pytest is an extra rather than a dependency.

```bash
pip install coverwise[pytest]
```

Python 3.10 or newer. Calling `coverwise.parametrize` without pytest importable raises `RuntimeError` naming pytest as the missing piece; the rest of the package works without it.

## The cross-product now

```python
import itertools

import pytest

OS = ["Windows", "macOS", "Linux"]
BROWSER = ["Chrome", "Firefox", "Safari"]
LOCALE = ["en", "ja", "de"]


@pytest.mark.parametrize("os,browser,locale", list(itertools.product(OS, BROWSER, LOCALE)))
def test_page_renders(os, browser, locale):
    assert render(os, browser, locale).ok
```

3 × 3 × 3 = 27 cases. The count is the product of the list lengths, so a fourth input with four values makes it 108, and a value added to any existing list moves it by a factor rather than by one.

## The covering suite

```python
import coverwise


@coverwise.parametrize(
    {
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "locale": ["en", "ja", "de"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)
def test_page_renders(os, browser, locale):
    assert render(os, browser, locale).ok
```

13 cases. The three inputs give three input pairs, and 3 × 3 + 3 × 3 + 3 × 3 = 27 value pairs; the constraint makes `os=Windows, browser=Safari` impossible, leaving 26 pairs that have to appear somewhere in the suite. Every one of them does, in 13 cases rather than 27, and the constraint holds case by case as well, so no generated case pairs Windows with Safari. That the cross-product is also 27 here is a coincidence of three-by-three-by-three; the pair count grows quadratically in the number of inputs while the cross-product grows exponentially.

The decision this asks for is which faults the suite is meant to catch. 13 cases catch every fault that a pair of inputs triggers. They do not catch a fault that needs a specific value in all three at once; for that, raise the strength and pay for it in cases. The cross-product covers all three at once and costs 27 cases to do it.

Each case gets an id built from its values — `os=macOS-browser=Firefox-locale=ja` — so `pytest -k` selects and a failure report names the combination without a lookup.

## Passing the rest of the model

Any further model field is passed through to `coverwise.generate` unchanged: `strength`, `constraints`, `seed`, `maxTests`, `weights`, `subModels`. `strength` defaults to 2. `seed` fixes which covering suite is produced, so pinning it keeps the case ids stable across runs and across machines.

`include_negative` is keyword-only and belongs to the decorator rather than the model. Values marked invalid produce negative cases, each carrying exactly one invalid value, and they are left out unless asked for.

```python
import coverwise


@coverwise.parametrize(
    {
        "email": ["user@example.com", {"value": "", "invalid": True}],
        "plan": ["child", "adult"],
    },
    include_negative=True,
    seed=7,
)
def test_signup_is_answered(email, plan):
    assert signup(email, plan) is not None
```

Two positive cases without `include_negative`, four with it: the two negative cases pair the empty email with each plan. Turn it on only when the test body can tell a rejection from an acceptance by looking at the values it received. When the expected outcome differs between the two suites, two tests with opposite expectations over `generate`'s `tests` and `negativeTests` are the clearer shape.

## A single parameter

A one-parameter model has no pairs, so the default strength of 2 would exceed the parameter count and the model would be rejected. `parametrize` defaults strength to 1 in that case, and pytest is given one argument name rather than several, so the test receives the bare value instead of a one-element tuple.

```python
import coverwise


@coverwise.parametrize({"browser": ["Chrome", "Firefox", "Safari"]})
def test_page_renders_per_browser(browser):
    assert render(browser).ok
```

Three cases, ids `browser=Chrome`, `browser=Firefox` and `browser=Safari`, and `browser` is the string. At strength 1 the required universe is every value of every parameter, so the suite is as large as the longest value list.

Parameter names become argument names, so each must be a valid Python identifier and must not be a keyword. `parametrize` raises `ValueError` naming the offending names rather than letting pytest fail at collection time with something less specific.

## Where to go next

- [Python API](../python-api.md) — `generate`, `analyze`, `extend` and the error type, for what the decorator does not cover.
- [Strength](../primer/strength.md) — what t = 2 catches, what it misses, and what raising it costs.
- [Constraint syntax](../constraints.md) — the expression language the `constraints` list is written in.
- [Audit an existing suite](audit-an-existing-suite.md) — measuring a suite that is staying hand-written.
- [Examples](../examples.md) — shorter recipes, including positive and negative suites as two tests.
