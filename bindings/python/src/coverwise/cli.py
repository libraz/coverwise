"""Launch the native coverwise executable bundled in this platform wheel."""

from __future__ import annotations

import os
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import NoReturn


def native_binary() -> Path:
    """Return the native executable packaged with this wheel.

    The distribution is intentionally platform-specific: its CLI delegates to
    the same C++ binary shipped in GitHub Release archives, so JSON behavior and
    exit codes stay identical across installation methods.

    ``COVERWISE_BINARY`` overrides the bundled path. It exists so this package
    can be exercised from an unpacked source tree against a locally built CLI;
    an installed wheel never needs it.
    """

    override = os.environ.get("COVERWISE_BINARY")
    if override:
        binary = Path(override)
        if not binary.is_file():
            raise RuntimeError(f"COVERWISE_BINARY points at a missing file: {binary}")
        return binary

    binary = Path(__file__).parent / "_bin" / "coverwise"
    if not binary.is_file():
        raise RuntimeError(
            "coverwise native executable is missing; install a supported platform wheel "
            "from PyPI instead of an unpacked source tree"
        )
    return binary


def run(args: Sequence[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    """Run the bundled CLI and return its completed process result.

    This is a small programmatic convenience for automation. For normal command
    line use, invoke ``coverwise`` directly so it preserves native signals and
    standard I/O without a Python subprocess in the middle.
    """

    return subprocess.run([str(native_binary()), *args], **kwargs)  # type: ignore[arg-type]


def main() -> NoReturn:
    """Replace this launcher process with the bundled native executable."""

    binary = native_binary()
    os.execv(binary, [str(binary), *sys.argv[1:]])
    raise AssertionError("os.execv returned unexpectedly")
