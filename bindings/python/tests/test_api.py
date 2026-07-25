from __future__ import annotations

import itertools

import pytest

import coverwise

MODEL = {
    "parameters": [
        {"name": "os", "values": ["win", "mac", "linux"]},
        {"name": "browser", "values": ["chrome", "firefox", "safari"]},
        {"name": "theme", "values": ["light", "dark"]},
    ]
}


def covered_pairs(tests, names):
    """Enumerate every value pair the suite covers, independently of the engine."""

    pairs = set()
    for test in tests:
        for left, right in itertools.combinations(names, 2):
            pairs.add((left, test[left], right, test[right]))
    return pairs


def all_pairs(parameters):
    """Enumerate the full pairwise universe from the model definition."""

    pairs = set()
    for left, right in itertools.combinations(parameters, 2):
        for left_value, right_value in itertools.product(left["values"], right["values"]):
            pairs.add((left["name"], left_value, right["name"], right_value))
    return pairs


def test_generate_covers_every_pair() -> None:
    result = coverwise.generate(MODEL)

    assert result["coverage"] == 1.0
    assert result["uncovered"] == []
    names = [p["name"] for p in MODEL["parameters"]]
    assert covered_pairs(result["tests"], names) == all_pairs(MODEL["parameters"])


def test_generate_accepts_a_name_to_values_mapping() -> None:
    from_mapping = coverwise.generate(
        parameters={p["name"]: p["values"] for p in MODEL["parameters"]}
    )
    from_list = coverwise.generate(MODEL)

    assert from_mapping["tests"] == from_list["tests"]


def test_generate_merges_keyword_fields_over_the_model() -> None:
    result = coverwise.generate(MODEL, strength=3)

    assert result["strength"] == 3
    assert result["coverage"] == 1.0


def test_generate_honors_constraints() -> None:
    result = coverwise.generate(MODEL, constraints=["IF os = mac THEN browser != chrome"])

    assert result["coverage"] == 1.0
    assert all(
        not (test["os"] == "mac" and test["browser"] == "chrome") for test in result["tests"]
    )


def test_generate_is_deterministic_for_a_fixed_seed() -> None:
    first = coverwise.generate(MODEL, seed=1234)
    second = coverwise.generate(MODEL, seed=1234)

    assert first["tests"] == second["tests"]


def test_generate_returns_an_incomplete_suite_instead_of_raising() -> None:
    result = coverwise.generate(MODEL, maxTests=2)

    assert len(result["tests"]) == 2
    assert result["coverage"] < 1.0
    assert result["uncovered"]


def test_generate_preserves_non_ascii_values() -> None:
    result = coverwise.generate(parameters={"言語": ["日本語", "絵文字🎉"], "b": ["x", "y"]})

    assert result["coverage"] == 1.0
    assert {test["言語"] for test in result["tests"]} == {"日本語", "絵文字🎉"}


def test_generate_reports_a_constraint_error() -> None:
    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.generate(MODEL, constraints=["IF os = = THEN"])

    assert excinfo.value.code == "CONSTRAINT_ERROR"
    assert excinfo.value.exit_code == 1
    assert "constraint" in str(excinfo.value).lower()
    assert excinfo.value.stderr


def test_generate_reports_invalid_input() -> None:
    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.generate(parameters=[{"name": "os", "values": ["win", "win"]}])

    assert excinfo.value.code == "INVALID_INPUT"
    assert excinfo.value.exit_code == 3


def test_generate_requires_parameters() -> None:
    with pytest.raises(TypeError):
        coverwise.generate(strength=2)


def test_analyze_coverage_reports_missing_pairs() -> None:
    report = coverwise.analyze_coverage(
        {"os": ["win", "mac"], "browser": ["chrome", "safari"]},
        [{"os": "win", "browser": "chrome"}],
    )

    assert report["coverageRatio"] == pytest.approx(0.25)
    assert report["uncoveredCount"] == 3
    assert {tuple(item["tuple"]) for item in report["uncovered"]} == {
        ("os=win", "browser=safari"),
        ("os=mac", "browser=chrome"),
        ("os=mac", "browser=safari"),
    }


def test_analyze_coverage_accepts_a_generate_result_envelope() -> None:
    generated = coverwise.generate(MODEL)

    report = coverwise.analyze_coverage(MODEL["parameters"], generated)

    assert report["coverageRatio"] == 1.0
    assert report["uncovered"] == []


def test_analyze_coverage_honors_strength_and_constraints() -> None:
    constraints = ["IF os = mac THEN browser != chrome"]
    generated = coverwise.generate(MODEL, strength=3, constraints=constraints)

    report = coverwise.analyze_coverage(
        MODEL["parameters"], generated["tests"], strength=3, constraints=constraints
    )

    assert report["coverageRatio"] == 1.0


def test_extend_tests_keeps_existing_tests_first() -> None:
    existing = [{"os": "win", "browser": "chrome", "theme": "light"}]

    result = coverwise.extend_tests(existing, MODEL)

    assert result["tests"][0] == existing[0]
    assert len(result["tests"]) > 1
    assert result["coverage"] == 1.0


def test_estimate_model_reports_the_tuple_budget() -> None:
    stats = coverwise.estimate_model(MODEL)

    assert stats["parameterCount"] == 3
    assert stats["totalValues"] == 8
    assert stats["totalTuples"] == 3 * 3 + 3 * 2 + 3 * 2
    assert [p["name"] for p in stats["parameters"]] == ["os", "browser", "theme"]


def test_estimate_model_rejects_an_unknown_constraint_parameter() -> None:
    with pytest.raises(coverwise.CoverwiseError):
        coverwise.estimate_model(MODEL, constraints=["IF missing = 1 THEN os = win"])
