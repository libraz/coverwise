from __future__ import annotations

import pytest

import coverwise
from coverwise.pytest_helpers import _decoder

NUMERIC_ALIASES = ["001", "nan", "Infinity", "1e100"]


@pytest.mark.parametrize("alias", NUMERIC_ALIASES)
def test_numeric_looking_aliases_keep_their_spelling(alias: str) -> None:
    decode = _decoder(
        {
            "name": "attempt",
            "values": [{"value": 1, "aliases": [alias]}],
            "type": "integer",
        }
    )

    decoded = decode(alias)

    assert decoded == alias
    assert type(decoded) is str


@pytest.mark.parametrize(
    ("kind", "seed", "alias"),
    [("integer", 1, "Infinity"), ("integer", 8, "001"), ("float", 0, "1e100")],
)
def test_parametrize_preserves_boundary_aliases_and_decodes_numbers(
    kind: str, seed: int, alias: str
) -> None:
    parameter = {
        "name": "attempt",
        "values": [{"value": 1, "aliases": NUMERIC_ALIASES}],
        "type": kind,
        "range": [0, 2],
    }
    if kind == "float":
        parameter["step"] = 0.5

    mark = coverwise.parametrize([parameter], strength=1, seed=seed)
    values = mark.mark.args[1]

    assert alias in values
    assert type(next(value for value in values if value == alias)) is str
    numeric_values = [value for value in values if value != alias]
    assert numeric_values
    assert all(type(value) is (int if kind == "integer" else float) for value in numeric_values)
