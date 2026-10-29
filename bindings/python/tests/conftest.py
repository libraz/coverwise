"""Shared fixtures for the Python binding tests."""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence
from typing import Any

import pytest

import coverwise


def _recount_coverage(
    model: Mapping[str, Any],
    suite: Mapping[str, Any] | Sequence[Mapping[str, Any]],
    *,
    names: Sequence[str] | None = None,
    strength: int | None = None,
) -> dict[str, Any]:
    """Recount a generated suite's coverage with ``analyze_coverage``.

    The generator's own ``coverage`` figure is never the judge: the suite's rows
    are measured again against the model by the independent validator. ``names``
    restricts the measurement to a subset of the parameters (rows projected onto
    them), which is how a sub-model group is checked at its own strength.
    """

    tests = suite["tests"] if isinstance(suite, Mapping) else suite
    parameters = coverwise.api._normalize_parameters(model["parameters"])
    constraints = model.get("constraints")
    if names is not None:
        parameters = [p for p in parameters if p["name"] in names]
        tests = [{name: test[name] for name in names} for test in tests]
        constraints = None
    return coverwise.analyze_coverage(
        parameters,
        tests,
        strength=strength if strength is not None else model.get("strength", 2),
        constraints=constraints,
    )


@pytest.fixture
def recount_coverage() -> Callable[..., dict[str, Any]]:
    return _recount_coverage
