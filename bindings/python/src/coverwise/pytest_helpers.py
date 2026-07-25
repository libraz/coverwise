"""pytest integration: drive a test with a covering set of argument tuples."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

from .api import _build_model, generate

__all__ = ["parametrize"]


def _require_pytest() -> Any:
    try:
        import pytest
    except ImportError as exc:  # pragma: no cover - only without pytest installed
        raise RuntimeError(
            "coverwise.parametrize requires pytest; install it to use this decorator"
        ) from exc
    return pytest


def _argument_names(parameters: Sequence[Mapping[str, Any]]) -> list[str]:
    """Extract parameter names, rejecting any that cannot be a test argument."""

    names = [str(parameter["name"]) for parameter in parameters]
    invalid = [name for name in names if not name.isidentifier()]
    if invalid:
        raise ValueError(
            "parameter names used by coverwise.parametrize must be valid Python "
            f"identifiers; rename {invalid}"
        )
    return names


def parametrize(
    parameters: Any = None,
    *,
    include_negative: bool = False,
    **fields: Any,
) -> Any:
    """Parametrize a test with a covering set of argument tuples.

    Replaces a hand-written full cross-product with a generated t-wise suite:
    the decorated test runs once per generated case, receiving each parameter as
    a same-named argument.

    Args:
        parameters: The parameter definitions, as either the JSON list form or a
            name-to-values mapping. Names must be valid Python identifiers,
            since they become test arguments.
        include_negative: Also run the generated negative tests, which exercise
            values marked ``"invalid": true``. Off by default, so a suite stays
            positive-only unless negative cases are asked for.
        **fields: Further model fields (``strength``, ``constraints``, ``seed``,
            ``maxTests``, ``weights``, ``subModels``, …), passed through to
            :func:`coverwise.generate`. ``strength`` defaults to 2, or to 1 for
            a single-parameter model.

    Returns:
        A ``pytest.mark.parametrize`` marker.

    Raises:
        ValueError: A parameter name is not a valid Python identifier.
        CoverwiseError: The model is invalid or a constraint could not be parsed.

    Example:
        >>> @coverwise.parametrize(
        ...     {"os": ["win", "mac"], "browser": ["chrome", "safari"]},
        ...     constraints=["IF os = mac THEN browser != chrome"],
        ... )
        ... def test_login(os, browser):
        ...     assert launch(os, browser).ok
    """

    pytest = _require_pytest()
    model = _build_model(None, {"parameters": parameters, **fields})
    names = _argument_names(model["parameters"])

    # A one-parameter model has no pairs to cover, so the default strength of 2
    # exceeds the parameter count and the model would be rejected. Parametrizing
    # over a single parameter is still a reasonable thing to ask for.
    if len(names) < 2:
        model.setdefault("strength", 1)

    result = generate(model)
    cases = list(result["tests"])
    if include_negative:
        cases.extend(result["negativeTests"])

    # pytest only unpacks a tuple per case when several argnames are given, so a
    # single-parameter model must pass bare values.
    if len(names) == 1:
        values: list[Any] = [case[names[0]] for case in cases]
    else:
        values = [tuple(case[name] for name in names) for case in cases]
    ids = ["-".join(f"{name}={case[name]}" for name in names) for case in cases]
    return pytest.mark.parametrize(",".join(names), values, ids=ids)
