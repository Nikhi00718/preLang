"""Runs the test cases listed in the Review 3 report and records what really happened.

    py -3 docs/review3/cases.py      (after building build/Release; writes docs/review3/cases.json)

Each case is a program (with small data files where needed). The recorded output is the line
of the compiler or of the generated program that shows the result.
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
COMPILER = str(ROOT / "build" / "Release" / "preplang.exe")
WORK = ROOT / "eval" / "work" / "cases"

STUDENTS = "name,marks\nAsha,85\nRavi,100\nMina,0\n"


def compile_program(name, source, files=None, flags=("--explain",)):
    folder = WORK / name
    folder.mkdir(parents=True, exist_ok=True)
    for file, text in (files or {}).items():
        (folder / file).write_bytes(text if isinstance(text, bytes) else text.encode("utf-8"))
    (folder / "p.prep").write_text(source, encoding="utf-8")
    done = subprocess.run([COMPILER, "p.prep", *flags], cwd=folder, capture_output=True, text=True)
    return folder, done.stdout + done.stderr


def first(text, *markers):
    """The first line that contains one of the markers."""
    for line in text.splitlines():
        if any(line.strip().startswith(marker) for marker in markers):
            return line.strip()
    return "(no such line)"


def example(path, *markers, flags=("--explain",)):
    done = subprocess.run([COMPILER, path, *flags], cwd=ROOT, capture_output=True, text=True)
    return first(done.stdout + done.stderr, *markers)


def run_generated(folder, *args):
    done = subprocess.run([sys.executable, str(folder / "output" / "generated.py"), *args], capture_output=True,
                          text=True)
    return done.stdout + done.stderr


def main():
    cases = []

    def add(kind, what, expected, actual):
        cases.append({"kind": kind, "what": what, "expected": expected, "actual": actual})
        print(kind, "|", what, "|", actual)

    # Valid inputs
    out = subprocess.run([COMPILER, "examples/tabular/students.prep", "--run"], cwd=ROOT, capture_output=True,
                         text=True).stdout
    add("valid", "Compile and run examples/tabular/students.prep", "the cleaned file is written",
        first(out, "Saved").replace(str(ROOT) + "\\", ""))
    add("valid", "Compile examples/tabular/tableset.prep (a folder of four tables)", "compiles without errors",
        example("examples/tabular/tableset.prep", "Compiled successfully", "Compilation failed"))
    add("valid", "Compile examples/tabular/metadata_only.prep (Parquet, footer only)",
        "contracts proven from the footer statistics",
        example("examples/tabular/metadata_only.prep", "contracts proven"))
    add("valid", "Compile examples/images/linked.prep (images linked to labels)", "compiles without errors",
        example("examples/images/linked.prep", "Compiled successfully", "Compilation failed"))

    # Invalid inputs
    add("invalid", "A character the language does not have ($)", "lexical error",
        example("examples/errors/lexical_error.prep", "error["))
    add("invalid", "A misspelled keyword (NORMALISE)", "syntax error with a suggestion",
        example("examples/errors/invalid.prep", "= hint"))
    add("invalid", "NORMALIZE on a text column", "type error",
        first(compile_program("text_normalize", 'LOAD "s.csv" AS s;\nNORMALIZE s.name USING MINMAX;\n'
                              'SAVE s AS "o.csv";\n', {"s.csv": STUDENTS})[1], "error["))
    add("invalid", "A column used after DROP removed it", "semantic error naming the DROP",
        first(compile_program("use_after_drop", 'LOAD "s.csv" AS s;\nDROP s [marks];\n'
                              'FILTER s WHERE marks > 5;\nSAVE s AS "o.csv";\n', {"s.csv": STUDENTS})[1], "error["))
    add("invalid", "A contract the data breaks (marks BETWEEN 0 AND 90)", "compile error naming the row",
        first(compile_program("violated", 'LOAD "s.csv" AS s;\nEXPECT s.marks BETWEEN 0 AND 90;\nSAVE s AS "o.csv";\n',
                              {"s.csv": STUDENTS})[1], "error["))

    # Boundary cases
    add("boundary", "An empty CSV file", "schema error",
        first(compile_program("empty", 'LOAD "e.csv" AS e;\nSAVE e AS "o.csv";\n', {"e.csv": ""})[1], "error["))
    add("boundary", "A CSV row with one value too many", "schema error with the line number",
        first(compile_program("extra", 'LOAD "x.csv" AS x;\nSAVE x AS "o.csv";\n',
                              {"x.csv": "a,b\n1,2\n3,4,5\n"})[1], "error["))
    add("boundary", "A CSV file saved as Windows-1252, not UTF-8", "schema error with line and byte",
        first(compile_program("cp1252", 'LOAD "w.csv" AS w;\nSAVE w AS "o.csv";\n',
                              {"w.csv": "name,city\nJos\xe9,Pune\n".encode("cp1252")})[1], "error["))
    add("boundary", "BETWEEN 0 AND 100 when the data has exactly 0 and 100", "PROVEN (both ends included)",
        first(compile_program("between_edges", 'LOAD "s.csv" AS s;\nEXPECT s.marks BETWEEN 0 AND 100;\n'
                              'SAVE s AS "o.csv";\n', {"s.csv": STUDENTS}, ("--semantic",))[1], "line 2: EXPECT"))
    add("boundary", "FILTER marks > 100 when the largest mark is 100", "warning: removes every row",
        first(compile_program("never_true", 'LOAD "s.csv" AS s;\nFILTER s WHERE marks > 100;\nSAVE s AS "o.csv";\n',
                              {"s.csv": STUDENTS})[1], "warning["))

    # Error conditions at run time
    folder, _ = compile_program("certificate", 'LOAD "s.csv" AS s;\nEXPECT s.marks BETWEEN 0 AND 100;\n'
                                'SAVE s AS "o.csv";\n', {"s.csv": STUDENTS}, ("--python",))
    (folder / "next.csv").write_text("name,marks\nZoe,99\nLi,3\n", encoding="utf-8")
    (folder / "bad.csv").write_text("name,marks\nZoe,99\nLi,130\n", encoding="utf-8")
    add("run time", "The compiled program given a new batch inside the certificate", "admitted and run",
        first(run_generated(folder, f"s={folder / 'next.csv'}"), "Admitted").replace(str(folder) + "\\", ""))
    add("run time", "The compiled program given a batch with marks 130", "refused, naming the row and the proof",
        first(run_generated(folder, f"s={folder / 'bad.csv'}"), "ContractError").replace(str(folder) + "\\", ""))
    folder, _ = compile_program("deferred", 'LOAD "s.csv" AS s;\nFILTER s WHERE marks < 100;\n'
                                'EXPECT s.marks BETWEEN 50 AND 100;\nSAVE s AS "o.csv";\n', {"s.csv": STUDENTS},
                                ("--python",))
    add("run time", "A deferred contract that fails when the program runs", "ContractError naming the line",
        first(run_generated(folder), "ContractError"))

    (HERE / "cases.json").write_text(json.dumps(cases, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
