# coverwise

[![PyPI](https://img.shields.io/pypi/v/coverwise)](https://pypi.org/project/coverwise/)
[![npm](https://img.shields.io/npm/v/@libraz/coverwise)](https://www.npmjs.com/package/@libraz/coverwise)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/coverwise/blob/main/LICENSE)

**Combinatorial test coverage from the command line.** The `coverwise` Python
package installs the native C++ CLI, which generates, analyzes, and incrementally
extends t-wise test suites using JSON input and output.

## Installation

```bash
pip install coverwise
```

Supported wheels: Linux x86_64 and macOS Apple Silicon. The package has no Python
runtime dependencies and installs the `coverwise` command directly.

## Quick Start

Save a model as `input.json`:

```json
{
  "parameters": [
    { "name": "os", "values": ["Linux", "macOS", "Windows"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "strength": 2
}
```

Generate a complete pairwise suite:

```bash
coverwise generate input.json > tests.json
coverwise analyze --params input.json --tests tests.json
```

Run `coverwise --help` for all commands. `generate`, `analyze`, `extend`, and
`stats` use the same JSON and exit-code contract as the standalone C++ binary.
`python -m coverwise` is equivalent to the installed command.

## Python Automation

For a small subprocess-based integration point, the package exposes the bundled
binary path and a launcher. The CLI remains the public interface; this helper
does not implement a second Python model API.

```python
import coverwise

result = coverwise.run(["stats", "input.json"], text=True, capture_output=True, check=True)
print(result.stdout)
```

See the [CLI reference](https://github.com/libraz/coverwise/blob/main/docs/en/cli.md)
and [full project documentation](https://github.com/libraz/coverwise/tree/main/docs/en).

## License

Apache-2.0
