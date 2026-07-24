#!/usr/bin/env python3
"""Ensure the npm and PyPI manifests publish the same release version."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PACKAGE_VERSION = json.loads((ROOT / "package.json").read_text())["version"]
PYPROJECT = (ROOT / "bindings" / "python" / "pyproject.toml").read_text()
match = re.search(r'^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"$', PYPROJECT, re.MULTILINE)

if match is None or match.group(1) != PACKAGE_VERSION:
    print(
        f"Version mismatch: package.json={PACKAGE_VERSION}, "
        f"bindings/python/pyproject.toml={match.group(1) if match else 'missing'}",
        file=sys.stderr,
    )
    raise SystemExit(1)
