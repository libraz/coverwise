"""Launch the native coverwise executable bundled in this platform wheel."""

from __future__ import annotations

import os
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any, Literal, NoReturn, overload

_PLATFORM_WHEEL_HINT = (
    "; install a supported platform wheel from PyPI instead of an unpacked source tree"
)


def _unusable_reason(binary: Path) -> str | None:
    """Name what stops ``binary`` from being launched, or ``None`` if nothing does.

    Every way of reaching the executable goes through here, so a path that is
    absent, a directory, or not marked executable is reported the same way and
    with the same exception type. Leaving any of them to the launch itself would
    surface a bare OS error naming neither the reason nor the way to fix it.
    """

    if not binary.exists():
        return "a missing file"
    if not binary.is_file():
        return "a path that is not a file"
    if not os.access(binary, os.X_OK):
        return "a file without execute permission"
    return None


def native_binary() -> Path:
    """Return the native executable packaged with this wheel.

    The distribution is intentionally platform-specific: its CLI delegates to
    the same C++ binary shipped in GitHub Release archives, so JSON behavior and
    exit codes stay identical across installation methods.

    ``COVERWISE_BINARY`` overrides the bundled path. It exists so this package
    can be exercised from an unpacked source tree against a locally built CLI;
    an installed wheel never needs it.

    Raises:
        RuntimeError: The executable cannot be run, naming the path and why.
    """

    override = os.environ.get("COVERWISE_BINARY")
    if override:
        binary = Path(override)
        reason = _unusable_reason(binary)
        if reason is not None:
            raise RuntimeError(f"COVERWISE_BINARY points at {reason}: {binary}")
        return binary

    binary = Path(__file__).parent / "_bin" / "coverwise"
    reason = _unusable_reason(binary)
    if reason is not None:
        raise RuntimeError(
            f"the coverwise native executable is {reason}: {binary}{_PLATFORM_WHEEL_HINT}"
        )
    return binary


def _launch_failure(binary: Path, cause: OSError) -> RuntimeError:
    """Describe an executable the operating system refused to start.

    The checks in :func:`native_binary` cover the failures a caller can act on,
    so what is left here is the narrow residue — a file replaced between the
    check and the launch, or one the kernel cannot execute at all. It still
    reaches the caller as a launch failure naming the path rather than as an
    ``OSError`` that looks like it came from their own code.
    """

    return RuntimeError(f"the coverwise executable at {binary} could not be started: {cause}")


@overload
def run(
    args: Sequence[str], *, text: Literal[True], **kwargs: Any
) -> subprocess.CompletedProcess[str]: ...


@overload
def run(args: Sequence[str], **kwargs: Any) -> subprocess.CompletedProcess[Any]: ...


def run(args: Sequence[str], **kwargs: Any) -> subprocess.CompletedProcess[Any]:
    """Run the bundled CLI and return its completed process result.

    This is a small programmatic convenience for automation. For normal command
    line use, invoke ``coverwise`` directly so it preserves native signals and
    standard I/O without a Python subprocess in the middle.

    Keyword arguments go straight to :func:`subprocess.run`, which decides
    whether ``stdout`` and ``stderr`` are text or bytes. ``text=True`` is
    therefore the only form whose captured output is typed as ``str``; every
    other call reports the loose type ``subprocess.run`` itself would report.

    This is also the one place the package starts a child process, so the model
    API reaches the executable through it and inherits the same diagnostics.

    Raises:
        RuntimeError: The executable cannot be located or started.
    """

    binary = native_binary()
    try:
        return subprocess.run([str(binary), *args], **kwargs)
    except OSError as exc:
        raise _launch_failure(binary, exc) from exc


def main() -> NoReturn:
    """Replace this launcher process with the bundled native executable."""

    binary = native_binary()
    try:
        os.execv(binary, [str(binary), *sys.argv[1:]])
    except OSError as exc:
        raise _launch_failure(binary, exc) from exc
    raise AssertionError("os.execv returned unexpectedly")
