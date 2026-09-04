# Python API

Reference for the `coverwise` PyPI package — four functions over plain dictionaries, a pytest decorator, and the native executable they drive. It is for a reader driving coverwise from Python, and it assumes the vocabulary of [Tuples and coverage](primer/tuples-and-coverage.md) and [Strength](primer/strength.md) rather than re-explaining it. [Getting started](getting-started.md) installs the package and generates a first suite.

The package ships the native command-line tool plus a thin Python layer that drives it. There is no separate Python implementation of the generator, so the JSON shapes, the generated suites and the error categories are the CLI's.

The JavaScript API returns the same fields with the same names and the same values, minus one. `schemaVersion`, the version envelope the CLI wraps every document in, is a CLI field and is absent from the JavaScript types, so a stringified JavaScript result handed to a CLI command that expects a suite is rejected as invalid input.

## Installation

```bash
pip install coverwise
```

The package requires Python 3.10 or newer, and it declares that floor as `requires-python`, so an older interpreter is refused by pip rather than at import time. There are no runtime Python dependencies.

Wheels are platform-specific, because the package carries a prebuilt executable rather than portable Python code:

| Platform | Wheel | Requires |
|---|---|---|
| Linux x86_64 | `manylinux_2_28_x86_64` | glibc 2.28 or newer |
| Linux aarch64 | `manylinux_2_28_aarch64` | glibc 2.28 or newer |
| macOS Apple Silicon | `macosx_14_0_arm64` | macOS 14 or newer |

The glibc floor rules out distributions older than that line — CentOS 7, Ubuntu 18.04, Debian 9 — even on x86_64. On any platform without a wheel, build the CLI from source and point `COVERWISE_BINARY` at it, or use the [C++ API](cpp-api.md) directly.

Two extras exist. `pip install coverwise[pytest]` adds pytest, which `parametrize` needs in the environment where tests run. `pip install coverwise[dev]` adds the toolchain this package is developed with — build, mypy, pytest, ruff and wheel — and is not needed to use it.

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
    print(test)  # {"os": "macOS", "browser": "Firefox"}, ...
```

Every function takes and returns plain dictionaries and lists — the same JSON documented in the [CLI reference](cli.md). Nothing has to be serialized or parsed by hand. Numbers decode the way `json` decodes them, so a whole-numbered ratio such as full coverage arrives as `1` rather than `1.0`; comparing against `1.0` still works.

## Model input

`parameters` accepts either the JSON list form or a name-to-values mapping:

```python
import coverwise

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

A mapping entry always holds a list, even for one value: `{"env": "prod"}` is rejected with a `TypeError` rather than read as the four values `p`, `r`, `o`, `d`. Write `{"env": ["prod"]}`.

A `set` or `frozenset` is refused wherever a value container belongs, with a `TypeError` naming the fix. Iterating a set depends on `PYTHONHASHSEED` and on insertion history, so the same call would describe a different model on every run and break the determinism the rest of the API guarantees:

```python
import coverwise

coverwise.generate(parameters={"os": {"win", "mac"}})            # TypeError
coverwise.generate(parameters={"os": sorted({"win", "mac"})})    # fine
coverwise.generate(parameters={"os": list(some_set)})            # fine
```

Containers that iterate in insertion order are reproducible and are accepted, so `dict.keys()` and `dict.items()` can be passed as-is — what disqualifies a container is its order, not the protocol it satisfies. The rule applies to both `parameters` forms and to every other place a container reaches a model, including `constraints`, `seeds` and test rows, and it is the same rule in the same words for `generate`, `extend_tests`, `estimate_model`, `analyze_coverage` and `parametrize`.

Model fields can be passed as keyword arguments, as a single mapping, or both — keyword arguments override the mapping, which makes a stored model easy to reuse:

```python
import coverwise

MODEL = {"parameters": {"os": ["win", "mac"], "browser": ["Chrome", "Firefox"]}}

pairwise = coverwise.generate(MODEL)
three_wise = coverwise.generate(MODEL, strength=3)
```

`MODEL` above is the model the function examples below reuse. Each of those blocks repeats its import and stands on its own otherwise; bind `MODEL` once in the same module and run any of them after it.

## Functions

### `generate(model=None, **fields)`

Generate a covering test suite. Returns the `generate` result: `tests`, `coverage`, `uncovered`, `stats`, and the other fields the CLI documents.

A suite that cannot reach full coverage — for example under `maxTests` — is returned with `coverage` below 1.0 rather than raising, so a partial result stays inspectable:

```python
import coverwise

result = coverwise.generate(MODEL, maxTests=3)
if result["coverage"] < 1.0:
    for missing in result["uncovered"]:
        print(missing["display"])  # "os=mac, browser=Firefox"
```

### `analyze_coverage(parameters, tests, strength=2, constraints=None)`

Measure the t-wise coverage of a suite you already have — hand-written tests, a suite from another tool, or a previous `generate` result. `tests` accepts a bare list of test cases or a whole `generate` result.

```python
import coverwise

report = coverwise.analyze_coverage(
    {"os": ["win", "mac"], "browser": ["Chrome", "Safari"]},
    [{"os": "win", "browser": "Chrome"}],
)

report["coverageRatio"]  # 0.25
[item["display"] for item in report["uncovered"]]  # ["os=win, browser=Safari", ...]
```

Two things the CLI's `analyze` can do are not reachable through this function. `parameters` is a name-to-values mapping or the parameter list form, never a whole model object: passing `{"parameters": [...], "strength": 3}` reads `strength` as a parameter name and raises a `TypeError`. And `strength` is always sent to the CLI explicitly, so the inheritance the CLI describes — an `analyze` run taking its strength from a `--params` model object — never happens here. Pass the strength as the argument.

### `extend_tests(existing, model=None, **fields)`

Extend a suite until the model is covered. Existing tests are kept as-is and come first in the returned `tests`, so recorded runs stay valid.

```python
import coverwise

previous = coverwise.generate(MODEL, maxTests=2)
result = coverwise.extend_tests(previous["tests"], MODEL)
added = result["tests"][len(previous["tests"]) :]
```

### `estimate_model(model=None, **fields)`

Report model statistics without generating anything: parameter and value counts, the raw tuple count, and an estimated suite size. Constraint syntax and parameter references are validated, which makes it a cheap pre-flight check.

`estimatedTests` is a coarse sizing heuristic derived from the largest value count, the strength and the parameter count, capped at `totalTuples`. It is not a bound in either direction — a generated suite may be smaller or larger.

```python
import coverwise

stats = coverwise.estimate_model(MODEL, strength=3)
stats["totalTuples"], stats["estimatedTests"]
```

### `parametrize(parameters=None, *, include_negative=False, **fields)`

Replace a hand-written cross-product with a generated t-wise suite. Returns a `pytest.mark.parametrize` marker, and each parameter becomes a same-named test argument:

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

The full cross-product above is 3 × 3 × 2 = 18 cases; the constrained pairwise suite covers every feasible pair in 9, and each case gets a readable id (`os=macOS-browser=Chrome-locale=en`).

`parameters` takes the same two forms `generate` takes. It defaults to `None` only so the failure is a clear one: omitting it raises `TypeError: a model requires 'parameters'`. `include_negative` is keyword-only — the `*` in the signature — so it can never be mistaken for a second positional model. Passing `include_negative=True` also runs the generated negative tests, which exercise the values marked `"invalid": true`; they are excluded by default.

Every further keyword is a model field passed straight through to `generate`. That includes `constraints` and `seeds` as well as `strength`, `seed`, `maxTests`, `weights` and `subModels`: `parametrize` builds the same model document `generate` accepts, and adds nothing to it.

Parameter names become test arguments, so they must be valid Python identifiers and must not be Python keywords; a name that cannot be one raises a `ValueError` before pytest collects anything.

A single-parameter model is handled specially, because pytest and the engine both need it to be:

```python
import coverwise

@coverwise.parametrize({"os": ["Windows", "macOS", "Linux"]})
def test_boot(os):
    assert boot(os).ok
```

`strength` defaults to 1 rather than 2 for one parameter, since the default would exceed the parameter count and the model would be rejected. And the test receives the bare value — `os` is the string `"Windows"`, not a one-element tuple — because pytest unpacks a tuple per case only when several argument names are given. An explicit `strength` in `**fields` still wins.

pytest is not a dependency of this package; the decorator only needs it in the environment where tests run. Calling `parametrize` without pytest installed raises `RuntimeError: coverwise.parametrize requires pytest; install it to use this decorator`. `pip install coverwise[pytest]` installs both when that environment is the same one.

## Errors

Invalid models and unparsable constraints raise `coverwise.CoverwiseError`:

```python
import coverwise

try:
    coverwise.generate(MODEL, constraints=["IF os = = THEN"])
except coverwise.CoverwiseError as error:
    error.code       # "CONSTRAINT_ERROR"
    error.exit_code  # 1, matching the CLI exit-code contract
    error.stderr     # the CLI's full diagnostic output, "error: " prefix included
    error.report     # the JSON report the CLI wrote before failing, or None
```

`code` is `CONSTRAINT_ERROR` or `INVALID_INPUT`, and `exit_code` is always one of the codes the [CLI reference](cli.md) documents. `stderr` is the diagnostic exactly as the CLI wrote it; the exception's own message is that same text with the `error: ` prefix removed. The JavaScript API reports one further category, `TUPLE_EXPLOSION`; the CLI folds it into `INVALID_INPUT`, and this package reports what the CLI reports. Insufficient coverage is not an error — see `generate` above.

One class of failure is caught before any subprocess starts. A non-finite number in the model — `inf`, `-inf` or `nan` — cannot be serialized as JSON, so it is rejected during serialization and raised as `CoverwiseError` with `code == "INVALID_INPUT"` and `exit_code == 3`. No executable ran, so `stderr` is the empty string and `report` is `None`. Read the exception's message on that path; `stderr` has nothing in it.

Some failures are diagnosed by a report rather than by a message. `analyze` measures a suite and only then rejects it for containing invalid rows, so the report naming those rows survives on the exception:

```python
import coverwise

PARAMS = {"os": ["Windows", "macOS"], "browser": ["Chrome", "Safari"]}
CONSTRAINTS = ["IF os = Windows THEN browser != Safari"]
tests = [{"os": "macOS", "browser": "Chrome"}, {"os": "Windows", "browser": "Safari"}]

try:
    coverwise.analyze_coverage(PARAMS, tests, constraints=CONSTRAINTS)
except coverwise.CoverwiseError as error:
    for invalid in error.report["invalidTests"]:
        print(invalid["testIndex"], invalid["reason"])  # 1 violates constraint #1 ...
```

`report` is `None` when the CLI failed before writing anything, which is the case for a model the acceptance gate rejects before generation starts. A constraint failure is not one of those: `generate` and `extend` write the whole envelope, with the `error` object inside it, before the failure surfaces.

`analyze_coverage` and `extend_tests` take two inputs, and standard input can carry only one of them, so the other is written to a temporary file that is deleted before the call returns. A diagnostic about that input would otherwise name a path the caller never wrote and can no longer look at, so the path is rewritten to name the argument instead: a failure reading the side input of `analyze_coverage` is reported against `the 'tests' argument`, and one of `extend_tests` against `the 'existing' argument`.

A crash of the native executable is not a model error and does not raise `CoverwiseError`: it raises a plain `RuntimeError` naming the signal or exit status, so a segfault is never presented as an invalid input.

## Input limits

The API serializes a model to JSON and hands it to the native executable, so the limits are the CLI's limits, and exceeding one raises `CoverwiseError` with `code == "INVALID_INPUT"` and `exit_code == 3`. The limits themselves, which one binds first, and the message each produces are documented in [Input limits](limits.md).

## Running the executable directly

`coverwise.run()` returns the standard `subprocess.CompletedProcess` for arguments passed straight to the CLI, and `coverwise.native_binary()` returns the bundled executable's path when an integration needs to own process creation:

```python
import coverwise

result = coverwise.run(["generate", "input.json"], text=True, capture_output=True, check=True)
print(result.stdout)
print(coverwise.native_binary())
```

`coverwise.run()` passes keyword arguments straight to `subprocess.run`, so it is `subprocess.run` that decides whether the captured output is text or bytes: `text=True` is the form whose `stdout` is a `str`.

Both raise `RuntimeError` rather than an `OSError` when the executable cannot be used, and the message names which of four things is wrong: the file is missing, the path is not a file, the file has no execute permission, or the operating system refused to start it. The first three are checked before the launch, so they are reported the same way however the executable was reached. When the path came from the wheel, the message ends with a note to install a supported platform wheel instead of running from an unpacked source tree.

`COVERWISE_BINARY` overrides the bundled path for both functions, and its failures are reported against that variable — `COVERWISE_BINARY points at a missing file: ...`. It exists for developing against a locally built CLI; an installed wheel never needs it.

## The console script

Installing the package puts a `coverwise` command on the path, and `python -m coverwise` runs the same thing:

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

The console script does not start a subprocess: it replaces its own process with the native executable through `os.execv`. Signals, exit codes and the three standard streams are therefore the executable's own, with no Python process in the middle to reinterpret them — which is what makes `coverwise generate big.json | head -c 200` behave the same way from a wheel as from a source build. Use `coverwise.run()` when a program needs the output back; use the command when a shell or a CI job is driving it.

See the [CLI reference](cli.md) for input schemas, output schemas, and exit codes.

## Version and type checking

```python
import coverwise

coverwise.__version__  # the version of the package and of the bundled executable
```

`__version__` is a module attribute rather than a member of `__all__`, and it names the one version both the Python layer and the executable it carries were built at.

The package is annotated inline and ships a PEP 561 marker, so mypy and pyright check calls against the real signatures with no stub package to install.

## Where to go next

- [Getting started](getting-started.md) — install and generate a first suite on every surface.
- [Replace a cross-product in pytest](use-cases/replace-a-cross-product-in-pytest.md) — `parametrize` applied to a suite that already exists.
- [CLI reference](cli.md) — the executable this package drives, and the JSON it writes.
- [Constraint syntax](constraints.md) — the expression language the `constraints` list accepts.
- [Input limits](limits.md) — what a model may contain, and what it reports at each ceiling.
- [Glossary](glossary.md) — the vocabulary this page uses.
