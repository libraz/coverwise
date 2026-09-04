"""Combinatorial test generation, backed by the bundled native coverwise CLI."""

from .api import CoverwiseError, analyze_coverage, estimate_model, extend_tests, generate
from .cli import native_binary, run
from .pytest_helpers import parametrize

__all__ = [
    "CoverwiseError",
    "analyze_coverage",
    "estimate_model",
    "extend_tests",
    "generate",
    "native_binary",
    "parametrize",
    "run",
]
__version__ = "1.6.0"
