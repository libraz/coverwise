#!/usr/bin/env python3
"""Ensure the npm and PyPI manifests publish the same release version."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PACKAGE_VERSION = json.loads((ROOT / "package.json").read_text())["version"]


def read_version(relative_path: str, pattern: str) -> str:
    """Extract the single semantic version a release manifest declares."""

    match = re.search(pattern, (ROOT / relative_path).read_text(), re.MULTILINE)
    return match.group(1) if match else "missing"


# __version__ is what installed users see, so it has to move with the manifests.
declared = {
    "bindings/python/pyproject.toml": read_version(
        "bindings/python/pyproject.toml", r'^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"$'
    ),
    "bindings/python/src/coverwise/__init__.py": read_version(
        "bindings/python/src/coverwise/__init__.py",
        r'^__version__\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"$',
    ),
}

mismatched = {path: found for path, found in declared.items() if found != PACKAGE_VERSION}
if mismatched:
    details = ", ".join(f"{path}={found}" for path, found in mismatched.items())
    print(f"Version mismatch: package.json={PACKAGE_VERSION}, {details}", file=sys.stderr)
    raise SystemExit(1)
