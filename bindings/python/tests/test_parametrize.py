from __future__ import annotations

import pytest

import coverwise

MODEL = {"os": ["win", "mac", "linux"], "browser": ["chrome", "firefox", "safari"]}
CONSTRAINTS = ["IF os = mac THEN browser != chrome"]


@coverwise.parametrize(MODEL, constraints=CONSTRAINTS)
def test_decorated_test_receives_each_generated_case(os: str, browser: str) -> None:
    """A decorated test runs once per generated case, with named arguments."""

    assert (os, browser) != ("mac", "chrome")


def test_generated_cases_cover_every_allowed_pair() -> None:
    mark = coverwise.parametrize(MODEL, constraints=CONSTRAINTS)
    argnames, argvalues = mark.mark.args
    allowed = {
        (os, browser)
        for os in MODEL["os"]
        for browser in MODEL["browser"]
        if (os, browser) != ("mac", "chrome")
    }

    assert argnames == "os,browser"
    assert set(argvalues) == allowed


def test_single_parameter_models_pass_bare_values() -> None:
    mark = coverwise.parametrize({"os": ["win", "mac"]}, strength=1)
    argnames, argvalues = mark.mark.args

    assert argnames == "os"
    assert sorted(argvalues) == ["mac", "win"]


def test_single_parameter_models_do_not_need_an_explicit_strength() -> None:
    """The default strength of 2 cannot apply to a one-parameter model."""

    mark = coverwise.parametrize({"os": ["win", "mac"]})

    assert sorted(mark.mark.args[1]) == ["mac", "win"]


def test_case_ids_name_the_values() -> None:
    mark = coverwise.parametrize({"os": ["win"], "browser": ["chrome"]})

    assert mark.mark.kwargs["ids"] == ["os=win-browser=chrome"]


def test_negative_tests_are_excluded_unless_requested() -> None:
    model = {
        "os": ["win", "mac"],
        "browser": ["chrome", {"value": "ie", "invalid": True}],
    }

    positive_only = coverwise.parametrize(model)
    with_negative = coverwise.parametrize(model, include_negative=True)

    assert all("ie" not in values for values in positive_only.mark.args[1])
    assert any("ie" in values for values in with_negative.mark.args[1])


def test_parameter_names_must_be_python_identifiers() -> None:
    with pytest.raises(ValueError, match="valid Python identifiers"):
        coverwise.parametrize({"os version": ["11", "12"], "browser": ["chrome", "safari"]})


def test_parameter_names_must_not_be_python_keywords() -> None:
    """A keyword is a valid identifier, but no test can declare it as an argument."""

    with pytest.raises(ValueError, match=r"rename \['class', 'lambda'\]"):
        coverwise.parametrize(
            {
                "class": ["a", "b"],
                "lambda": ["x", "y"],
                "browser": ["chrome", "safari"],
            }
        )


def test_soft_keywords_are_usable_parameter_names() -> None:
    """``type`` and ``match`` are ordinary argument names outside their syntax."""

    mark = coverwise.parametrize({"type": ["a", "b"], "match": ["x", "y"]})

    assert mark.mark.args[0] == "type,match"


def test_declared_python_types_reach_the_test() -> None:
    """Every argument equals, in type and value, a value the caller declared."""

    model = {
        "retries": [0, 3],
        "debug": [True, False],
        "ratio": [0.5, 1.5],
        "name": ["a", "b"],
    }
    mark = coverwise.parametrize(model)
    names = mark.mark.args[0].split(",")
    cases = [dict(zip(names, row, strict=True)) for row in mark.mark.args[1]]

    assert cases
    for case in cases:
        for name, value in case.items():
            assert any(type(value) is type(d) and value == d for d in model[name]), (name, value)
    assert {case["retries"] for case in cases} == {0, 3}
    assert {case["debug"] for case in cases} == {True, False}


def test_value_objects_and_negative_cases_keep_their_declared_type() -> None:
    model = {
        "level": [1, 2, {"value": -1, "invalid": True}],
        "flag": [True, {"value": False}],
    }
    mark = coverwise.parametrize(model, include_negative=True)

    levels = {row[0] for row in mark.mark.args[1]}
    assert levels == {1, 2, -1}
    assert all(type(level) is int for level in levels)
    assert all(type(row[1]) is bool for row in mark.mark.args[1])


def test_boundary_parameters_receive_numbers() -> None:
    mark = coverwise.parametrize(
        [
            {"name": "size", "values": [], "type": "integer", "range": [1, 10]},
            {"name": "ratio", "values": [], "type": "float", "range": [0, 1], "step": 0.5},
        ]
    )

    assert all(type(row[0]) is int for row in mark.mark.args[1])
    assert all(type(row[1]) is float for row in mark.mark.args[1])


def test_case_ids_are_unchanged_by_value_conversion() -> None:
    mark = coverwise.parametrize({"retries": [3], "debug": [True]})

    assert mark.mark.kwargs["ids"] == ["retries=3-debug=true"]
