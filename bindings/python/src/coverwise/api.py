"""Model API for coverwise, backed by the bundled native executable.

Every function takes and returns plain JSON-shaped data, so results are
identical to the CLI and to the JavaScript API for the same input.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from collections.abc import Iterable, Mapping, Sequence
from typing import Any

from .cli import native_binary

__all__ = [
    "CoverwiseError",
    "analyze_coverage",
    "estimate_model",
    "extend_tests",
    "generate",
]

# Exit codes the native CLI documents. Insufficient coverage is deliberately
# absent: it reports a real, complete result whose coverage is below 1.0, so the
# API returns it instead of raising.
_EXIT_OK = 0
_EXIT_INSUFFICIENT_COVERAGE = 2

_ERROR_CODES = {
    1: "CONSTRAINT_ERROR",
    3: "INVALID_INPUT",
}


class CoverwiseError(RuntimeError):
    """Raised when the native CLI rejects a model.

    Attributes:
        code: Surface-independent category, matching the JavaScript API's
            ``CoverwiseError.code`` vocabulary (``CONSTRAINT_ERROR``,
            ``INVALID_INPUT``).
        exit_code: The CLI exit code, per the documented exit-code contract.
        stderr: The CLI's full diagnostic output.
    """

    def __init__(self, code: str, message: str, exit_code: int, stderr: str) -> None:
        super().__init__(message)
        self.code = code
        self.exit_code = exit_code
        self.stderr = stderr


def _normalize_parameters(parameters: Any) -> list[dict[str, Any]]:
    """Accept either the JSON list form or a name-to-values mapping.

    ``{"os": ["win", "mac"]}`` is the same model as
    ``[{"name": "os", "values": ["win", "mac"]}]``; the mapping form exists
    because it reads better in Python call sites. Mappings preserve insertion
    order, so parameter order stays under the caller's control either way.
    """

    if isinstance(parameters, Mapping):
        return [{"name": name, "values": list(values)} for name, values in parameters.items()]
    if isinstance(parameters, str) or not isinstance(parameters, Iterable):
        raise TypeError(
            "parameters must be a list of parameter objects or a name-to-values mapping"
        )
    return [dict(p) if isinstance(p, Mapping) else p for p in parameters]


def _build_model(model: Mapping[str, Any] | None, fields: Mapping[str, Any]) -> dict[str, Any]:
    """Merge the positional model mapping with keyword fields.

    Keyword fields win, so a caller can pass a stored model and override a
    single field. ``None`` values are dropped so optional keyword arguments can
    default to ``None`` without emitting ``null`` into the JSON the CLI parses.
    """

    merged: dict[str, Any] = dict(model) if model is not None else {}
    merged.update({key: value for key, value in fields.items() if value is not None})
    if "parameters" not in merged:
        raise TypeError("a model requires 'parameters'")
    merged["parameters"] = _normalize_parameters(merged["parameters"])
    return merged


def _run(args: Sequence[str], stdin_text: str) -> dict[str, Any]:
    """Run the native CLI, feeding it JSON on standard input.

    UTF-8 is pinned on both directions so non-ASCII parameter values survive a
    process boundary regardless of the ambient locale.
    """

    completed = subprocess.run(
        [str(native_binary()), *args],
        input=stdin_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=False,
    )
    if completed.returncode not in (_EXIT_OK, _EXIT_INSUFFICIENT_COVERAGE):
        stderr = completed.stderr.strip()
        message = stderr.splitlines()[0] if stderr else "coverwise failed"
        raise CoverwiseError(
            _ERROR_CODES.get(completed.returncode, "INVALID_INPUT"),
            message.removeprefix("error: "),
            completed.returncode,
            completed.stderr,
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:  # pragma: no cover - a broken bundled binary
        raise RuntimeError(
            f"coverwise produced output that is not JSON: {completed.stdout[:200]!r}"
        ) from exc


def _dumps(payload: Any) -> str:
    """Serialize a payload for the CLI, rejecting non-JSON values up front."""

    return json.dumps(payload, ensure_ascii=False, allow_nan=False)


def _run_with_side_input(
    args_before: Sequence[str], side_payload: Any, args_after: Sequence[str], stdin_payload: Any
) -> dict[str, Any]:
    """Run a subcommand that needs two JSON inputs.

    Standard input can carry only one of them, so the other goes through a
    temporary file that is removed before the result is returned. The temp file
    is written and closed before the child starts, because the CLI reads it by
    path.
    """

    handle, side_path = tempfile.mkstemp(prefix="coverwise-", suffix=".json")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as side_file:
            side_file.write(_dumps(side_payload))
        return _run([*args_before, side_path, *args_after, "-"], _dumps(stdin_payload))
    finally:
        os.unlink(side_path)


def generate(model: Mapping[str, Any] | None = None, **fields: Any) -> dict[str, Any]:
    """Generate a covering test suite.

    Args:
        model: A JSON-shaped model mapping, as accepted by the CLI.
        **fields: Individual model fields, overriding ``model``. ``parameters``
            accepts either the JSON list form or a name-to-values mapping.

    Returns:
        The CLI's ``generate`` result, including ``tests``, ``coverage``, and
        ``uncovered``. A suite that cannot reach full coverage is returned with
        ``coverage`` below 1.0 rather than raising.

    Raises:
        CoverwiseError: The model is invalid or a constraint could not be parsed.

    Example:
        >>> result = generate(
        ...     parameters={"os": ["win", "mac"], "browser": ["chrome", "safari"]},
        ...     strength=2,
        ... )
        >>> result["coverage"] == 1.0
        True
    """

    return _run(["generate", "-"], _dumps(_build_model(model, fields)))


def analyze_coverage(
    parameters: Any,
    tests: Sequence[Mapping[str, Any]] | Mapping[str, Any],
    strength: int = 2,
    constraints: Sequence[str] | None = None,
) -> dict[str, Any]:
    """Analyze the t-wise coverage of an existing test suite.

    Args:
        parameters: The parameter definitions the suite is measured against.
        tests: The test cases to measure. Accepts a bare list of test cases or
            a ``generate`` result envelope.
        strength: Interaction strength.
        constraints: Constraint expressions; tuples with no valid completion are
            excluded from the coverage universe.

    Returns:
        The CLI's ``analyze`` report, including ``coverageRatio`` and
        ``uncovered``.

    Raises:
        CoverwiseError: The parameters, tests, or constraints are invalid.
    """

    params_payload: dict[str, Any] = {
        "parameters": _normalize_parameters(parameters),
    }
    if constraints is not None:
        params_payload["constraints"] = list(constraints)
    return _run_with_side_input(
        ["analyze", "--strength", str(strength), "--tests"], tests, ["--params"], params_payload
    )


def extend_tests(
    existing: Sequence[Mapping[str, Any]] | Mapping[str, Any],
    model: Mapping[str, Any] | None = None,
    **fields: Any,
) -> dict[str, Any]:
    """Extend an existing suite until the model is covered.

    Existing tests are kept as-is and appear first in the returned ``tests``.

    Args:
        existing: The test cases to keep. Accepts a bare list of test cases or a
            ``generate`` result envelope.
        model: A JSON-shaped model mapping, as accepted by the CLI.
        **fields: Individual model fields, overriding ``model``.

    Returns:
        The CLI's ``extend`` result: the full extended suite plus coverage.

    Raises:
        CoverwiseError: The model or the existing tests are invalid.
    """

    return _run_with_side_input(["extend", "--existing"], existing, [], _build_model(model, fields))


def estimate_model(model: Mapping[str, Any] | None = None, **fields: Any) -> dict[str, Any]:
    """Report model statistics without generating a suite.

    Validates constraint syntax and parameter references, then reports the
    pre-constraint tuple count and an estimated suite size.

    Args:
        model: A JSON-shaped model mapping, as accepted by the CLI.
        **fields: Individual model fields, overriding ``model``.

    Returns:
        The CLI's ``stats`` output.

    Raises:
        CoverwiseError: The model is invalid or a constraint could not be parsed.
    """

    return _run(["stats", "-"], _dumps(_build_model(model, fields)))
