from __future__ import annotations

import json
from pathlib import Path

import pytest

import coverwise


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
