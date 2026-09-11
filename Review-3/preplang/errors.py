"""
errors.py
=========

Structured diagnostics for every phase of the PrepLang compiler.

Design goal (see the project report, "Error Message Philosophy"): an error
should always answer six questions.

    1. What failed      -> ``title``
    2. Where it failed   -> ``line`` / ``column``
    3. Why it failed     -> ``problem``
    4. What was expected -> ``expected``
    5. What was found    -> ``found``
    6. How to fix it     -> ``hint`` / ``suggestions``

Every phase raises or records subclasses of :class:`CompilerError`, so the
front-end (CLI or IDE) can render all diagnostics uniformly.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable, Sequence


class ErrorPhase:
    """Names of the compiler phases that can report a diagnostic."""

    LEXICAL = "LEXICAL ERROR"
    SYNTAX = "SYNTAX ERROR"
    SEMANTIC = "SEMANTIC ERROR"
    TYPE = "TYPE ERROR"
    SCHEMA = "SCHEMA ERROR"
    CONTRACT = "DATA CONTRACT ERROR"
    RUNTIME = "RUNTIME ERROR"


@dataclass
class CompilerError(Exception):
    """Base class for all PrepLang diagnostics.

    The object is both an exception (so a phase may abort) and a plain record
    (so a phase may *collect* several and keep going -- error recovery).
    """

    problem: str
    line: int | None = None
    column: int | None = None
    expected: str | None = None
    found: str | None = None
    hint: str | None = None
    suggestions: Sequence[str] = field(default_factory=tuple)
    source_line: str | None = None

    #: Overridden by each subclass.
    phase: str = ErrorPhase.SEMANTIC

    def __post_init__(self) -> None:
        Exception.__init__(self, self.problem)

    # -- rendering ---------------------------------------------------------
    def header(self) -> str:
        if self.line is None:
            return self.phase
        return f"{self.phase} - Line {self.line}"

    def format(self) -> str:
        """Render the diagnostic as a multi-line, human-readable block."""
        out: list[str] = [self.header(), ""]
        out.append(f"Problem:\n    {self.problem}")

        if self.source_line is not None:
            out.append("")
            out.append(f"Source:\n    {self.source_line.strip()}")
            if self.column is not None:
                indent = len(self.source_line) - len(self.source_line.lstrip())
                offset = max(0, self.column - 1 - indent)
                out.append(" " * (4 + offset) + "^")

        if self.expected is not None:
            out.append("")
            out.append(f"Expected:\n    {self.expected}")
        if self.found is not None:
            out.append("")
            out.append(f"Found:\n    {self.found}")
        if self.suggestions:
            out.append("")
            out.append("Did you mean:")
            out.extend(f"    {s}" for s in self.suggestions)
        if self.hint:
            out.append("")
            out.append(f"Hint:\n    {self.hint}")

        return "\n".join(out)

    def one_line(self) -> str:
        """Compact single-line rendering, useful for lists and test output."""
        where = f"line {self.line}" if self.line is not None else "-"
        return f"[{self.phase}] {where}: {self.problem}"

    def __str__(self) -> str:  # pragma: no cover - delegation
        return self.format()


# ---------------------------------------------------------------------------
# Phase specific error classes
# ---------------------------------------------------------------------------


@dataclass
class LexicalError(CompilerError):
    phase: str = ErrorPhase.LEXICAL


@dataclass
class SyntaxError_(CompilerError):
    """Syntax error.  Named with a trailing underscore to avoid shadowing the
    Python builtin ``SyntaxError`` inside this package."""

    phase: str = ErrorPhase.SYNTAX


@dataclass
class SemanticError(CompilerError):
    phase: str = ErrorPhase.SEMANTIC


@dataclass
class TypeError_(CompilerError):
    """Datatype mismatch detected by the semantic analyser."""

    phase: str = ErrorPhase.TYPE


@dataclass
class SchemaError(CompilerError):
    phase: str = ErrorPhase.SCHEMA


@dataclass
class ContractError(CompilerError):
    phase: str = ErrorPhase.CONTRACT


@dataclass
class RuntimeError_(CompilerError):
    phase: str = ErrorPhase.RUNTIME


@dataclass
class CompilerWarning:
    """Non fatal diagnostic (optimiser hints, dead operations, ...)."""

    message: str
    line: int | None = None
    kind: str = "WARNING"

    def one_line(self) -> str:
        where = f"line {self.line}" if self.line is not None else "-"
        return f"[{self.kind}] {where}: {self.message}"

    def format(self) -> str:
        head = f"{self.kind}" + (f" - Line {self.line}" if self.line else "")
        return f"{head}\n\n    {self.message}"


class ErrorReporter:
    """Collects diagnostics so a phase can continue after a recoverable error.

    Using a collector rather than raising on the first problem is what allows
    PrepLang to report *several* errors from one compilation, which is the
    behaviour expected from a real compiler.
    """

    def __init__(self, source: str | None = None) -> None:
        self.errors: list[CompilerError] = []
        self.warnings: list[CompilerWarning] = []
        self._source_lines: list[str] = source.splitlines() if source else []

    # -- recording ---------------------------------------------------------
    def report(self, error: CompilerError) -> CompilerError:
        """Record *error* and return it (so callers may also raise it)."""
        if error.source_line is None and error.line is not None:
            error.source_line = self.source_line(error.line)
        self.errors.append(error)
        return error

    def warn(self, message: str, line: int | None = None, kind: str = "WARNING") -> CompilerWarning:
        warning = CompilerWarning(message=message, line=line, kind=kind)
        self.warnings.append(warning)
        return warning

    def extend(self, errors: Iterable[CompilerError]) -> None:
        for error in errors:
            self.report(error)

    # -- queries -----------------------------------------------------------
    @property
    def ok(self) -> bool:
        return not self.errors

    def __len__(self) -> int:
        return len(self.errors)

    def __bool__(self) -> bool:
        # A reporter object is always "present"; without this, an empty
        # reporter would be falsy (because __len__ == 0) and idioms such as
        # ``reporter or ErrorReporter()`` would silently drop it.
        return True

    def source_line(self, line: int) -> str | None:
        if 1 <= line <= len(self._source_lines):
            return self._source_lines[line - 1]
        return None

    def set_source(self, source: str) -> None:
        self._source_lines = source.splitlines()

    # -- rendering ---------------------------------------------------------
    def render(self) -> str:
        blocks = [e.format() for e in self.errors]
        blocks += [w.format() for w in self.warnings]
        return ("\n\n" + "-" * 60 + "\n\n").join(blocks)

    def summary(self) -> str:
        return (
            f"{len(self.errors)} error(s), {len(self.warnings)} warning(s)"
            if (self.errors or self.warnings)
            else "no errors"
        )


def closest_names(target: str, candidates: Iterable[str], limit: int = 3) -> list[str]:
    """Return the *candidates* most similar to *target* (for "did you mean").

    Uses :mod:`difflib`, which is a light-weight stand-in for the edit-distance
    based suggestion engines found in production compilers.
    """
    import difflib

    return difflib.get_close_matches(target, list(candidates), n=limit, cutoff=0.4)
