"""End-to-end test of the compiler on every program in examples/.

    py -3 tests/test_examples.py path/to/preplang

Each program is compiled with every stage printed, then with --explain and --dry-run. The programs
in examples/errors/ must fail with exit code 1 and print an error; every other program must
compile with exit code 0, and the Python program it generates must be valid Python (it is
byte-compiled, not run, so no pandas or OpenCV is needed). The compiler writes into output/ of the
current folder, so run it from a scratch folder (ctest runs it in the build folder).
"""
import py_compile
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = Path(sys.argv[1]).resolve()

failures = 0


def check(passed, name):
    global failures
    print(("  PASS  " if passed else "  FAIL  ") + name)
    failures += not passed


def compile_program(program, *flags):
    return subprocess.run([str(COMPILER), str(program), *flags], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


for program in sorted((ROOT / "examples").rglob("*.prep")):
    name = program.relative_to(ROOT).as_posix()
    failing = program.parent.name == "errors"
    done = compile_program(program)
    if failing:
        check(done.returncode == 1 and "error" in done.stdout, f"{name}: reported as an error")
        continue
    check(done.returncode == 0, f"{name}: compiles")
    generated = Path("output") / "generated.py"
    try:
        py_compile.compile(str(generated), doraise=True)
        check(True, f"{name}: generated program is valid Python")
    except (py_compile.PyCompileError, FileNotFoundError) as error:
        check(False, f"{name}: generated program is valid Python ({error})")
    for flag in ("--explain", "--dry-run", "--lineage"):
        check(compile_program(program, flag).returncode == 0, f"{name}: {flag}")

version = subprocess.run([str(COMPILER), "--version"], capture_output=True, text=True)
check(version.returncode == 0 and version.stdout.startswith("preplang "), "--version")
check(subprocess.run([str(COMPILER)], capture_output=True).returncode == 2, "no arguments: usage, exit code 2")

print(f"{failures} failure(s)")
sys.exit(1 if failures else 0)
