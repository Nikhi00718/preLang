"""Tests of tools/release_facts.py, the data owner's differentially private release.

    py -3 tests/test_release_facts.py

One file loaded under two spellings is one table: the release must not divide the budget as if
they were two tables of different rows, which would spend epsilon once per spelling on the same
rows (the tables of a release compose in parallel because a row is in one table only).
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release_facts  # noqa: E402

failures = 0


def check(passed, name):
    global failures
    print(("  PASS  " if passed else "  FAIL  ") + name)
    failures += not passed


def queries(folder, spellings):
    lines = ["preplang-queries\t1", "constants\t0\t100"]
    for i, spelling in enumerate(spellings):
        lines += [f"table\t{spelling}\tt{i}\tcsv\t{Path(folder, spelling).as_posix()}", "column\tv\tINTEGER\trange"]
    path = Path(folder, f"queries{len(spellings)}.tsv")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return str(path)


def main():
    print("One file under two spellings is one table")
    with tempfile.TemporaryDirectory() as folder:
        Path(folder, "p.csv").write_text("v\n" + "".join(f"{i % 50}\n" for i in range(200)), encoding="utf-8")
        os.makedirs(Path(folder, "sub"))
        spellings = ["p.csv", "sub/../p.csv"] + (["P.CSV"] if os.path.exists(Path(folder, "P.CSV")) else [])
        text = release_facts.release([queries(folder, spellings)], 1.0, 0.05, 0)
        mechanism = next(iter(text.values())).splitlines()[1]
        check("epsilon 1 for each of 1 tables" in mechanism,
              f"{len(spellings)} spellings ({', '.join(spellings)}) share one budget: {mechanism[:90]}")
        facts = [line for line in next(iter(text.values())).splitlines() if line.startswith("column")]
        check(len(set(facts)) == 1, "every spelling gets the same facts")
    print("All release tests passed" if failures == 0 else f"{failures} release test(s) FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
