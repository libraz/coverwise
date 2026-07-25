# coverwise

[![PyPI](https://img.shields.io/pypi/v/coverwise)](https://pypi.org/project/coverwise/)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)

**Combinatorial test coverage for Python.** The `coverwise` package ships the
native C++ CLI plus a Python API that generates, analyzes, and incrementally
extends t-wise test suites — including a `pytest` decorator that replaces a
hand-written cross-product with a covering suite.

## Installation

```bash
pip install coverwise
```

Supported wheels: Linux x86_64, Linux aarch64, and macOS 14+ Apple Silicon. The
package ships a prebuilt executable rather than portable Python code, so other
platforms have to build the CLI from source. There are no Python runtime
dependencies, and the `coverwise` command is installed directly.

## Quick Start

```python
import coverwise

result = coverwise.generate(
    parameters={
        "os": ["Linux", "macOS", "Windows"],
        "browser": ["Chrome", "Firefox", "Safari"],
    },
    constraints=["IF os = Windows THEN browser != Safari"],
    strength=2,
)

result["coverage"]  # 1
result["tests"]     # [{"os": "Linux", "browser": "Firefox"}, ...]
```

Every function takes and returns plain dictionaries — the same JSON the CLI
reads and writes:

| Function | Purpose |
|----------|---------|
| `generate(model, **fields)` | Generate a covering suite |
| `analyze_coverage(parameters, tests, strength, constraints)` | Measure an existing suite |
| `extend_tests(existing, model, **fields)` | Extend a suite, keeping existing tests |
| `estimate_model(model, **fields)` | Model size and validity, without generating |

Invalid models and unparsable constraints raise `coverwise.CoverwiseError`,
carrying `code`, `exit_code`, and the CLI's `stderr`.

## pytest

`coverwise.parametrize` runs a test once per generated case, passing each
parameter as a same-named argument:

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

The full cross-product is 18 cases; the pairwise suite covers every pair in far
fewer. pytest is not a dependency of this package — the decorator only needs it
where tests run, and `pip install coverwise[pytest]` installs both together.

## Command Line

```bash
coverwise generate input.json > tests.json
coverwise analyze --params input.json --tests tests.json
coverwise extend --existing tests.json input.json > updated.json
coverwise stats input.json
```

Any input path may be `-` to read that JSON from standard input.
`python -m coverwise` is equivalent to the installed command, and
`coverwise --help` lists every command with its exit-code contract.

See the [Python API reference](https://github.com/libraz/coverwise/blob/main/docs/en/python-api.md),
the [CLI reference](https://github.com/libraz/coverwise/blob/main/docs/en/cli.md),
and the [full project documentation](https://github.com/libraz/coverwise/tree/main/docs/en).

## License

Apache-2.0
