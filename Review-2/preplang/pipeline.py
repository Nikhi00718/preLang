"""
pipeline.py
===========

Drives the compiler front end and collects every artefact it produces.

    source
      -> lexer            -> tokens
      -> parser           -> AST
      -> semantic analyser-> symbol table + schema + semantic facts
      -> contract checker -> contract report
      -> IR generator     -> IR

A phase is skipped when an earlier phase has already failed, exactly as a real
compiler does: there is no point type-checking a program that does not parse.
The result object keeps whatever *was* produced, so the visualiser can still
show the token stream of a program that fails to parse.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path

from .ast_nodes import ProgramNode
from .contracts import ContractReport, validate_contracts
from .errors import ErrorReporter
from .ir import IRProgram
from .ir_generator import generate_ir
from .lexer import Lexer
from .parser import Parser
from .semantic_analyzer import AnalysisResult, SemanticAnalyzer
from .symbol_table import SymbolTable
from .tokens import Token, TokenType


@dataclass
class CompilationResult:
    """Every artefact produced by one compilation."""

    source: str
    reporter: ErrorReporter
    tokens: list[Token] = field(default_factory=list)
    ast: ProgramNode | None = None
    analysis: AnalysisResult | None = None
    contract_report: ContractReport | None = None
    ir: IRProgram | None = None
    timings: dict[str, float] = field(default_factory=dict)
    reached_phase: str = "lexical analysis"
    #: the first phase that produced an error, or None when none did
    failed_phase: str | None = None

    # -- status ------------------------------------------------------------
    @property
    def ok(self) -> bool:
        """True when no diagnostic and no failed contract was produced."""
        contracts_ok = self.contract_report is None or self.contract_report.ok
        return self.reporter.ok and contracts_ok

    @property
    def symbols(self) -> SymbolTable | None:
        return self.analysis.symbols if self.analysis else None

    @property
    def token_count(self) -> int:
        return len([t for t in self.tokens if t.type is not TokenType.EOF])

    def summary(self) -> str:
        parts = [
            f"{self.token_count} tokens",
            f"{len(self.ast.statements) if self.ast else 0} statements",
        ]
        if self.symbols is not None:
            parts.append(f"{len(self.symbols)} dataset(s)")
        if self.ir is not None:
            parts.append(f"{len(self.ir)} IR instructions")
        parts.append(self.reporter.summary())
        if self.contract_report is not None and self.contract_report.results:
            parts.append(
                f"{len(self.contract_report.passed)}/{len(self.contract_report.results)} contracts passed"
            )
        return ", ".join(parts)

    def format_timings(self) -> str:
        total = sum(self.timings.values())
        lines = [f"{'PHASE':<28}{'TIME (ms)':>12}"]
        lines.append("-" * 40)
        for phase, seconds in self.timings.items():
            lines.append(f"{phase:<28}{seconds * 1000:>12.3f}")
        lines.append("-" * 40)
        lines.append(f"{'TOTAL':<28}{total * 1000:>12.3f}")
        return "\n".join(lines)


def _note_failure(result: CompilationResult, phase: str) -> None:
    """Remember the first phase that reported an error."""
    if result.failed_phase is None and not result.reporter.ok:
        result.failed_phase = phase


def compile_source(
    source: str,
    base_dir: str | Path | None = None,
    validate_data: bool = True,
) -> CompilationResult:
    """Run the whole front end over *source* and return every artefact."""
    reporter = ErrorReporter(source)
    result = CompilationResult(source=source, reporter=reporter)
    base_dir = Path(base_dir) if base_dir else Path.cwd()

    # -- Phase 1: lexical analysis -------------------------------------
    start = time.perf_counter()
    result.tokens = Lexer(source, reporter).tokenize()
    result.timings["1. lexical analysis"] = time.perf_counter() - start
    _note_failure(result, "lexical analysis")

    # -- Phase 2: syntax analysis --------------------------------------
    result.reached_phase = "syntax analysis"
    start = time.perf_counter()
    result.ast = Parser(result.tokens, reporter).parse()
    result.timings["2. syntax analysis"] = time.perf_counter() - start
    _note_failure(result, "syntax analysis")

    if not reporter.ok:
        # Lexical or syntax errors: analysing a malformed tree would only
        # produce cascading, misleading diagnostics.
        return result

    # -- Phase 3: semantic analysis ------------------------------------
    result.reached_phase = "semantic analysis"
    start = time.perf_counter()
    analyser = SemanticAnalyzer(reporter, base_dir)
    result.analysis = analyser.analyse(result.ast)
    result.timings["3. semantic analysis"] = time.perf_counter() - start
    _note_failure(result, "semantic analysis")

    # -- Data contract validation --------------------------------------
    if validate_data and result.analysis is not None:
        result.reached_phase = "data contract validation"
        start = time.perf_counter()
        result.contract_report = validate_contracts(
            result.analysis.contracts, result.analysis.symbols, base_dir
        )
        result.timings["4. contract validation"] = time.perf_counter() - start

    if not reporter.ok:
        return result

    # -- Phase 4: intermediate code generation -------------------------
    result.reached_phase = "intermediate code generation"
    start = time.perf_counter()
    result.ir = generate_ir(result.ast)
    result.timings["5. IR generation"] = time.perf_counter() - start

    return result


def compile_file(path: str | Path, validate_data: bool = True) -> CompilationResult:
    """Compile the PrepLang program stored in *path*.

    Relative dataset paths inside the program are resolved against the
    directory containing the program file.
    """
    file_path = Path(path)
    source = file_path.read_text(encoding="utf-8")
    return compile_source(source, base_dir=file_path.parent, validate_data=validate_data)
