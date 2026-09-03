"""Model API for coverwise, backed by the bundled native executable.

Every function takes and returns plain JSON-shaped data, so results are
identical to the CLI and to the JavaScript API for the same input.
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
import tempfile
from collections.abc import Iterable, Mapping, Sequence
from collections.abc import Set as AbstractSet
from typing import Any, NamedTuple

from .cli import native_binary

__all__ = [
    "CoverwiseError",
    "analyze_coverage",
    "estimate_model",
    "extend_tests",
    "generate",
]

# Exit codes the native CLI documents. Insufficient coverage is deliberately
# absent from the failing set: it reports a real, complete result whose coverage
# is below 1.0, so the API returns it instead of raising.
_EXIT_OK = 0
_EXIT_CONSTRAINT_ERROR = 1
_EXIT_INSUFFICIENT_COVERAGE = 2
_EXIT_INVALID_INPUT = 3

_ERROR_CODES = {
    _EXIT_CONSTRAINT_ERROR: "CONSTRAINT_ERROR",
    _EXIT_INVALID_INPUT: "INVALID_INPUT",
}


class CoverwiseError(RuntimeError):
    """Raised when the native CLI rejects a model.

    Attributes:
        code: Surface-independent category, matching the JavaScript API's
            ``CoverwiseError.code`` vocabulary (``CONSTRAINT_ERROR``,
            ``INVALID_INPUT``).
        exit_code: The CLI exit code, per the documented exit-code contract;
            always one of the codes that contract defines.
        stderr: The CLI's full diagnostic output.
        report: The JSON document the CLI wrote before it failed, or ``None``
            when it wrote none. ``analyze`` writes its full coverage report
            before rejecting a suite that contains invalid rows, so
            ``report["invalidTests"]`` names those rows and their reasons.
    """

    def __init__(
        self,
        code: str,
        message: str,
        exit_code: int,
        stderr: str,
        report: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.exit_code = exit_code
        self.stderr = stderr
        self.report = report


def _parameter_values(name: Any, values: Any) -> list[Any]:
    """Take the value list a mapping entry declares, refusing to invent one.

    A bare string is iterable, so ``{"env": "prod"}`` would otherwise silently
    become the four-value parameter ``["p", "r", "o", "d"]`` and produce a suite
    that has nothing to do with the model the caller wrote.

    A ``set``/``frozenset`` is rejected for a different reason: its iteration
    order depends on ``PYTHONHASHSEED`` and insertion history, so the same
    model would parametrize a different suite on every run, breaking the
    determinism the rest of the API guarantees.
    """

    if isinstance(values, AbstractSet):
        raise TypeError(
            f"values for parameter {name!r} must be a list, not a "
            f"{type(values).__name__}; an unordered set does not produce a "
            f"reproducible suite, pass sorted(values) or list(values) instead"
        )
    if isinstance(values, (str, bytes, Mapping)) or not isinstance(values, Iterable):
        raise TypeError(
            f"values for parameter {name!r} must be a list of values, not "
            f"{type(values).__name__}; write [{values!r}] for a single value"
        )
    return list(values)


def _normalize_parameters(parameters: Any) -> list[dict[str, Any]]:
    """Accept either the JSON list form or a name-to-values mapping.

    ``{"os": ["win", "mac"]}`` is the same model as
    ``[{"name": "os", "values": ["win", "mac"]}]``; the mapping form exists
    because it reads better in Python call sites. Mappings preserve insertion
    order, so parameter order stays under the caller's control either way.
    """

    if isinstance(parameters, Mapping):
        return [
            {"name": name, "values": _parameter_values(name, values)}
            for name, values in parameters.items()
        ]
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


class _Outcome(NamedTuple):
    """Everything a CLI invocation reported, with nothing dropped.

    ``report`` holds the parsed stdout document whenever the CLI wrote a
    well-formed one, independently of ``returncode``. Several subcommands write
    their report and only then exit non-zero — ``analyze`` does it for a suite
    containing invalid rows — and that body is the only place the reason is
    recorded, so the exit status alone must never decide whether it survives.
    """

    report: dict[str, Any] | None
    stdout: str
    returncode: int
    stderr: str


def _relabel(text: str, path_labels: Mapping[str, str]) -> str:
    """Rewrite internal temporary paths into the argument the caller passed.

    The CLI reports a read failure by interpolating the path it was given, which
    for a side input is a temporary file this module created and has already
    deleted; the caller never saw it and cannot act on it.
    """

    for path, label in path_labels.items():
        text = text.replace(f"file '{path}'", label).replace(f"'{path}'", label)
        text = text.replace(path, label)
    return text


def _invoke(args: Sequence[str], stdin_text: str, path_labels: Mapping[str, str]) -> _Outcome:
    """Run the native CLI, feeding it JSON on standard input.

    UTF-8 is pinned on both directions so non-ASCII parameter values survive a
    process boundary regardless of the ambient locale. Both streams are carried
    out whole: this is the only function that sees the child's stdout, and it
    has no failure branch that can throw it away.
    """

    completed = subprocess.run(
        [str(native_binary()), *args],
        input=stdin_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=False,
    )
    try:
        parsed = json.loads(completed.stdout)
    except json.JSONDecodeError:
        parsed = None
    return _Outcome(
        parsed if isinstance(parsed, dict) else None,
        completed.stdout,
        completed.returncode,
        _relabel(completed.stderr, path_labels),
    )


def _abnormal_exit(returncode: int) -> str:
    """Describe a child exit that is not one of the documented CLI exit codes."""

    if returncode < 0:
        try:
            name = signal.Signals(-returncode).name
        except ValueError:  # pragma: no cover - a signal number Python does not name
            name = f"signal {-returncode}"
        return f"was terminated by {name}"
    return f"exited with status {returncode}"


def _failure(outcome: _Outcome) -> Exception:
    """Build the exception for a non-zero exit, keeping the report reachable."""

    stderr = outcome.stderr.strip()
    message = stderr.splitlines()[0].removeprefix("error: ") if stderr else "coverwise failed"
    code = _ERROR_CODES.get(outcome.returncode)
    if code is None:
        # A crashed or signal-killed executable never classified anything, so
        # reporting it as a model error would send the caller to debug an input
        # that the engine may well have accepted.
        detail = f": {message}" if stderr else ""
        return RuntimeError(
            f"the coverwise executable {_abnormal_exit(outcome.returncode)} instead of "
            f"reporting a result{detail}"
        )
    return CoverwiseError(code, message, outcome.returncode, outcome.stderr, outcome.report)


def _run(
    args: Sequence[str], stdin_text: str, path_labels: Mapping[str, str] | None = None
) -> dict[str, Any]:
    """Run a subcommand and hand its JSON document back to the caller.

    Whether a run raises is decided here, from the exit status; what the caller
    can see is decided by :func:`_invoke`, which never discards a report. A
    non-zero exit therefore still carries the report the CLI wrote, as
    ``CoverwiseError.report``.
    """

    outcome = _invoke(args, stdin_text, path_labels or {})
    if outcome.returncode in (_EXIT_OK, _EXIT_INSUFFICIENT_COVERAGE):
        if outcome.report is None:  # pragma: no cover - a broken bundled binary
            raise RuntimeError(
                f"coverwise produced output that is not JSON: {outcome.stdout[:200]!r}"
            )
        return outcome.report
    raise _failure(outcome)


def _dumps(payload: Any) -> str:
    """Serialize a payload for the CLI, rejecting non-JSON values up front.

    ``allow_nan=False`` turns a non-finite float (``inf``, ``-inf``, ``nan``)
    into a :class:`ValueError`, which is reported as a :class:`CoverwiseError`
    so callers can catch every model rejection — subprocess-side or not —
    through the one documented exception type. A :class:`TypeError`, raised
    for a value ``json`` cannot serialize at all, is left to propagate as-is:
    it is a Python usage error, not a model the CLI ever gets to see.
    """

    try:
        return json.dumps(payload, ensure_ascii=False, allow_nan=False)
    except ValueError as exc:
        raise CoverwiseError(
            _ERROR_CODES[_EXIT_INVALID_INPUT],
            f"model contains a value that is not finite: {exc}",
            _EXIT_INVALID_INPUT,
            "",
            None,
        ) from exc


def _run_with_side_input(
    args_before: Sequence[str],
    side_payload: Any,
    side_label: str,
    args_after: Sequence[str],
    stdin_payload: Any,
) -> dict[str, Any]:
    """Run a subcommand that needs two JSON inputs.

    Standard input can carry only one of them, so the other goes through a
    temporary file that is removed before the result is returned. The temp file
    is written and closed before the child starts, because the CLI reads it by
    path. ``side_label`` names the argument that file stands for, so diagnostics
    about it point at something the caller passed.
    """

    handle, side_path = tempfile.mkstemp(prefix="coverwise-", suffix=".json")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as side_file:
            side_file.write(_dumps(side_payload))
        return _run(
            [*args_before, side_path, *args_after, "-"],
            _dumps(stdin_payload),
            {side_path: side_label},
        )
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
        ``uncovered``. A suite that leaves tuples uncovered is returned rather
        than raised.

    Raises:
        CoverwiseError: The parameters, tests, or constraints are invalid. A
            suite whose rows are rejected is still measured, so the report
            naming them in ``invalidTests`` is available as
            ``CoverwiseError.report``.
    """

    params_payload: dict[str, Any] = {
        "parameters": _normalize_parameters(parameters),
    }
    if constraints is not None:
        params_payload["constraints"] = list(constraints)
    return _run_with_side_input(
        ["analyze", "--strength", str(strength), "--tests"],
        tests,
        "the 'tests' argument",
        ["--params"],
        params_payload,
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

    return _run_with_side_input(
        ["extend", "--existing"],
        existing,
        "the 'existing' argument",
        [],
        _build_model(model, fields),
    )


def estimate_model(model: Mapping[str, Any] | None = None, **fields: Any) -> dict[str, Any]:
    """Report model statistics without generating a suite.

    Validates constraint syntax and parameter references, then reports the
    pre-constraint tuple count and an estimated suite size.

    ``estimatedTests`` is a coarse sizing heuristic from the largest value
    count, the strength and the parameter count, capped at ``totalTuples``. It
    is not a bound in either direction: a generated suite may be smaller or
    larger.

    Args:
        model: A JSON-shaped model mapping, as accepted by the CLI.
        **fields: Individual model fields, overriding ``model``.

    Returns:
        The CLI's ``stats`` output.

    Raises:
        CoverwiseError: The model is invalid or a constraint could not be parsed.
    """

    return _run(["stats", "-"], _dumps(_build_model(model, fields)))
