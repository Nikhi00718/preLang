"""
main.py -- PrepLang Review 1 command line driver.

Usage
-----
    python main.py examples/demo_valid.prep
    python main.py examples/demo_syntax_error.prep
    python main.py --demo                     # run every bundled example
    python main.py -c 'NORMALIZE students.age;'

The driver runs the two phases implemented in Phase 1 of the project:

    source text --> [ LEXICAL ANALYSIS ] --> tokens
                --> [ SYNTAX ANALYSIS ]  --> validated statements / errors
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from preplang import (
    ErrorReporter,
    format_statement_table,
    format_token_table,
)
from preplang.lexer import Lexer
from preplang.parser_basic import BasicParser

BASE_DIR = Path(__file__).resolve().parent
LINE = "=" * 68


def banner(title: str) -> str:
    return f"\n{LINE}\n  {title}\n{LINE}"


def compile_source(source: str, name: str = "<input>", show_tokens: bool = True) -> bool:
    """Run lexing + syntax validation on *source*; return True when it is valid."""
    print(banner(f"PrepLang Review 1 Compiler  --  {name}"))

    print("\n--- SOURCE PROGRAM " + "-" * 49)
    for number, text in enumerate(source.splitlines(), start=1):
        print(f"{number:>3} | {text}")

    reporter = ErrorReporter(source)

    # ---- Phase 1: Lexical Analysis -----------------------------------
    lexer = Lexer(source, reporter)
    tokens = lexer.tokenize()
    if show_tokens:
        print("\n--- PHASE 1: LEXICAL ANALYSIS (TOKENS) " + "-" * 30)
        print(format_token_table(tokens))

    # ---- Phase 2: Syntax Analysis ------------------------------------
    parser = BasicParser(tokens, reporter)
    statements = parser.parse()
    print("\n--- PHASE 2: SYNTAX ANALYSIS (RECOGNISED STATEMENTS) " + "-" * 16)
    print(format_statement_table(statements))

    # ---- Diagnostics --------------------------------------------------
    print("\n--- DIAGNOSTICS " + "-" * 52)
    if reporter.ok:
        print("OK  No lexical or syntax errors detected.")
        print(f"    {len(tokens) - 1} tokens, {len(statements)} statements.")
    else:
        print(reporter.render())
        print("\n" + "-" * 60)
        print(f"Compilation failed: {reporter.summary()}")
    return reporter.ok


def run_file(path: Path, show_tokens: bool = True) -> bool:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"Unable to read {path}: {exc}", file=sys.stderr)
        return False
    return compile_source(source, path.name, show_tokens)


def run_demo() -> int:
    """Run every ``.prep`` file in examples/ -- the Review 1 demonstration."""
    examples = sorted((BASE_DIR / "examples").glob("*.prep"))
    if not examples:
        print("No examples found.", file=sys.stderr)
        return 1
    failures = 0
    for example in examples:
        ok = run_file(example, show_tokens=example.name.startswith("demo_valid"))
        expected_fail = "error" in example.name
        if ok == expected_fail:  # valid file failed, or bad file passed
            failures += 1
    print(banner("DEMO SUMMARY"))
    print(f"{len(examples)} example programs executed, {failures} unexpected result(s).")
    return 0 if failures == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="preplang-r1",
        description="PrepLang Review 1: lexical analysis and syntax validation.",
    )
    parser.add_argument("file", nargs="?", help="PrepLang source file (.prep)")
    parser.add_argument("-c", "--command", help="compile a source string given on the command line")
    parser.add_argument("--demo", action="store_true", help="run all bundled example programs")
    parser.add_argument("--no-tokens", action="store_true", help="hide the token table")
    args = parser.parse_args(argv)

    if args.demo:
        return run_demo()
    if args.command:
        return 0 if compile_source(args.command, "<command line>", not args.no_tokens) else 1
    if args.file:
        return 0 if run_file(Path(args.file), not args.no_tokens) else 1

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
