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
