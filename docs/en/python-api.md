# Python API

## Installation

The `coverwise` PyPI package ships the native command-line tool plus a thin
Python API that drives it. There is no separate Python implementation of the
generator, so results and JSON shapes match the C++ CLI and the JavaScript API
exactly. Error categories follow the CLI's exit-code contract, which is slightly
coarser than the JavaScript one — see [Errors](#errors).

```bash
pip install coverwise
coverwise --help
```

Supported wheels are Linux x86_64, Linux aarch64, and macOS 14+ Apple Silicon.
Other platforms have to build the CLI from source, because the package ships a
prebuilt executable rather than portable Python code. There are no runtime
Python dependencies. `python -m coverwise` invokes the same command.

## Quick start

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
    strength=2,
)

print(result["coverage"])  # 1
for test in result["tests"]:
    print(test)  # {"os": "Linux", "browser": "Firefox"}, ...
```

Every function takes and returns plain dictionaries and lists — the same JSON
documented in the [CLI reference](cli.md). Nothing has to be serialized or
parsed by hand. Numbers decode the way `json` decodes them, so a whole-numbered
ratio such as full coverage arrives as `1` rather than `1.0`; comparing against
`1.0` still works.

## Model input

`parameters` accepts either the JSON list form or a name-to-values mapping:

```python
# Mapping form: concise, and preserves declaration order.
coverwise.generate(parameters={"os": ["win", "mac"]})

# List form: required for per-value options such as invalid values or aliases.
coverwise.generate(
    parameters=[
        {"name": "os", "values": ["win", "mac"]},
        {"name": "browser", "values": ["Chrome", {"value": "IE", "invalid": True}]},
    ]
)
```

A mapping entry always holds a list, even for one value: `{"env": "prod"}` is
rejected with a `TypeError` rather than read as the four values `p`, `r`, `o`,
`d`. Write `{"env": ["prod"]}`.

Model fields can be passed as keyword arguments, as a single mapping, or both —
keyword arguments override the mapping, which makes a stored model easy to reuse:

```python
MODEL = {"parameters": {"os": ["win", "mac"], "browser": ["Chrome", "Firefox"]}}

pairwise = coverwise.generate(MODEL)
three_wise = coverwise.generate(MODEL, strength=3)
```

## Functions

### `generate(model=None, **fields)`

Generate a covering test suite. Returns the `generate` result: `tests`,
`coverage`, `uncovered`, `stats`, and the other fields the CLI documents.

A suite that cannot reach full coverage — for example under `maxTests` — is
returned with `coverage` below 1.0 rather than raising, so a partial result stays
inspectable:

```python
result = coverwise.generate(MODEL, maxTests=3)
if result["coverage"] < 1.0:
    for missing in result["uncovered"]:
        print(missing["display"])  # "os=mac, browser=Firefox"
```

### `analyze_coverage(parameters, tests, strength=2, constraints=None)`

Measure the t-wise coverage of a suite you already have — hand-written tests, a
suite from another tool, or a previous `generate` result. Accepts a bare list of
test cases or a whole `generate` result.

```python
report = coverwise.analyze_coverage(
    {"os": ["win", "mac"], "browser": ["Chrome", "Safari"]},
    [{"os": "win", "browser": "Chrome"}],
)

report["coverageRatio"]  # 0.25
[item["display"] for item in report["uncovered"]]  # ["os=win, browser=Safari", ...]
```

### `extend_tests(existing, model=None, **fields)`

Extend a suite until the model is covered. Existing tests are kept as-is and
come first in the returned `tests`, so recorded runs stay valid.

```python
result = coverwise.extend_tests(previous["tests"], MODEL)
added = result["tests"][len(previous["tests"]) :]
```

### `estimate_model(model=None, **fields)`

Report model statistics without generating anything: parameter and value counts,
the raw tuple count, and an estimated suite size. Constraint syntax and
parameter references are validated, which makes it a cheap pre-flight check.

`estimatedTests` is a coarse sizing heuristic derived from the largest value
count, the strength and the parameter count, capped at `totalTuples`. It is not
a bound in either direction — a generated suite may be smaller or larger.

```python
stats = coverwise.estimate_model(MODEL, strength=3)
stats["totalTuples"], stats["estimatedTests"]
```

## pytest integration

`coverwise.parametrize` replaces a hand-written cross-product with a generated
t-wise suite. Each parameter becomes a same-named test argument:

```python
import coverwise

@coverwise.parametrize(
    {
        "os": ["Windows", "macOS", "Linux"],
        "browser": ["Chrome", "Firefox", "Safari"],
        "locale": ["en", "ja"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
)
def test_login(os, browser, locale):
    assert login(os, browser, locale).ok
```

The full cross-product above is 18 cases; the pairwise suite covers every pair
in far fewer, and each case gets a readable id (`os=macOS-browser=Chrome-locale=ja`).

Parameter names become test arguments, so they must be valid Python identifiers
and must not be Python keywords; a name that cannot be one raises a `ValueError`
before pytest collects anything. Any further model field (`strength`, `seed`, `maxTests`, `weights`, `subModels`)
is passed through. Values marked `"invalid": true` are excluded unless
`include_negative=True` is given, which also runs the generated negative tests.

pytest is not a dependency of this package; the decorator only needs it in the
environment where tests run. `pip install coverwise[pytest]` installs both when
that environment is the same one.

## Input limits

The API serializes a model to JSON and hands it to the native executable, so the
limits are the CLI's limits. Exceeding one raises `CoverwiseError` with
`code == "INVALID_INPUT"` and `exit_code == 3`:

| Limit | Value |
|-------|-------|
| Parameters per model | 1,024 |
| Values per parameter | 16,384 |
| Rows in `tests`, `seeds`, or `existing` | 100,000 |
| Constraint expressions | 256 |
| UTF-8 bytes in one string | 65,536 (64 KiB) |
| UTF-8 bytes in a model's strings, combined | 1,048,576 (1 MiB) |
| Bytes of one serialized JSON document | 67,108,864 (64 MiB) |

The combined-bytes budget covers the strings that describe the model —
parameter names, values, aliases, class names, constraint expressions and
sub-model parameter names. The parameter count is what keeps constraint
feasibility search bounded: the search walks one parameter per level, so nothing
else limits how deep it can go.

The last row bounds each document that reaches the executable — the one written
to its standard input, and the temporary file a two-input call writes for the
other. It is a memory guard rather than part of what the API accepts: it is sized
well above what a model meeting the limits above needs, so a real model reaches
one of those limits first and is rejected by the limit it actually exceeded.

## Errors

Invalid models and unparsable constraints raise `coverwise.CoverwiseError`:

```python
try:
    coverwise.generate(MODEL, constraints=["IF os = = THEN"])
except coverwise.CoverwiseError as error:
    error.code       # "CONSTRAINT_ERROR"
    error.exit_code  # 1, matching the CLI exit-code contract
    error.stderr     # the CLI's full diagnostic output
    error.report     # the JSON report the CLI wrote before failing, or None
```

`code` is `CONSTRAINT_ERROR` or `INVALID_INPUT`, and `exit_code` is always one of
the codes the [CLI reference](cli.md) documents. The JavaScript API reports one
further category, `TUPLE_EXPLOSION`; the CLI folds it into `INVALID_INPUT`, and
this package reports what the CLI reports. Insufficient coverage is not an error
— see `generate` above.

Some failures are diagnosed by a report rather than by a message. `analyze`
measures a suite and only then rejects it for containing invalid rows, so the
report naming those rows survives on the exception:

```python
try:
    coverwise.analyze_coverage(PARAMS, tests, constraints=CONSTRAINTS)
except coverwise.CoverwiseError as error:
    for invalid in error.report["invalidTests"]:
        print(invalid["testIndex"], invalid["reason"])  # 1 violates constraint #1 ...
```

`report` is `None` when the CLI failed before writing anything, which is the case
for an unparsable model or constraint.

A crash of the native executable is not a model error and does not raise
`CoverwiseError`: it raises a plain `RuntimeError` naming the signal or exit
status, so a segfault is never presented as an invalid input.

## Command-line interface

The installed command is the same executable the API drives:

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

See the [CLI reference](cli.md) for input schemas, output schemas, and exit
codes.

## Running the executable directly

`coverwise.run()` returns the standard `subprocess.CompletedProcess` for
arguments passed straight to the CLI, and `coverwise.native_binary()` returns the
bundled executable's path when an integration needs to own process creation:

```python
result = coverwise.run(["generate", "input.json"], text=True, capture_output=True, check=True)
print(result.stdout)
```

`coverwise.run()` passes keyword arguments straight to `subprocess.run`, so it is
`subprocess.run` that decides whether the captured output is text or bytes:
`text=True` is the form whose `stdout` is a `str`.

Setting `COVERWISE_BINARY` points both at a different executable. It exists for
developing against a locally built CLI; an installed wheel never needs it.

## Type checking

The package is annotated inline and ships a PEP 561 marker, so mypy and pyright
check calls against the real signatures with no stub package to install.
