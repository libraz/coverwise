"""Each documented input limit, one step past it, through the Python API.

The limit-case table is shared with the CLI and WebAssembly runners: each case
names a limit, how to build an input one step past it, and the message the
command line gives. Python hands the model to the native CLI, so the CLI's
message is the one a caller sees.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

import coverwise

TABLE = json.loads(
    (Path(__file__).resolve().parents[3] / "tests" / "fixtures" / "limit_cases.json").read_text(
        encoding="utf-8"
    )
)["cases"]


def named(count: int, values) -> list[dict[str, Any]]:
    return [{"name": f"p{i}", "values": values(i)} for i in range(count)]


def build(spec: dict[str, Any]) -> dict[str, Any]:
    """Build the model a case describes, one step past its limit."""

    kind = spec["kind"]
    count = spec.get("count", 0)
    size = spec.get("bytes", 0)
    if kind == "parameters":
        return {"parameters": named(count, lambda _: ["v"])}
    if kind == "values":
        return {
            "parameters": [
                {"name": "p0", "values": [f"v{j}" for j in range(count)]},
                {"name": "p1", "values": ["a"]},
            ]
        }
    if kind == "rows":
        return {
            "parameters": [
                {"name": "p0", "values": ["v0"]},
                {"name": "p1", "values": ["a"]},
            ],
            "tests": [{"p0": "v0", "p1": "a"} for _ in range(count)],
        }
    if kind == "constraints":
        return {
            "parameters": [
                {"name": "p0", "values": ["a", "b"]},
                {"name": "p1", "values": ["x", "y"]},
            ],
            "constraints": ["p0 = a"] * count,
        }
    if kind == "long-value":
        return {
            "parameters": [
                {"name": "p0", "values": ["a" * size]},
                {"name": "p1", "values": ["x"]},
            ]
        }
    if kind == "long-values":
        return {
            "parameters": [
                {"name": "p0", "values": [f"{j}:".ljust(size, "a") for j in range(count)]},
                {"name": "p1", "values": ["x"]},
            ]
        }
    if kind == "uniform":
        return {
            "parameters": named(
                spec["parameters"], lambda _: [f"v{j}" for j in range(spec["values"])]
            ),
            "strength": spec["strength"],
        }
    raise AssertionError(f"unknown builder {kind}")


@pytest.mark.parametrize("case", TABLE, ids=[case["name"] for case in TABLE])
def test_a_limit_is_refused_with_the_documented_message(case: dict[str, Any]) -> None:
    model = build(case["build"])

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        if case["entry"] == "analyze":
            coverwise.analyze_coverage(model["parameters"], model["tests"])
        else:
            coverwise.generate(model)

    error = excinfo.value
    assert error.code == "INVALID_INPUT"
    assert error.exit_code == 3
    assert case["cli"] in str(error)
    if "cliBody" in case:
        assert case["cliBody"] in json.dumps(error.report, separators=(",", ":"))
