# Python API

## Installation

The `coverwise` PyPI package installs the native command-line tool. It ships no
separate Python implementation of the generator, so its JSON behavior, output,
and exit codes exactly match the C++ CLI.

```bash
pip install coverwise
coverwise --help
```

Supported wheels are Linux x86_64 and macOS Apple Silicon. There are no runtime
Python dependencies. `python -m coverwise` invokes the same command.

## Command-line interface

Use the installed command for shell scripts and pipelines:

```bash
coverwise generate input.json > tests.json
coverwise analyze --params params.json --tests tests.json
coverwise extend --existing tests.json input.json > updated-tests.json
coverwise stats input.json
```

See the [CLI reference](cli.md) for input schemas, output schemas, and exit
codes.

## Automation helper

For a Python process that needs to run the executable, `coverwise.run()` returns
the standard `subprocess.CompletedProcess`. Pass the same arguments that follow
the `coverwise` command.

```python
import coverwise

result = coverwise.run(
    ["generate", "input.json"],
    text=True,
    capture_output=True,
    check=True,
)
print(result.stdout)
```

`coverwise.native_binary()` returns the bundled executable path when an
integration needs to own process creation itself.
