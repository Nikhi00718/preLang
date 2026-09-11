"""
pipeline.py
===========

Drives the **complete** compiler and collects every artefact it produces.

    source
      -> lexer             -> tokens
      -> parser            -> AST
      -> semantic analyser -> symbol table + inferred schema + semantic facts
      -> contract checker  -> contract report
      -> IR generator      -> IR
      -> optimiser         -> optimised IR + optimisation report
      -> code generator    -> Python/Pandas source
      -> executor          -> processed dataset            (optional)

A phase is skipped when an earlier one has failed, exactly as a real compiler
does: there is no point type-checking a program that does not parse. Whatever
*was* produced is kept, so the visualiser can still show the token stream of a
program that fails to parse.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from pathlib import Path

from .ast_nodes import ProgramNode
from .code_generator import GeneratedCode, generate_code
from .contracts import ContractReport, validate_contracts
from .errors import ErrorReporter
from .executor import ExecutionResult, execute
from .ir import IRProgram
from .ir_generator import generate_ir
from .lexer import Lexer
from .optimizer import OptimizationReport, optimize
from .parser import Parser
from .semantic_analyzer import AnalysisResult, SemanticAnalyzer
from .symbol_table import SymbolTable
from .tokens import Token, TokenType


@dataclass
class CompilationResult:
    """Every artefact produced by one compilation."""

    source: str
    reporter: ErrorReporter
    base_dir: Path = field(default_factory=Path.cwd)
    tokens: list[Token] = field(default_factory=list)
    ast: ProgramNode | None = None
    analysis: AnalysisResult | None = None
    contract_report: ContractReport | None = None
    ir: IRProgram | None = None
    optimised_ir: IRProgram | None = None
    optimisation: OptimizationReport | None = None
    code: GeneratedCode | None = None
    unoptimised_code: GeneratedCode | None = None
    execution: ExecutionResult | None = None
    timings: dict[str, float] = field(default_factory=dict)
    reached_phase: str = "lexical analysis"
    failed_phase: str | None = None

    # -- status ------------------------------------------------------------
    @property
    def ok(self) -> bool:
        """True when no diagnostic and no failed contract was produced."""
        contracts_ok = self.contract_report is None or self.contract_report.ok
        return self.reporter.ok and contracts_ok

    @property
    def compiled(self) -> bool:
        """True when target code was produced (contracts may still have failed)."""
        return self.code is not None

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
        if self.ir is not None and self.optimised_ir is not None:
            parts.append(f"IR {len(self.ir)} -> {len(self.optimised_ir)}")
        if self.optimisation is not None and self.optimisation.pruned_columns:
            parts.append(f"{len(self.optimisation.pruned_columns)} columns pruned")
        parts.append(self.reporter.summary())
        if self.contract_report is not None and self.contract_report.results:
            parts.append(
                f"{len(self.contract_report.passed)}/{len(self.contract_report.results)} contracts passed"
            )
        return ", ".join(parts)

    def format_timings(self) -> str:
        total = sum(self.timings.values())
        lines = [f"{'PHASE':<30}{'TIME (ms)':>12}", "-" * 42]
        for phase, seconds in self.timings.items():
            lines.append(f"{phase:<30}{seconds * 1000:>12.3f}")
        lines.append("-" * 42)
        lines.append(f"{'TOTAL':<30}{total * 1000:>12.3f}")
        return "\n".join(lines)


def _note_failure(result: CompilationResult, phase: str) -> None:
    """Remember the first phase that reported an error."""
    if result.failed_phase is None and not result.reporter.ok:
        result.failed_phase = phase


def compile_source(
    source: str,
    base_dir: str | Path | None = None,
    validate_data: bool = True,
    optimise: bool = True,
    run: bool = False,
) -> CompilationResult:
    """Compile *source*, optionally executing the generated program."""
    reporter = ErrorReporter(source)
    directory = Path(base_dir) if base_dir else Path.cwd()
    result = CompilationResult(source=source, reporter=reporter, base_dir=directory)

    # -- Phase 1: lexical analysis --------------------------------------
    start = time.perf_counter()
    result.tokens = Lexer(source, reporter).tokenize()
    result.timings["1. lexical analysis"] = time.perf_counter() - start
    _note_failure(result, "lexical analysis")

    # -- Phase 2: syntax analysis ---------------------------------------
    result.reached_phase = "syntax analysis"
    start = time.perf_counter()
    result.ast = Parser(result.tokens, reporter).parse()
    result.timings["2. syntax analysis"] = time.perf_counter() - start
    _note_failure(result, "syntax analysis")
    if not reporter.ok:
        return result

    # -- Phase 3: semantic analysis -------------------------------------
    result.reached_phase = "semantic analysis"
    start = time.perf_counter()
    result.analysis = SemanticAnalyzer(reporter, directory).analyse(result.ast)
    result.timings["3. semantic analysis"] = time.perf_counter() - start
    _note_failure(result, "semantic analysis")

    # -- Data contract validation ---------------------------------------
    if validate_data and result.analysis is not None:
        result.reached_phase = "data contract validation"
        start = time.perf_counter()
        result.contract_report = validate_contracts(
            result.analysis.contracts, result.analysis.symbols, directory
        )
        result.timings["4. contract validation"] = time.perf_counter() - start

    if not reporter.ok:
        return result

    # -- Phase 4: intermediate code generation --------------------------
    result.reached_phase = "intermediate code generation"
    start = time.perf_counter()
    result.ir = generate_ir(result.ast)
    result.timings["5. IR generation"] = time.perf_counter() - start

    # -- Phase 5: optimisation ------------------------------------------
    result.reached_phase = "optimisation"
    start = time.perf_counter()
    if optimise:
        result.optimised_ir, result.optimisation = optimize(
            result.ir, result.analysis.symbols, reporter
        )
    else:
        # still run the analysis: the liveness annotation is what makes the
        # generated dropna(subset=...) correct, optimised or not
        from .optimizer import analyse_liveness

        analyse_liveness(result.ir, result.analysis.symbols)
        for instruction in result.ir.instructions:
            instruction.meta.pop("usecols", None)  # read every column from disk
        result.optimised_ir = result.ir
    result.timings["6. optimisation"] = time.perf_counter() - start

    # -- Phase 6: target code generation --------------------------------
    result.reached_phase = "target code generation"
    start = time.perf_counter()
    result.code = generate_code(result.optimised_ir, optimised=optimise)
    result.timings["7. code generation"] = time.perf_counter() - start

    # -- Execution -------------------------------------------------------
    if run:
        result.reached_phase = "execution"
        start = time.perf_counter()
        result.execution = execute(
            result.code.source, directory, output_files=result.code.output_files
        )
        result.timings["8. execution"] = time.perf_counter() - start

    return result


def compile_file(
    path: str | Path,
    validate_data: bool = True,
    optimise: bool = True,
    run: bool = False,
) -> CompilationResult:
    """Compile the PrepLang program stored in *path*.

    Relative dataset paths inside the program are resolved against the
    directory containing the program file.
    """
    file_path = Path(path)
    return compile_source(
        file_path.read_text(encoding="utf-8"),
        base_dir=file_path.parent,
        validate_data=validate_data,
        optimise=optimise,
        run=run,
    )


def generate_unoptimised(result: CompilationResult) -> GeneratedCode | None:
    """Regenerate the target code with optimisation disabled.

    Used by the benchmark and by the IDE's before/after comparison.
    """
    if result.ir is None or result.analysis is None:
        return None
    from .optimizer import analyse_liveness

    baseline = result.ir.copy()
    analyse_liveness(baseline, result.analysis.symbols)
    for instruction in baseline.instructions:
        instruction.meta.pop("usecols", None)  # read every column from disk
    return generate_code(baseline, optimised=False)
