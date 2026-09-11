"""
main.py -- PrepLang Review 3 command line driver.

Runs the complete compiler and prints every artefact of every phase:

    source -> tokens -> AST -> symbol table + schema -> semantic facts
           -> contracts -> IR -> optimised IR + optimisation report
           -> generated Python/Pandas -> execution

Usage
-----
    python main.py examples/demo_valid.prep              # compile and show every stage
    python main.py examples/demo_valid.prep --run        # also execute the result
    python main.py examples/demo_wide_dataset.prep --stages opt,code
    python main.py examples/demo_valid.prep --no-optimise --stages code
    python main.py examples/demo_valid.prep --emit out/pipeline.py
    python main.py --demo
    python main.py -c 'LOAD "students.csv" AS s; NORMALIZE s.name;'
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from preplang.ast_nodes import format_ast
from preplang.lexer import format_token_table
from preplang.pipeline import CompilationResult, compile_source

BASE_DIR = Path(__file__).resolve().parent
EXAMPLES = BASE_DIR / "examples"
WIDTH = 74

ALL_STAGES = (
    "source", "tokens", "ast", "symbols", "semantic", "contracts",
    "ir", "opt", "code", "run", "timing",
)


def banner(title: str) -> None:
    print(f"\n{'=' * WIDTH}\n  {title}\n{'=' * WIDTH}")


def section(title: str) -> None:
    print(f"\n--- {title} " + "-" * max(3, WIDTH - len(title) - 5))


def show(result: CompilationResult, stages: set[str], name: str) -> None:
    banner(f"PrepLang Compiler v1.0  --  {name}")

    if "source" in stages:
        section("SOURCE PROGRAM")
        for number, text in enumerate(result.source.splitlines(), start=1):
            print(f"{number:>3} | {text}")

    if "tokens" in stages:
        section("PHASE 1: LEXICAL ANALYSIS (TOKEN STREAM)")
        print(format_token_table(result.tokens, show_eof=False))

    if "ast" in stages and result.ast is not None:
        section("PHASE 2: SYNTAX ANALYSIS (ABSTRACT SYNTAX TREE)")
        print(format_ast(result.ast))

    if "symbols" in stages and result.symbols is not None:
        section("SYMBOL TABLE AND INFERRED DATASET SCHEMA")
        print(result.symbols.format())

    if "semantic" in stages and result.analysis is not None:
        section("PHASE 3: SEMANTIC ANALYSIS (CHECKS PERFORMED)")
        print(result.analysis.format_facts())

    if "contracts" in stages and result.contract_report is not None:
        section("DATA CONTRACT VALIDATION")
        print(result.contract_report.format())

    if "ir" in stages and result.ir is not None:
        section("PHASE 4: INTERMEDIATE REPRESENTATION")
        print(result.ir.format())

    if "opt" in stages and result.optimised_ir is not None:
        section("PHASE 5: OPTIMISED INTERMEDIATE REPRESENTATION")
        print(result.optimised_ir.format("OPTIMISED INTERMEDIATE REPRESENTATION"))
        if result.optimisation is not None:
            print()
            print(result.optimisation.format())

    if "code" in stages and result.code is not None:
        section("PHASE 6: GENERATED PYTHON / PANDAS CODE")
        print(result.code.source)

    if "run" in stages and result.execution is not None:
        section("EXECUTION")
        print(result.execution.format())
        for dataset, preview in result.execution.previews.items():
            print(f"\nFinal dataset '{dataset}' (first rows):")
            print(preview)

    section("DIAGNOSTICS")
    if result.reporter.errors or result.reporter.warnings:
        print(result.reporter.render())
        print()
    if result.ok:
        print("OK  Compilation succeeded.")
    elif not result.reporter.ok:
        print(f"FAILED  Rejected during {result.failed_phase or result.reached_phase}.")
    else:
        failed = len(result.contract_report.failed) if result.contract_report else 0
        print(
            f"REJECTED  The program compiles, but {failed} data contract(s) "
            f"are violated by the dataset."
        )
    print(f"    {result.summary()}")

    if "timing" in stages:
        section("PHASE TIMINGS")
        print(result.format_timings())


def parse_stages(text: str | None, run: bool) -> set[str]:
    if not text or text == "all":
        stages = set(ALL_STAGES)
    else:
        requested = {s.strip() for s in text.split(",") if s.strip()}
        unknown = requested - set(ALL_STAGES)
        if unknown:
            raise SystemExit(
                f"Unknown stage(s): {', '.join(sorted(unknown))}. "
                f"Available: {', '.join(ALL_STAGES)}"
            )
        stages = requested | {"source"}
    if run:
        stages.add("run")
    return stages


#: The bundled examples and whether each is expected to compile cleanly.
#: Keeping this explicit, rather than guessing from the file name, means
#: --demo actually verifies the documented behaviour of every example.
EXPECTED = {
    "demo_valid.prep": True,
    "demo_all_statements.prep": True,
    "demo_wide_dataset.prep": True,
    "demo_lexical_error.prep": False,
    "demo_syntax_error.prep": False,
    "demo_type_error.prep": False,
    "demo_dropped_column.prep": False,
    "demo_dirty_schema.prep": False,
    "demo_contract_violation.prep": False,
}


def run_demo(stages: set[str], run: bool) -> int:
    examples = sorted(EXAMPLES.glob("*.prep"))
    failures = 0
    for example in examples:
        result = compile_source(
            example.read_text(encoding="utf-8"), base_dir=EXAMPLES, run=run
        )
        show(result, stages, example.name)
        expected = EXPECTED.get(example.name)
        if expected is not None and result.ok != expected:
            failures += 1
            print(
                f"\n!! {example.name}: expected it to "
                f"{'compile cleanly' if expected else 'be rejected'}, but it did not"
            )
    banner("DEMO SUMMARY")
    print(f"{len(examples)} example programs compiled, {failures} unexpected result(s).")
    return 0 if failures == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="preplang",
        description="PrepLang: a schema-aware DSL compiler for data preprocessing.",
    )
    parser.add_argument("file", nargs="?", help="PrepLang source file (.prep)")
    parser.add_argument("-c", "--command", help="compile a source string")
    parser.add_argument("--demo", action="store_true", help="compile every bundled example")
    parser.add_argument("--run", action="store_true", help="execute the generated code")
    parser.add_argument("--no-optimise", action="store_true", help="disable the optimiser")
    parser.add_argument(
        "--no-data-check", action="store_true", help="skip data-contract validation"
    )
    parser.add_argument("--emit", help="write the generated Python to this file")
    parser.add_argument(
        "--stages", help="comma separated subset of: " + ", ".join(ALL_STAGES)
    )
    args = parser.parse_args(argv)
    stages = parse_stages(args.stages, args.run)

    if args.demo:
        return run_demo(stages, args.run)

    if args.command:
        source, directory, name = args.command, EXAMPLES, "<command line>"
    elif args.file:
        path = Path(args.file)
        if not path.exists():
            print(f"No such file: {path}", file=sys.stderr)
            return 2
        source, directory, name = path.read_text(encoding="utf-8"), path.parent, path.name
    else:
        parser.print_help()
        return 0

    result = compile_source(
        source,
        base_dir=directory,
        validate_data=not args.no_data_check,
        optimise=not args.no_optimise,
        run=args.run,
    )
    show(result, stages, name)

    if args.emit and result.code is not None:
        target = Path(args.emit)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(result.code.source, encoding="utf-8")
        print(f"\nGenerated Python written to {target}")

    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
