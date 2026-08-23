from __future__ import annotations

import re
import subprocess
import sys
import zipfile
from importlib import metadata
from pathlib import Path

import pytest

import coverwise

PACKAGE_DIR = Path(coverwise.__file__).parent
PROJECT_DIR = Path(__file__).resolve().parents[1]

# Each reveal_type call below prints the type a checker infers for the call
# above it, which is what the shipped annotations promise a consumer.
CONSUMER_SOURCE = """\
import coverwise

text_result = coverwise.run(["--help"], text=True, capture_output=True, check=True)
reveal_type(text_result.stdout)

default_result = coverwise.run(["--help"], capture_output=True)
reveal_type(default_result.stdout)

report = coverwise.generate(parameters={"a": ["1", "2"], "b": ["x", "y"]})
reveal_type(report)
"""


@pytest.fixture(scope="module")
def checked_types(tmp_path_factory) -> list[str]:
    """Type-check a consumer of the public API and return the revealed types."""

    pytest.importorskip("mypy", reason="the type checker is not installed in this environment")
    source = tmp_path_factory.mktemp("typing") / "consumer.py"
    source.write_text(CONSUMER_SOURCE, encoding="utf-8")

    completed = subprocess.run(
        [
            sys.executable,
            "-m",
            "mypy",
            "--no-incremental",
            "--config-file",
            str(PROJECT_DIR / "pyproject.toml"),
            str(source),
        ],
        capture_output=True,
        text=True,
        check=False,
    )

    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "library stubs" not in completed.stdout
    return re.findall(r'Revealed type is "(.+?)"', completed.stdout)


def test_package_ships_a_typing_marker() -> None:
    assert (PACKAGE_DIR / "py.typed").is_file()


def test_distribution_metadata_declares_inline_types() -> None:
    classifiers = metadata.metadata("coverwise").get_all("Classifier") or []

    assert "Typing :: Typed" in classifiers


def test_built_wheel_carries_the_typing_marker() -> None:
    wheels = sorted((PROJECT_DIR / "dist").glob("*.whl"))
    if not wheels:
        pytest.skip("no built wheel to inspect; run scripts/build_wheel.sh first")

    with zipfile.ZipFile(wheels[-1]) as wheel:
        names = wheel.namelist()
        metadata_path = next(name for name in names if name.endswith(".dist-info/METADATA"))
        wheel_metadata = wheel.read(metadata_path).decode("utf-8")

    assert "coverwise/py.typed" in names
    assert "Classifier: Typing :: Typed" in wheel_metadata


def test_a_type_checker_reads_the_shipped_annotations(checked_types) -> None:
    """Without the marker every call would be Any, whatever the annotations say."""

    assert checked_types[2] == "dict[str, Any]"


def test_a_text_run_is_declared_to_capture_str(checked_types) -> None:
    assert checked_types[0] == "str"


def test_a_default_run_is_not_declared_to_capture_str(checked_types) -> None:
    """The default call decodes nothing, so the declaration must not promise str."""

    assert checked_types[1] != "str"


def test_run_captures_str_only_when_text_is_requested() -> None:
    as_text = coverwise.run(["--help"], text=True, capture_output=True, check=True)
    as_bytes = coverwise.run(["--help"], capture_output=True, check=True)

    assert isinstance(as_text.stderr, str)
    assert isinstance(as_bytes.stderr, bytes)
