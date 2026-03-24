"""Generate benchmark input files for coverwise performance profiling."""

import json
import sys
from pathlib import Path


def make_params(n_params: int, n_values: int) -> list[dict]:
    return [
        {"name": f"p{i}", "values": [f"v{j}" for j in range(n_values)]}
        for i in range(n_params)
    ]


def make_input(n_params: int, n_values: int, strength: int = 2, seed: int = 1) -> dict:
    return {
        "parameters": make_params(n_params, n_values),
        "strength": strength,
        "seed": seed,
    }


def main():
    out_dir = Path(__file__).parent / "data"
    out_dir.mkdir(exist_ok=True)

    cases = {
        # name: (params, values, strength)
        "small_2wise": (10, 3, 2),
        "medium_2wise": (20, 5, 2),
        "large_2wise": (30, 5, 2),
        "small_3wise": (10, 3, 3),
        "medium_3wise": (15, 3, 3),
        "small_4wise": (8, 3, 4),
        "many_values": (5, 20, 2),
        "many_params": (50, 3, 2),
    }

    for name, (p, v, t) in cases.items():
        data = make_input(p, v, t)
        path = out_dir / f"{name}.json"
        path.write_text(json.dumps(data, indent=2) + "\n")
        print(f"{name}: {p} params × {v} values, {t}-wise → {path}")

    print(f"\n{len(cases)} benchmark files written to {out_dir}")


if __name__ == "__main__":
    main()
