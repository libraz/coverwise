from __future__ import annotations

import itertools
import json
import os
import subprocess
import sys
import tempfile

import pytest

import coverwise
import coverwise.api

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


def cli_analyze(tmp_path, params_payload, tests, strength=2):
    """Analyze a suite by invoking the CLI directly, as the reference output."""

    params_path = tmp_path / "params.json"
    tests_path = tmp_path / "tests.json"
    params_path.write_text(json.dumps(params_payload), encoding="utf-8")
    tests_path.write_text(json.dumps(tests), encoding="utf-8")

    completed = coverwise.run(
        [
            "analyze",
            "--strength",
            str(strength),
            "--tests",
            str(tests_path),
            "--params",
            str(params_path),
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    return json.loads(completed.stdout), completed.returncode


def stand_in_binary(tmp_path, body):
    """Write an executable that stands in for the native CLI."""

    script = tmp_path / "coverwise-stand-in"
    script.write_text(f"#!/bin/sh\n{body}\n", encoding="utf-8")
    script.chmod(0o755)
    return script


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


@pytest.mark.parametrize("value", [float("inf"), float("-inf"), float("nan")])
def test_a_non_finite_value_is_rejected_before_the_subprocess_starts(monkeypatch, value) -> None:
    """A non-finite value fails while the payload is built, never inside the CLI.

    ``coverwise.CoverwiseError`` is the one documented exception the API
    raises, so a value that never reaches the subprocess boundary must still
    surface through it rather than as a bare ``ValueError``.
    """

    def unreachable(*args, **kwargs):
        raise AssertionError("the native binary must not run for a non-finite value")

    monkeypatch.setattr(coverwise.api.subprocess, "run", unreachable)

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.generate(parameters={"timeout": [1.0, value], "os": ["win", "mac"]})

    assert excinfo.value.code == "INVALID_INPUT"
    assert excinfo.value.exit_code == 3
    assert "not finite" in str(excinfo.value)


def test_generate_requires_parameters() -> None:
    with pytest.raises(TypeError):
        coverwise.generate(strength=2)


def test_generate_rejects_a_bare_string_where_a_value_list_belongs() -> None:
    with pytest.raises(TypeError, match=r"\['prod'\] for a single value"):
        coverwise.generate(parameters={"env": "prod", "os": ["win", "mac"]})


def test_generate_rejects_a_scalar_where_a_value_list_belongs() -> None:
    with pytest.raises(TypeError, match=r"\[3\] for a single value"):
        coverwise.generate(parameters={"retries": 3, "os": ["win", "mac"]})


def test_generate_accepts_a_single_element_value_list() -> None:
    result = coverwise.generate(parameters={"env": ["prod"], "os": ["win", "mac"]})

    assert result["coverage"] == 1.0
    assert {test["env"] for test in result["tests"]} == {"prod"}


@pytest.mark.parametrize("unordered", [{"win", "mac"}, frozenset({"win", "mac"})])
def test_generate_rejects_a_set_where_a_value_list_belongs(unordered) -> None:
    """A set's iteration order depends on the hash seed, so it cannot back a
    deterministic suite the way a list of the same values does."""

    with pytest.raises(TypeError, match="unordered set"):
        coverwise.generate(parameters={"os": unordered, "browser": ["chrome", "safari"]})


def test_accepted_container_order_does_not_depend_on_the_hash_seed() -> None:
    """The values a list accepts must parametrize the same suite under every
    ``PYTHONHASHSEED``, since that seed can only be fixed before interpreter
    start; each seed here runs in its own subprocess to prove it."""

    script = (
        "import json, coverwise\n"
        "result = coverwise.generate(\n"
        "    parameters={'os': ['win', 'mac', 'linux'], 'browser': ['chrome', 'firefox']}\n"
        ")\n"
        "print(json.dumps(result['tests']))\n"
    )
    outputs = set()
    for seed in ("0", "1", "2"):
        completed = subprocess.run(
            [sys.executable, "-c", script],
            env={**os.environ, "PYTHONHASHSEED": seed},
            capture_output=True,
            text=True,
            check=True,
        )
        outputs.add(completed.stdout)
    assert len(outputs) == 1


def test_a_signal_killed_executable_is_not_reported_as_a_model_error(tmp_path, monkeypatch) -> None:
    monkeypatch.setenv("COVERWISE_BINARY", str(stand_in_binary(tmp_path, "kill -SEGV $$")))

    with pytest.raises(RuntimeError) as excinfo:
        coverwise.generate(MODEL)

    assert not isinstance(excinfo.value, coverwise.CoverwiseError)
    assert not hasattr(excinfo.value, "code")
    assert not hasattr(excinfo.value, "exit_code")
    assert "SIGSEGV" in str(excinfo.value)


def test_an_undocumented_exit_status_is_not_reported_as_a_model_error(
    tmp_path, monkeypatch
) -> None:
    monkeypatch.setenv("COVERWISE_BINARY", str(stand_in_binary(tmp_path, "exit 42")))

    with pytest.raises(RuntimeError) as excinfo:
        coverwise.generate(MODEL)

    assert not isinstance(excinfo.value, coverwise.CoverwiseError)
    assert "42" in str(excinfo.value)


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


def test_analyze_coverage_surfaces_the_report_for_a_suite_it_rejects(tmp_path) -> None:
    """A rejected suite is still measured, and that measurement is the diagnosis."""

    parameters = {"os": ["win", "mac"], "browser": ["chrome", "safari"]}
    constraints = ["IF os = mac THEN browser != chrome"]
    tests = [
        {"os": "win", "browser": "chrome"},
        {"os": "mac", "browser": "chrome"},
        {"os": "mac", "browser": "safari"},
        {"os": "win", "browser": "safari"},
    ]

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.analyze_coverage(parameters, tests, constraints=constraints)

    params_payload = {
        "parameters": [{"name": name, "values": values} for name, values in parameters.items()],
        "constraints": constraints,
    }
    from_cli, cli_exit_code = cli_analyze(tmp_path, params_payload, tests)

    assert excinfo.value.report == from_cli
    assert excinfo.value.exit_code == cli_exit_code
    assert [item["testIndex"] for item in excinfo.value.report["invalidTests"]] == [1]
    assert "violates constraint" in excinfo.value.report["invalidTests"][0]["reason"]
    assert excinfo.value.report["coverageRatio"] == from_cli["coverageRatio"]


def test_analyze_coverage_reports_no_report_when_the_cli_wrote_none() -> None:
    """A model rejected before measurement has nothing to hand back."""

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.analyze_coverage(
            {"os": ["win", "mac"]},
            [{"os": "win"}],
            constraints=["IF os = = THEN"],
        )

    assert excinfo.value.report is None
    assert excinfo.value.code == "CONSTRAINT_ERROR"


# A side input travels through a temporary file this module creates, names on
# the command line, and deletes before returning. The CLI reports a read failure
# by interpolating the path it was given, so that message is the one place an
# internal path can reach a caller who never saw it. Refusing to read a document
# is the only failure that names the path, so it is what these drive; the size
# has to clear the CLI's whole-document guard, which sits far above every
# documented per-entity limit precisely so that ordinary input never meets it.
OVER_DOCUMENT_GUARD_ROWS = 1200
OVER_DOCUMENT_GUARD_PAD = "x" * 60_000


def test_an_oversized_tests_argument_is_named_as_an_argument() -> None:
    oversized = [
        {"os": "win", "browser": "chrome", "pad": OVER_DOCUMENT_GUARD_PAD}
        for _ in range(OVER_DOCUMENT_GUARD_ROWS)
    ]

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.analyze_coverage(
            {"os": ["win", "mac"], "browser": ["chrome", "safari"]}, oversized
        )

    # Pin the failure mode too: another rejection would not carry a path at all,
    # and the leak assertions below would then hold for the wrong reason.
    assert "exceeds the maximum" in str(excinfo.value)
    assert "the 'tests' argument" in str(excinfo.value)
    assert tempfile.gettempdir() not in str(excinfo.value)
    assert tempfile.gettempdir() not in excinfo.value.stderr
    assert "coverwise-" not in excinfo.value.stderr


def test_an_oversized_existing_argument_is_named_as_an_argument() -> None:
    oversized = [
        {"os": "win", "browser": "chrome", "theme": "light", "pad": OVER_DOCUMENT_GUARD_PAD}
    ] * OVER_DOCUMENT_GUARD_ROWS

    with pytest.raises(coverwise.CoverwiseError) as excinfo:
        coverwise.extend_tests(oversized, MODEL)

    assert "exceeds the maximum" in str(excinfo.value)
    assert "the 'existing' argument" in str(excinfo.value)
    assert tempfile.gettempdir() not in str(excinfo.value)
    assert tempfile.gettempdir() not in excinfo.value.stderr
    assert "coverwise-" not in excinfo.value.stderr


def test_a_suite_far_past_a_megabyte_of_json_is_measured() -> None:
    """What bounds a suite is the documented row count, not the bytes it takes.

    This suite is a couple of megabytes of JSON and well inside every published
    limit, and the JavaScript surfaces measure it without complaint. Python
    reaches the engine through a file, so it is the surface where a bound on the
    document rather than on the model would show up first.
    """

    parameters = {
        "os": ["win", "mac", "linux"],
        "browser": ["chrome", "firefox", "safari"],
        "theme": ["light", "dark"],
    }
    exhaustive = [
        {"os": os_value, "browser": browser, "theme": theme}
        for os_value in parameters["os"]
        for browser in parameters["browser"]
        for theme in parameters["theme"]
    ]
    suite = exhaustive * 2000
    assert len(json.dumps(suite)) > 1024 * 1024

    report = coverwise.analyze_coverage(parameters, suite)

    assert report["coverageRatio"] == 1.0
    assert report["invalidTests"] == []


def test_extend_tests_keeps_existing_tests_first() -> None:
    existing = [{"os": "win", "browser": "chrome", "theme": "light"}]

    result = coverwise.extend_tests(existing, MODEL)

    assert result["tests"][0] == existing[0]
    assert len(result["tests"]) > 1
    assert result["coverage"] == 1.0


def test_a_failed_extend_leaves_no_temporary_file_behind(monkeypatch) -> None:
    created: list[str] = []
    real_mkstemp = tempfile.mkstemp

    def recording_mkstemp(*args, **kwargs):
        handle, path = real_mkstemp(*args, **kwargs)
        created.append(path)
        return handle, path

    monkeypatch.setattr(coverwise.api.tempfile, "mkstemp", recording_mkstemp)

    # A row whose member is not a scalar has no value to record, drifted or
    # otherwise, so it is refused rather than carried through — unlike a row that
    # merely names a value the model has since dropped, which extend keeps.
    malformed = [{"os": ["win"], "browser": "chrome", "theme": "light"}]

    with pytest.raises(coverwise.CoverwiseError):
        coverwise.extend_tests(malformed, MODEL)

    assert created
    assert not any(os.path.exists(path) for path in created)


def test_extend_keeps_a_row_naming_a_value_the_model_no_longer_has() -> None:
    """Filling a gap in the model is what extend is for, so a drifted row stays.

    The row is kept exactly as written and reported as excluded from coverage,
    which is what the CLI, the WASM package and the pure-TypeScript package all
    do with it.
    """

    drifted = {"os": "nope", "browser": "chrome", "theme": "light"}

    result = coverwise.extend_tests([drifted], MODEL)

    assert result["tests"][0] == drifted
    warnings = "\n".join(result["warnings"])
    assert "Existing test 0 preserved but excluded from coverage" in warnings
    # The warning is the caller's only account of why the row was left out, so
    # it names the value they wrote. The unassigned sentinel is an
    # implementation detail of the index vector and tells them nothing.
    assert "value 'nope' is not declared by parameter os" in warnings
    assert "4294967295" not in warnings
    assert result["coverage"] == 1.0


def test_estimate_model_reports_the_tuple_budget() -> None:
    stats = coverwise.estimate_model(MODEL)

    assert stats["parameterCount"] == 3
    assert stats["totalValues"] == 8
    assert stats["totalTuples"] == 3 * 3 + 3 * 2 + 3 * 2
    assert [p["name"] for p in stats["parameters"]] == ["os", "browser", "theme"]


def test_estimate_model_rejects_an_unknown_constraint_parameter() -> None:
    """A preflight classifies a bad constraint the way generation does."""

    constraints = ["IF missing = 1 THEN os = win"]

    with pytest.raises(coverwise.CoverwiseError) as estimated:
        coverwise.estimate_model(MODEL, constraints=constraints)
    with pytest.raises(coverwise.CoverwiseError) as generated:
        coverwise.generate(MODEL, constraints=constraints)

    assert estimated.value.code == "CONSTRAINT_ERROR"
    assert estimated.value.code == generated.value.code
    assert estimated.value.exit_code == generated.value.exit_code


def test_generate_reports_negative_coverage_that_accounts_for_every_tuple() -> None:
    """The three counts describe one universe, so covered plus omitted is the total."""

    result = coverwise.generate(
        parameters=[
            {"name": "A", "values": ["a0", {"value": "bad", "invalid": True}]},
            {"name": "B", "values": ["b0", "b1", "b2"]},
            {"name": "C", "values": ["c0", "c1", "c2"]},
            {"name": "D", "values": ["d0", "d1", "d2"]},
        ],
        strength=4,
        seed=42,
    )

    negative = result["negativeCoverage"]
    assert negative["totalTuples"] > 0
    assert negative["coveredTuples"] + negative["omittedTuples"] == negative["totalTuples"]
    assert negative["omittedTuples"] == 0
    assert negative["coverageRatio"] == 1


def test_a_truncated_negative_suite_reports_the_tuples_it_omitted() -> None:
    """maxTests stops negative generation part way, and the omitted count says so."""

    result = coverwise.generate(
        parameters=[
            {"name": "A", "values": ["a0", {"value": "bad", "invalid": True}]},
            {"name": "B", "values": ["b0", "b1"]},
        ],
        strength=2,
        maxTests=3,
        seed=42,
    )

    negative = result["negativeCoverage"]
    assert negative["omittedTuples"] > 0
    assert negative["coveredTuples"] + negative["omittedTuples"] == negative["totalTuples"]
    assert negative["coverageRatio"] == negative["coveredTuples"] / negative["totalTuples"]
