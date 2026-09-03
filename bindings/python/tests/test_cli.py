from __future__ import annotations

import json
import os
import subprocess
import sysconfig
from pathlib import Path

import pytest

import coverwise
import coverwise.cli

# The command a user installs, as opposed to the executable it hands over to.
CONSOLE_SCRIPT = Path(sysconfig.get_path("scripts")) / "coverwise"

# The largest pipe buffer in common use. A reader closing early only reaches the
# writer while the writer is still writing, so a report has to outlast what the
# kernel accepts with nobody reading for the case to be exercised at all.
LARGEST_PIPE_BUFFER = 64 * 1024


@pytest.mark.parametrize("flag", ["--help", "-h"])
def test_bundled_cli_reports_help_on_standard_output(flag) -> None:
    """Usage the caller asked for is output the command produced, so it goes to
    standard output; only usage printed because an invocation was wrong stays
    on standard error."""

    result = coverwise.run([flag], text=True, capture_output=True, check=True)

    assert "coverwise generate" in result.stdout
    assert result.stderr == ""


@pytest.mark.parametrize("args", [[], ["nosuchcommand"]])
def test_a_usage_error_is_diagnosed_on_standard_error(args) -> None:
    result = coverwise.run(args, text=True, capture_output=True, check=False)

    assert result.returncode == 3
    assert result.stdout == ""
    assert result.stderr != ""


def test_bundled_cli_generates_a_complete_pairwise_suite(tmp_path) -> None:
    input_path = tmp_path / "input.json"
    input_path.write_text(
        json.dumps(
            {
                "parameters": [
                    {"name": "os", "values": ["Linux", "macOS"]},
                    {"name": "browser", "values": ["Chrome", "Firefox"]},
                ],
                "strength": 2,
                "seed": 42,
            }
        )
    )

    result = coverwise.run(
        ["generate", str(input_path)], text=True, capture_output=True, check=True
    )
    payload = json.loads(result.stdout)

    assert payload["coverage"] == 1.0
    assert len(payload["tests"]) == 4


def model_with_a_report_past_the_pipe_buffer(tmp_path):
    """Write a model whose report is far larger than any platform's pipe buffer."""

    model = {
        "parameters": [
            {"name": f"p{index}", "values": [f"value-{index}-{step}" for step in range(24)]}
            for index in range(16)
        ],
        "strength": 2,
        "seed": 7,
    }
    path = tmp_path / "oversized.json"
    path.write_text(json.dumps(model), encoding="utf-8")
    return path


def run_until_the_reader_closes(argv, stderr_path):
    """Start a producer, take one byte, and close the pipe while it still writes.

    Standard error goes to a file rather than a second pipe: reading one pipe
    while the writer is blocked on the other is how a test like this hangs
    instead of failing.
    """

    with stderr_path.open("w", encoding="utf-8") as stderr_file:
        process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=stderr_file, text=True)
        try:
            process.stdout.read(1)
        finally:
            process.stdout.close()
        try:
            exit_code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:  # pragma: no cover - a writer that never notices
            process.kill()
            raise
    return exit_code, stderr_path.read_text(encoding="utf-8")


def test_the_console_script_and_the_executable_agree_when_a_reader_closes_the_pipe(
    tmp_path,
) -> None:
    """``coverwise generate model.json | head`` is one input reaching two shipped
    commands, so it cannot end two ways.

    The console script is a Python process that becomes the executable, and the
    signal dispositions it was started with survive that. Whether a reader
    closing early ends the run as a documented exit code or as a signal
    therefore cannot be left to the disposition each command inherits: both have
    to reach the same status and say the same thing on standard error.
    """

    model_path = model_with_a_report_past_the_pipe_buffer(tmp_path)
    complete = coverwise.run(
        ["generate", str(model_path)], text=True, capture_output=True, check=True
    )
    assert len(complete.stdout) > LARGEST_PIPE_BUFFER
    assert CONSOLE_SCRIPT.is_file()

    from_executable = run_until_the_reader_closes(
        [str(coverwise.native_binary()), "generate", str(model_path)],
        tmp_path / "executable-stderr.txt",
    )
    from_console_script = run_until_the_reader_closes(
        [str(CONSOLE_SCRIPT), "generate", str(model_path)],
        tmp_path / "console-script-stderr.txt",
    )

    assert from_console_script == from_executable
    exit_code, diagnostic = from_console_script
    assert exit_code == 3
    assert "write" in diagnostic.lower()


def test_the_environment_override_selects_the_executable(tmp_path, monkeypatch) -> None:
    stand_in = tmp_path / "coverwise"
    stand_in.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    stand_in.chmod(0o755)
    monkeypatch.setenv("COVERWISE_BINARY", str(stand_in))

    assert coverwise.native_binary() == stand_in


def test_the_environment_override_must_point_at_an_existing_file(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("COVERWISE_BINARY", str(tmp_path / "absent"))

    with pytest.raises(RuntimeError, match="COVERWISE_BINARY points at a missing file"):
        coverwise.native_binary()


def test_a_package_without_the_bundled_executable_asks_for_a_platform_wheel(monkeypatch) -> None:
    monkeypatch.delenv("COVERWISE_BINARY", raising=False)
    monkeypatch.setattr(Path, "is_file", lambda self: False)

    with pytest.raises(RuntimeError, match="install a supported platform wheel"):
        coverwise.native_binary()


# Every way an executable can be unreachable, and every public way of reaching
# for one. A check that covers a single failure mode, or sits on a single entry
# point, leaves a caller with whichever OS error the launch happened to raise.
UNUSABLE_CAUSES = {
    "missing": "a missing file",
    "not a file": "a path that is not a file",
    "not executable": "a file without execute permission",
}

LAUNCHERS = {
    "native_binary": coverwise.native_binary,
    "run": lambda: coverwise.run(["--help"], capture_output=True),
    "main": coverwise.cli.main,
    "generate": lambda: coverwise.generate(parameters={"os": ["win", "mac"], "b": ["x", "y"]}),
}

# Locating the executable is every launcher's first step; only these go on to
# hand it to the operating system.
PROCESS_LAUNCHERS = {
    name: launcher for name, launcher in LAUNCHERS.items() if name != "native_binary"
}


def unusable_binary(tmp_path, mode):
    """Produce a path that cannot serve as the native executable."""

    if mode == "missing":
        return tmp_path / "absent"
    if mode == "not a file":
        directory = tmp_path / "a-directory"
        directory.mkdir()
        return directory
    without_mode_bit = tmp_path / "not-executable"
    without_mode_bit.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    without_mode_bit.chmod(0o644)
    return without_mode_bit


@pytest.mark.parametrize("launcher", LAUNCHERS.values(), ids=list(LAUNCHERS))
@pytest.mark.parametrize("mode", list(UNUSABLE_CAUSES))
def test_an_unusable_executable_is_reported_as_a_runtime_error(
    tmp_path, monkeypatch, launcher, mode
) -> None:
    """A wheel with a broken payload and a misconfigured override are the same
    kind of failure, and neither is a model the engine rejected, so both name
    the path and the cause rather than surfacing as an operating system error."""

    target = unusable_binary(tmp_path, mode)
    monkeypatch.setenv("COVERWISE_BINARY", str(target))

    with pytest.raises(RuntimeError) as excinfo:
        launcher()

    assert not isinstance(excinfo.value, OSError)
    assert not isinstance(excinfo.value, coverwise.CoverwiseError)
    assert str(target) in str(excinfo.value)
    assert UNUSABLE_CAUSES[mode] in str(excinfo.value)


@pytest.mark.parametrize("launcher", PROCESS_LAUNCHERS.values(), ids=list(PROCESS_LAUNCHERS))
def test_an_executable_the_kernel_refuses_to_start_is_reported_as_a_runtime_error(
    tmp_path, monkeypatch, launcher
) -> None:
    """The file passes every check a caller could act on and still cannot run,
    which is the residue the checks cannot cover; it names the path all the same."""

    target = tmp_path / "not-a-program"
    target.write_bytes(b"\x00\x01 not a program\n")
    target.chmod(0o755)
    monkeypatch.setenv("COVERWISE_BINARY", str(target))

    with pytest.raises(RuntimeError) as excinfo:
        launcher()

    assert not isinstance(excinfo.value, OSError)
    assert str(target) in str(excinfo.value)


def test_a_bundled_executable_without_its_mode_bit_asks_for_a_platform_wheel(monkeypatch) -> None:
    """The bundled path meets the same checks as the override, so a wheel whose
    payload lost its mode bit is diagnosed as a wheel problem."""

    real_access = os.access
    monkeypatch.delenv("COVERWISE_BINARY", raising=False)
    monkeypatch.setattr(
        os,
        "access",
        lambda path, mode, **kwargs: (
            False if str(path).endswith("_bin/coverwise") else real_access(path, mode, **kwargs)
        ),
    )

    with pytest.raises(RuntimeError) as excinfo:
        coverwise.native_binary()

    assert "a file without execute permission" in str(excinfo.value)
    assert "install a supported platform wheel" in str(excinfo.value)
