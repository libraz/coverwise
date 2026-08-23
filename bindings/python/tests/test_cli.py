from __future__ import annotations

import json
from pathlib import Path

import pytest

import coverwise


def test_bundled_cli_reports_help() -> None:
    result = coverwise.run(["--help"], text=True, capture_output=True, check=True)

    assert "coverwise generate" in result.stderr


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
