"""How large are the certificates? (part of RQ7)

    py -3 eval/certsize.py [path/to/preplang.exe]

For the table programs of eval/certcost.py (the examples and the real pipelines, compiled on their
first month), counts the facts in the contract file every compilation writes: the full certificate
(--no-deopt), the admission certificate, and each item certificate's facts beyond admission. A fact
is a column's type, its having no missing values, a lower bound or an upper bound. Image programs are
left out: their certificates are boxes of image shapes, which the contract file does not list.
Writes eval/results/certsize.json and .md.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "certsize"
SOURCE = ROOT / "eval" / "work" / "certcost"  # the copies eval/certcost.py compiled
RESULTS = ROOT / "eval" / "results"
FACTS = ("type", "not_null", "min", "max")


def count(columns):
    return sum(1 for facts in columns.values() for key in FACTS
               if facts.get(key) is not None and facts.get(key) is not False)  # a bound of 0 is a fact


def file_columns(path):
    """Columns of an input file, or 0 for an input that is not one file (a folder of tables)."""
    import pandas as pd
    if not path.is_file():
        return 0
    return len(pd.read_parquet(path).columns if path.suffix == ".parquet" else pd.read_csv(path, nrows=1).columns)


def contract(compiler, folder, program, extra):
    target = WORK / f"{folder.name}_{Path(program).stem}{'_full' if extra else ''}"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(folder, target, ignore=shutil.ignore_patterns("output", "processed"))
    done = subprocess.run([str(compiler), program, "--python", *extra], cwd=target, capture_output=True, text=True)
    assert done.returncode == 0, done.stdout + done.stderr
    return json.loads((target / "output" / "preplang_contract.json").read_text(encoding="utf-8"))


def main(compiler):
    jobs = [(SOURCE / "examples" / "tabular", prep.name) for prep in sorted((SOURCE / "examples" / "tabular").glob("*.prep"))]
    jobs += [(SOURCE / workload, "program.prep") for workload in ("traffic", "bikes", "taxi")]
    rows = []
    for folder, program in jobs:
        deopt, full = contract(compiler, folder, program, []), contract(compiler, folder, program, ["--no-deopt"])
        if not full["inputs"]:
            continue
        items = [sum(count(columns) for columns in item["needs"].values()) for item in deopt["removals"]]
        columns = sum(len(table["columns"]) for table in full["inputs"].values())
        in_files = sum(file_columns(folder / table["file"]) for table in full["inputs"].values())
        rows.append({"program": program if folder.name == "tabular" else folder.name, "columns": columns,
                     "file_columns": in_files,
                     "full": sum(count(t["columns"]) for t in full["inputs"].values()),
                     "admission": sum(count(t["columns"]) for t in deopt["inputs"].values()),
                     "items": items})
        print(rows[-1], flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "certsize.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    lines = ["# Certificate sizes (facts)", "",
             "| Program | Columns in the input files | Certified columns | Full certificate | Admission certificate | "
             "Removals | Facts per item certificate |", "|---|---:|---:|---:|---:|---:|---|"]
    for r in rows:
        lines.append(f"| {r['program']} | {r['file_columns'] or '-'} | {r['columns']} | {r['full']} | "
                     f"{r['admission']} | {len(r['items'])} | {', '.join(map(str, r['items'])) or '-'} |")
    lines.append("")
    (RESULTS / "certsize.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
