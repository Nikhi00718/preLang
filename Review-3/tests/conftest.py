"""Shared pytest fixtures and helpers for the PrepLang test suite."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"

# make the `preplang` package importable when pytest is run from anywhere
sys.path.insert(0, str(ROOT))

from preplang.pipeline import CompilationResult, compile_source  # noqa: E402


def compile_demo(
    source: str,
    validate_data: bool = True,
    optimise: bool = True,
    run: bool = False,
) -> CompilationResult:
    """Compile *source* with the bundled example datasets available."""
    return compile_source(
        source,
        base_dir=EXAMPLES,
        validate_data=validate_data,
        optimise=optimise,
        run=run,
    )


def load(dataset: str = "students.csv", name: str = "students") -> str:
    return f'LOAD "{dataset}" AS {name};\n'


def errors_of(result: CompilationResult) -> list[str]:
    return [e.problem for e in result.reporter.errors]


def phases_of(result: CompilationResult) -> list[str]:
    return [e.phase for e in result.reporter.errors]
