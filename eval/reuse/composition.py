"""Do certificates compose across pipeline stages? (RQ10)

    py -3 eval/reuse/composition.py [path/to/preplang.exe]

Each real workload of eval/reuse/realbatches.py (the cleaning stage) gets a second
stage that reads its output, as a feature or model-input step would. Both
stages are compiled once: stage 1 on the first month, stage 2 on what stage 1
writes for that month. tools/contracts.py then compares statically what stage 2
relies on with what stage 1 guarantees. Every later month is run through both
compiled stages, and stage 2 is also compiled and run fresh on stage 1's output
as the ground truth. The claim checked: a fact the composition says is
guaranteed never makes stage 2 refuse or deoptimize; only facts it reports as
open can. Then stage 1 is strengthened with the EXPECT statements the tool
suggests, and the months are run again.

Writes eval/results/composition.json and .md.
"""
import json
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from policies import compile_variants, run_fresh, run_program, same_outputs, score  # noqa: E402
from realbatches import BIKES, REAL, TAXI, TRAFFIC  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "eval" / "work" / "composition"
RESULTS = ROOT / "eval" / "results"
PYTHON = sys.executable

STAGE2 = {
    "taxi": ("trips_clean.parquet", "trips", "features.prep", """# Fare-model features from the cleaned trips.
LOAD "trips_clean.parquet" AS trips;
EXPECT trips.fare_amount BETWEEN 0 AND 1000;
EXPECT trips.passenger_count NOT_NULL;
EXPECT trips.trip_distance BETWEEN 0 AND 100;
EXPECT trips.tip_amount BETWEEN 0 AND 500;
FILL_NULL trips.payment_type WITH 5;
FILTER trips WHERE fare_amount >= 0;
NORMALIZE trips.trip_distance USING MINMAX;
SAVE trips AS "fare_features.parquet";
"""),
    "traffic": ("traffic_clean.csv", "traffic", "features.prep", """# Demand-model features from the cleaned traffic data.
LOAD "traffic_clean.csv" AS traffic;
EXPECT traffic.traffic_volume BETWEEN 0 AND 1;
EXPECT traffic.weather_main NOT_NULL;
EXPECT traffic.rain_1h BETWEEN 0 AND 100;
EXPECT traffic.temp BETWEEN 230 AND 320;
FILTER traffic WHERE clouds_all <= 100;
SAVE traffic AS "traffic_features.csv";
"""),
    "bikes": ("days_clean.csv", "days", "features.prep", """# Bike-demand features from the cleaned days.
LOAD "days_clean.csv" AS days;
EXPECT days.hum BETWEEN 0 AND 1;
EXPECT days.windspeed NOT_NULL;
EXPECT days.weathersit BETWEEN 1 AND 4;
FILTER days WHERE temp >= 0;
NORMALIZE days.windspeed USING MINMAX;
SAVE days AS "days_features.csv";
"""),
}
# The month both stages are compiled with: the first one, except for the taxi data, whose
# publisher changed the type of two columns in February 2023 (RQ8): its first month after that.
STAGE1 = {"taxi": (TAXI, "trips.parquet", "trips", REAL / "green", "2023-02"),
          "traffic": (TRAFFIC, "traffic.csv", "traffic", REAL / "traffic", None),
          "bikes": (BIKES, "days.csv", "days", REAL / "bikes", None)}


def compose(stage1, stage2, link, report):
    done = subprocess.run([PYTHON, str(ROOT / "tools" / "contracts.py"), "check",
                           str(stage1 / "output" / "preplang_contract.json"),
                           str(stage2 / "output" / "preplang_contract.json"), "--link", link, "--json", str(report)],
                          capture_output=True, text=True)
    result = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {}
    return done.stdout, result


def open_columns(result):
    return {(item["column"], item["kind"]) for link in result.get("links", []) for item in link["open"]}


def deopt_fact(line, stage2_program):
    """The column a deoptimization message is about: "line 5: EXPECT trips.tip_amount ...: tip_amount goes up to 600"."""
    problem = line.rsplit(": ", 1)[-1]
    column = problem.split(" ")[0]
    kind = "not_null" if "missing" in problem else "min" if "goes down" in problem else "max"
    return column, kind


def run_workload(compiler, name, strengthen):
    program1, input1, table1, months_folder, anchor = STAGE1[name]
    output1, table2, file2, program2 = STAGE2[name]
    months = sorted(months_folder.glob("*.*"))
    if anchor:
        months = [m for m in months if m.stem >= anchor]
    folder = WORK / name / ("strengthened" if strengthen else "as written")
    stage1, stage2 = folder / "stage1", folder / "stage2"
    for path in (stage1, stage2):
        if path.exists():
            shutil.rmtree(path)
        path.mkdir(parents=True)
    # Stage 1, compiled on the first month and run on it: its output is what stage 2 is compiled with.
    (stage1 / "program.prep").write_text(program1, encoding="utf-8")
    shutil.copyfile(months[0], stage1 / input1)
    if strengthen:
        fixes = [f for link in strengthen.get("links", []) for f in link.get("fixes", []) if f.startswith("EXPECT")]
        text = program1.splitlines()
        save = max(i for i, line in enumerate(text) if line.startswith("SAVE"))
        program1 = "\n".join(text[:save] + fixes + text[save:]) + "\n"
        (stage1 / "program.prep").write_text(program1, encoding="utf-8")
    ok, text = compile_variants(compiler, stage1, "program.prep")
    assert ok, text
    first = run_program(stage1, "generated.py", {})
    assert first["status"] == "finished", first
    (stage2 / file2).write_text(program2, encoding="utf-8")
    (stage2 / output1).write_bytes(first["files"][output1])
    ok, text = compile_variants(compiler, stage2, file2)
    assert ok, text
    static, result = compose(stage1, stage2, f"{output1}={table2}", folder / "composition.json")
    open_facts = open_columns(result)
    rows = []
    fresh2 = folder / "fresh2"
    for month in months[1:]:
        one = run_program(stage1, "generated.py", {table1: month})
        row = {"month": month.stem, "stage1": one["status"], "stage1_deopts": one["deopts"]}
        if one["status"] == "finished":
            batch = folder / f"stage1_output{Path(output1).suffix}"
            batch.write_bytes(one["files"][output1])
            two = run_program(stage2, "generated.py", {table2: batch})
            if fresh2.exists():
                shutil.rmtree(fresh2)
            fresh2.mkdir()
            (fresh2 / file2).write_text(program2, encoding="utf-8")
            shutil.copyfile(batch, fresh2 / output1)
            fresh = run_fresh(compiler, fresh2, file2)
            facts = [deopt_fact(d, program2) for d in two["deopts"]]
            row.update(stage2=two["status"], stage2_deopts=two["deopts"], stage2_reason=two["reason"],
                       fresh2=fresh["status"], outcome=score(two, fresh),
                       guaranteed_fact_broken=any(f not in open_facts for f in facts))
        rows.append(row)
        print(name, "strengthened" if strengthen else "as written", month.stem, row.get("stage1"), row.get("stage2"),
              len(row.get("stage2_deopts", [])), flush=True)
    return {"workload": name, "strengthened": bool(strengthen), "composition": result, "report": static,
            "program1": program1, "months": rows}


def main(compiler):
    results = []
    for name in STAGE2:
        written = run_workload(compiler, name, None)
        results.append(written)
        if written["composition"].get("missing"):
            results.append(run_workload(compiler, name, written["composition"]))
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "composition.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    write_markdown(results)


def write_markdown(results):
    lines = ["# RQ10: composing certificates across pipeline stages", "",
             "Stage 1 cleans a month (eval/reuse/realbatches.py), stage 2 builds features from its output. Both are "
             "compiled once on the first month; tools/contracts.py compares what stage 2 relies on with stage 1's "
             "guarantee. *Strengthened*: stage 1 with the EXPECT statements the tool suggested.", "",
             "| Workload | Stage 1 | Facts stage 2 relies on | Guaranteed | Months through both | Stage 2 refused | "
             "Stage 2 deoptimized | ... by a guaranteed fact | Stage 1 stopped | Stage 2 like fresh |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for r in results:
        links = r["composition"].get("links", [{}])
        facts = sum(link.get("facts", 0) for link in links)
        held = sum(link.get("guaranteed", 0) for link in links)
        months = r["months"]
        through = [m for m in months if m.get("stage2")]
        lines.append(
            f"| {r['workload']} | {'strengthened' if r['strengthened'] else 'as written'} | {facts} | {held} | "
            f"{len(through)} | {sum(m['stage2'] == 'refused' for m in through)} | "
            f"{sum(bool(m['stage2_deopts']) for m in through)} | "
            f"{sum(m['guaranteed_fact_broken'] for m in through)} | "
            f"{sum(m['stage1'] != 'finished' for m in months)} | "
            f"{sum(m['outcome'] in ('correct run', 'correct stop') for m in through)} |")
    lines += ["", "## What the composition reported", ""]
    for r in results:
        lines += [f"### {r['workload']} ({'strengthened' if r['strengthened'] else 'as written'})", "", "```",
                  r["report"].rstrip(), "```", ""]
    (RESULTS / "composition.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
