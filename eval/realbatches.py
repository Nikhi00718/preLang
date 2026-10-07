"""Data certificates on real data that changes over time (RQ8).

    py -3 eval/realbatches.py download     # once: public datasets into eval/data/real/
    py -3 eval/realbatches.py [path/to/preplang.exe]

Three public datasets with real monthly batches:
  * traffic  UCI Metro Interstate Traffic Volume, hourly 2012-2018, one CSV per month
  * bikes    UCI Bike Sharing, daily 2011-2012, one CSV per month
  * taxi     NYC TLC green taxi trip records 2022-2024, the published Parquet file of each month

Each pipeline is written once, as a data engineer would, and deployed in two ways:
  * anchor=first     compiled on the first month, then run on every later month
  * anchor=previous  compiled each month on the month before, then run on the next one
The compiled program is given the new month with name=file; as ground truth the month is put
in place of the input, compiled and run fresh. Batches are cut from the raw files line by line,
so no value is re-formatted. Writes eval/results/realbatches.json and .md.
"""
import json
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from certificates import file_facts_admit, outputs, same_outputs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
REAL = ROOT / "eval" / "data" / "real"
WORK = ROOT / "eval" / "work" / "realbatches"
RESULTS = ROOT / "eval" / "results"
PYTHON = sys.executable

TRAFFIC = """# Hourly traffic and weather of one month, cleaned for a forecasting model.
LOAD "traffic.csv" AS traffic;
EXPECT traffic.traffic_volume BETWEEN 0 AND 10000;
EXPECT traffic.temp BETWEEN 230 AND 320;            # Kelvin
EXPECT traffic.clouds_all BETWEEN 0 AND 100;
EXPECT traffic.weather_main NOT_NULL;
FILL_NULL traffic.rain_1h WITH 0;
FILTER traffic WHERE rain_1h < 100;                  # drops impossible rain readings
NORMALIZE traffic.traffic_volume USING MINMAX;
SELECT traffic [date_time, temp, rain_1h, clouds_all, weather_main, traffic_volume];
SAVE traffic AS "traffic_clean.csv";
"""

BIKES = """# Daily bike rentals of one month, cleaned for a demand model.
LOAD "days.csv" AS days;
EXPECT days.cnt BETWEEN 0 AND 10000;
EXPECT days.hum BETWEEN 0 AND 1;
EXPECT days.temp BETWEEN 0 AND 1;
EXPECT days.weathersit BETWEEN 1 AND 4;
FILL_NULL days.windspeed WITH 0;
FILTER days WHERE hum > 0;                            # a humidity of 0 is a sensor failure
NORMALIZE days.cnt USING ZSCORE;
SELECT days [dteday, season, weathersit, temp, hum, windspeed, cnt];
SAVE days AS "days_clean.csv";
"""

TAXI = """# Green taxi trips of one month, cleaned for a fare model.
LOAD "trips.parquet" AS trips;
EXPECT trips.VendorID NOT_NULL;
EXPECT trips.PULocationID BETWEEN 1 AND 265;
EXPECT trips.DOLocationID BETWEEN 1 AND 265;
EXPECT trips.lpep_pickup_datetime NOT_NULL;
FILL_NULL trips.passenger_count WITH 1;
FILL_NULL trips.payment_type WITH 5;
FILTER trips WHERE fare_amount >= 0 AND trip_distance > 0 AND trip_distance < 100;
EXPECT trips.fare_amount BETWEEN 0 AND 1000;
SELECT trips [lpep_pickup_datetime, PULocationID, DOLocationID, passenger_count, trip_distance, fare_amount, tip_amount, payment_type];
SAVE trips AS "trips_clean.parquet";
"""

WORKLOADS = {  # name -> (program, input file name, input name, folder of monthly batches)
    "traffic": (TRAFFIC, "traffic.csv", "traffic", REAL / "traffic"),
    "bikes": (BIKES, "days.csv", "days", REAL / "bikes"),
    "taxi": (TAXI, "trips.parquet", "trips", REAL / "green"),
}


def download():
    REAL.mkdir(parents=True, exist_ok=True)
    sources = {"metro.zip": "https://archive.ics.uci.edu/static/public/492/metro+interstate+traffic+volume.zip",
               "bike.zip": "https://archive.ics.uci.edu/static/public/275/bike+sharing+dataset.zip"}
    for name, url in sources.items():
        if not (REAL / name).exists():
            urllib.request.urlretrieve(url, REAL / name)
        zipfile.ZipFile(REAL / name).extractall(REAL)
    (REAL / "green").mkdir(exist_ok=True)
    for year in (2022, 2023, 2024):
        for month in range(1, 13):
            target = REAL / "green" / f"{year}-{month:02d}.parquet"
            if not target.exists():
                urllib.request.urlretrieve(
                    f"https://d37ci6vzurychx.cloudfront.net/trip-data/green_tripdata_{year}-{month:02d}.parquet", target)
    import gzip
    with gzip.open(REAL / "Metro_Interstate_Traffic_Volume.csv.gz", "rb") as packed:
        (REAL / "Metro_Interstate_Traffic_Volume.csv").write_bytes(packed.read())
    split_by_month(REAL / "Metro_Interstate_Traffic_Volume.csv", "date_time", REAL / "traffic")
    split_by_month(REAL / "day.csv", "dteday", REAL / "bikes")


def split_by_month(source, column, folder):
    """One CSV per month, cut from the raw lines (YYYY-MM at the start of `column`)."""
    folder.mkdir(exist_ok=True)
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    index = lines[0].rstrip("\r\n").split(",").index(column)
    months = defaultdict(list)
    for line in lines[1:]:
        months[line.split(",")[index][:7]].append(line)
    for month, rows in months.items():
        if len(rows) >= 20:  # the traffic data has a gap; skip near-empty months
            (folder / f"{month}.csv").write_text(lines[0] + "".join(rows), encoding="utf-8")


def read(path):
    if path.suffix == ".parquet":
        return pd.read_parquet(path, dtype_backend="numpy_nullable")
    return pd.read_csv(path, keep_default_na=False, na_values=[""], dtype_backend="numpy_nullable")


def prepare(folder, program, input_file, batch):
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    shutil.copyfile(batch, folder / input_file)


def compile_run(compiler, folder, run):
    done = subprocess.run([str(compiler), "program.prep", "--run" if run else "--python"], cwd=folder,
                          capture_output=True, text=True)
    text = done.stdout + done.stderr
    if "Compilation failed" in text:
        return "compile error", text
    return ("finished" if done.returncode == 0 else "contract error"), text


def run_compiled(folder, name, batch):
    out = folder / "output"
    for path in list(out.rglob("*")):
        if path.is_file() and path.name != "generated.py":
            path.unlink()
    done = subprocess.run([PYTHON, str(out / "generated.py"), f"{name}={batch}"], capture_output=True, text=True)
    text = done.stdout + done.stderr
    if done.returncode == 0:
        return "finished", "", outputs(out)
    reason = next((line for line in text.splitlines() if line.startswith("ContractError")), text[-300:])
    return ("refused" if "is not the input the program was compiled with" in text else "contract error"), reason, {}


def main(compiler):
    rows = []
    for workload, (program, input_file, name, folder) in WORKLOADS.items():
        batches = sorted(folder.glob("*.*"))
        for mode in ("first", "previous"):
            anchor_folder = WORK / workload / "compiled"
            for i, batch in enumerate(batches[1:], start=1):
                anchor = batches[0] if mode == "first" else batches[i - 1]
                if mode == "previous" or i == 1:
                    prepare(anchor_folder, program, input_file, anchor)
                    anchor_status, text = compile_run(compiler, anchor_folder, run=False)
                    anchor_ok = anchor_status == "finished"
                if not anchor_ok:  # the anchor month itself does not compile: nothing to deploy
                    rows.append({"workload": workload, "mode": mode, "anchor": anchor.stem, "batch": batch.stem,
                                 "compiled_once": "anchor fails", "fresh": None, "reason": "", "file_facts_admit": None,
                                 "same_outputs": None})
                    continue
                compiled_once, reason, out_a = run_compiled(anchor_folder, name, batch)
                fresh_folder = WORK / workload / "fresh"
                prepare(fresh_folder, program, input_file, batch)
                fresh, _ = compile_run(compiler, fresh_folder, run=True)
                out_b = outputs(fresh_folder / "output") if fresh == "finished" else {}
                generated = (anchor_folder / "output" / "generated.py").read_text(encoding="utf-8")
                original, new = read(anchor), read(batch)
                columns = [c for c in original.columns if f'"{c}"' in generated]
                rows.append({"workload": workload, "mode": mode, "anchor": anchor.stem, "batch": batch.stem,
                             "compiled_once": compiled_once, "fresh": fresh, "reason": reason,
                             "file_facts_admit": file_facts_admit(original, new, columns),
                             "same_outputs": same_outputs(out_a, out_b) if compiled_once == fresh == "finished" else None})
                print(workload, mode, anchor.stem, "->", batch.stem, compiled_once, fresh, flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "realbatches.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    write_markdown(rows)


def agrees(r):
    if r["compiled_once"] == "finished":
        return r["fresh"] == "finished" and r["same_outputs"]
    return r["compiled_once"] == "contract error" and r["fresh"] in ("contract error", "compile error")


def write_markdown(rows):
    lines = ["# RQ8: data certificates on real monthly batches", "",
             "Each pipeline is compiled on one month (the first month, or the month before) and given the "
             "next months with `name=file`; each month is also compiled and run fresh as the ground truth.", "",
             "| Workload | Anchor | Batches | Fingerprint | File facts | Certificate | Ends like fresh | "
             "Unsound | Refused, fresh runs |", "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for workload in WORKLOADS:
        for mode in ("first", "previous"):
            items = [r for r in rows if r["workload"] == workload and r["mode"] == mode and r["fresh"] is not None]
            admitted = [r for r in items if r["compiled_once"] != "refused"]
            unsound = sum(r["compiled_once"] == "finished" and not agrees(r) for r in items)
            needless = sum(r["compiled_once"] == "refused" and r["fresh"] == "finished" for r in items)
            lines.append(f"| {workload} | {mode} | {len(items)} | 0 | {sum(r['file_facts_admit'] for r in items)} | "
                         f"{len(admitted)} | {sum(agrees(r) for r in admitted)} | {unsound} | {needless} |")
    lines += ["", "## Why batches were refused", ""]
    reasons = Counter()
    for r in rows:
        if r["compiled_once"] == "refused":
            needed = r["reason"].split("relies on this for ")[-1].split(". Compile")[0]
            reasons[(r["workload"], needed if "relies on this" in r["reason"] else r["reason"][:120])] += 1
    for (workload, reason), count in reasons.most_common():
        lines.append(f"- {workload}: {count} x {reason}")
    lines.append("")
    (RESULTS / "realbatches.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "download":
        download()
    else:
        main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
