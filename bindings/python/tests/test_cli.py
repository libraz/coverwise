from __future__ import annotations

import json

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
