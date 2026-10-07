"""Can a compiled program be reused on new data? (RQ7: data certificates)

    py -3 eval/certificates.py [path/to/preplang.exe]

Each workload is compiled once. It then gets new batches of its input tables:
other days of the same process, and edits real pipelines see (rows resampled,
values drifting, values missing, values out of range). For every batch:

  * compiled once:  the generated program is run with table=batch.csv; it
                    admits the batch if the batch satisfies the data certificate
  * fresh:          the batch is put in place of the file, compiled and run
                    (the ground truth: what compiling for this batch gives)

Three admission policies are compared on the same batches:
  * fingerprint     the earlier guard: only the byte-identical file
  * file facts      every fact of the compiled file (type, no missing values,
                    [min, max] of every column read), like a schema inferred by TFDV
  * certificate     only the facts the program's proofs need (PrepLang)

Soundness: when the compiled-once program admits a batch and finishes, the
fresh compilation must finish too and write exactly the same files.
Writes eval/results/certificates.json and eval/results/certificates.md.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "certificates"
RESULTS = ROOT / "eval" / "results"
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe"
PYTHON = sys.executable
SEEDS = 4

DAILY_PROGRAM = """# Sensor readings of one day, cleaned for a dashboard.
LOAD "day.csv" AS day;
EXPECT day.humidity BETWEEN 0 AND 100;
EXPECT day.temp BETWEEN -40 AND 60;
EXPECT day.id UNIQUE;
EXPECT day.status NOT_NULL;
FILL_NULL day.humidity WITH 50;
FILL_NULL day.reading WITH 0;
FILTER day WHERE humidity >= 0 AND temp > -50;
NORMALIZE day.reading USING ZSCORE;
SELECT day [id, temp, humidity, reading, status];
SAVE day AS "day_clean.csv";
"""


def make_day(seed, rows=20000, drift=0.0):
    """One day of readings. `drift` moves the temperature, as seasons do."""
    rng = np.random.default_rng(seed)
    reading = rng.normal(5, 2, rows).round(3).astype(object)
    reading[rng.random(rows) < 0.02] = ""
    return pd.DataFrame({
        "id": np.arange(seed * rows, (seed + 1) * rows),
        "temp": (rng.normal(22 + drift, 4, rows)).round(2),
        "humidity": rng.integers(20, 90, rows),
        "reading": reading,
        "status": rng.choice(["ok", "warn", "fail"], rows, p=[0.9, 0.08, 0.02]),
    })


def workloads():
    """name -> (folder with the program and inputs, program file, [(table keys, input file)])."""
    tabular = ROOT / "examples" / "tabular"
    daily = WORK / "src_daily"
    daily.mkdir(parents=True, exist_ok=True)
    (daily / "daily.prep").write_text(DAILY_PROGRAM, encoding="utf-8")
    make_day(0).to_csv(daily / "day.csv", index=False)
    found = {
        "daily": (daily, "daily.prep", [(["day"], "day.csv")]),
        "students": (tabular, "students.prep", [(["students"], "students.csv")]),
        "optimize": (tabular, "optimize.prep", [(["top", "complete"], "students.csv")]),
        "ml_split": (tabular, "ml_split.prep", [(["train"], "train.csv"), (["test"], "test.csv")]),
        "tableset": (tabular, "tableset.prep", [([f"company.{name}"], f"company/{name}.csv")
                                                 for name in ("departments", "employees", "interns", "managers")]),
        "parquet": (tabular, "metadata_only.prep", [(["students"], "students.parquet")]),
    }
    images = ROOT / "examples" / "images"
    if (images / "dataset").is_dir():
        found["linked_images"] = (images, "linked.prep", [(["labels"], "labels.csv")])
    return found


def read_table(path):
    if path.suffix == ".parquet":
        return pd.read_parquet(path, dtype_backend="numpy_nullable")
    return pd.read_csv(path, keep_default_na=False, na_values=[""], dtype_backend="numpy_nullable")


def write_table(frame, path):
    if path.suffix == ".parquet":
        frame.to_parquet(path, index=False)
    else:
        frame.to_csv(path, index=False)


def numeric(frame):
    return [c for c in frame.columns
            if pd.api.types.is_numeric_dtype(frame[c]) and not pd.api.types.is_bool_dtype(frame[c])]


def batches(frame, seed):
    """Edits of one table, as new batches would bring them. name -> new frame."""
    rng = np.random.default_rng(seed)
    out = {"resampled": frame.sample(len(frame), replace=True, random_state=seed).reset_index(drop=True)}
    for name, scale in (("drift small", 0.1), ("drift medium", 0.5), ("drift large", 1.5)):
        moved = out["resampled"].copy()
        for column in numeric(moved):
            values = moved[column].astype("Float64")
            spread = float(values.std()) if values.notna().sum() > 1 and float(values.std()) > 0 else 1.0
            values = values + rng.normal(0, scale * spread, len(values))
            moved[column] = values.round(0) if pd.api.types.is_integer_dtype(frame[column]) else values.round(3)
            if pd.api.types.is_integer_dtype(frame[column]):
                moved[column] = moved[column].astype("Int64")
        out[name] = moved
    holes = frame.copy()
    column = holes.columns[rng.integers(len(holes.columns))]
    holes.loc[rng.random(len(holes)) < 0.2, column] = pd.NA
    out["missing values"] = holes
    if numeric(frame):
        spike = frame.copy()
        column = numeric(frame)[rng.integers(len(numeric(frame)))]
        spike.loc[rng.integers(len(spike)), column] = float(spike[column].max()) * 10 + 7
        if pd.api.types.is_integer_dtype(frame[column]):
            spike[column] = spike[column].round(0).astype("Int64")
        out["out of range"] = spike
    return out


def file_facts_admit(original, batch, columns):
    """The 'file facts' policy: every read column keeps its kind, stays without
    missing values if it had none, and stays within the compiled file's [min, max]."""
    for column in columns:
        if column not in batch.columns:
            return False
        a, b = original[column], batch[column]
        if pd.api.types.is_numeric_dtype(a) != pd.api.types.is_numeric_dtype(b):
            return False
        if a.notna().all() and b.isna().any():
            return False
        if pd.api.types.is_numeric_dtype(a) and not pd.api.types.is_bool_dtype(a) and b.notna().any():
            if float(b.min()) < float(a.min()) or float(b.max()) > float(a.max()):
                return False
    return True


def outputs(folder):
    """The files a run wrote (not the program or its manifest)."""
    files = {}
    for path in sorted(folder.rglob("*")):
        if path.is_file() and path.suffix in (".csv", ".parquet", ".npy"):
            files[path.relative_to(folder).as_posix()] = path.read_bytes()
    return files


def same_outputs(a, b):
    if a.keys() != b.keys():
        return False
    for name in a:
        if name.endswith(".parquet"):
            import io
            if not pd.read_parquet(io.BytesIO(a[name])).equals(pd.read_parquet(io.BytesIO(b[name]))):
                return False
        elif a[name] != b[name]:
            return False
    return True


def run_compiled(folder, keys, batch_path):
    output = folder / "output"
    for path in list(output.rglob("*")):
        if path.is_file() and path.name != "generated.py":
            path.unlink()
    args = [f"{key}={batch_path}" for key in keys]
    done = subprocess.run([PYTHON, str(output / "generated.py"), *args], capture_output=True, text=True)
    text = done.stdout + done.stderr
    if done.returncode == 0:
        return "finished", outputs(output)
    if "is not the file the program was compiled with" in text:
        return "refused", {}
    return "contract error", {}


def run_fresh(source_folder, program, relative, batch_path, fresh):
    if fresh.exists():
        shutil.rmtree(fresh)
    shutil.copytree(source_folder, fresh, ignore=shutil.ignore_patterns("output", "processed"))
    shutil.copyfile(batch_path, fresh / relative)
    done = subprocess.run([str(COMPILER), program, "--run"], cwd=fresh, capture_output=True, text=True)
    text = done.stdout + done.stderr
    if "Compilation failed" in text:
        return "compile error", {}
    if done.returncode != 0:
        return "contract error", {}
    return "finished", outputs(fresh / "output")


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, (source, program, groups) in workloads().items():
        folder = WORK / name
        if folder.exists():
            shutil.rmtree(folder)
        shutil.copytree(source, folder, ignore=shutil.ignore_patterns("output", "processed"))
        compiled = subprocess.run([str(COMPILER), program, "--python"], cwd=folder, capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stdout
        generated = (folder / "output" / "generated.py").read_text(encoding="utf-8")
        for keys, relative in groups:
            original = read_table(folder / relative)
            columns = [c for c in original.columns if f'"{c}"' in generated]
            for seed in range(SEEDS):
                edits = batches(original, seed)
                if name == "daily":  # other days of the same process
                    edits = {"another day": make_day(100 + seed), "another season": make_day(200 + seed, drift=12)}
                for kind, frame in edits.items():
                    batch = WORK / f"batch{Path(relative).suffix}"
                    write_table(frame, batch)
                    compiled_once, out_a = run_compiled(folder, keys, batch)
                    fresh, out_b = run_fresh(source, program, relative, batch, WORK / "fresh")
                    rows.append({
                        "workload": name, "table": "+".join(keys), "edit": kind, "seed": seed,
                        "compiled_once": compiled_once, "fresh": fresh,
                        "file_facts_admit": file_facts_admit(original, frame, columns),
                        "same_outputs": same_outputs(out_a, out_b) if compiled_once == fresh == "finished" else None,
                    })
                    print(name, keys, kind, seed, compiled_once, fresh, flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "certificates.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    write_markdown(rows)


def agrees(r):
    """The compiled-once run ends like the fresh one: same files, or both stopped by a contract."""
    if r["compiled_once"] == "finished":
        return r["fresh"] == "finished" and r["same_outputs"]
    return r["compiled_once"] == "contract error" and r["fresh"] in ("contract error", "compile error")


def write_markdown(rows):
    def share(items, test):
        return f"{sum(1 for r in items if test(r))}/{len(items)}"

    lines = ["# RQ7: reusing a compiled program on new data (data certificates)", "",
             "Each batch is run by the program compiled once (with `table=batch`) and, as the ground truth, "
             "compiled and run fresh for that batch.", "",
             "| Workload | Batches | Fingerprint admits | File facts admit | Certificate admits | "
             "Admitted and ends like fresh | Unsound |", "|---|---:|---:|---:|---:|---:|---:|"]
    for workload in dict.fromkeys(r["workload"] for r in rows):
        items = [r for r in rows if r["workload"] == workload]
        admitted = [r for r in items if r["compiled_once"] != "refused"]
        unsound = [r for r in items if r["compiled_once"] == "finished" and
                   (r["fresh"] != "finished" or not r["same_outputs"])]
        lines.append(f"| {workload} | {len(items)} | 0/{len(items)} | {share(items, lambda r: r['file_facts_admit'])} | "
                     f"{len(admitted)}/{len(items)} | "
                     f"{share(admitted, agrees)} | {len(unsound)} |")
    admitted = [r for r in rows if r["compiled_once"] != "refused"]
    unsound = [r for r in rows if r["compiled_once"] == "finished" and (r["fresh"] != "finished" or not r["same_outputs"])]
    lines += [f"| **all** | {len(rows)} | 0/{len(rows)} | {share(rows, lambda r: r['file_facts_admit'])} | "
              f"{len(admitted)}/{len(rows)} | "
              f"{share(admitted, agrees)} | {len(unsound)} |", ""]
    lines += ["## By kind of edit", "", "| Edit | Batches | File facts admit | Certificate admits | "
              "Refused, but a fresh compilation runs |", "|---|---:|---:|---:|---:|"]
    for kind in dict.fromkeys(r["edit"] for r in rows):
        items = [r for r in rows if r["edit"] == kind]
        lines.append(f"| {kind} | {len(items)} | {share(items, lambda r: r['file_facts_admit'])} | "
                     f"{share(items, lambda r: r['compiled_once'] != 'refused')} | "
                     f"{share(items, lambda r: r['compiled_once'] == 'refused' and r['fresh'] == 'finished')} |")
    lines += ["", "*Admitted* counts batches the certificate let through; a deferred or value-level check may "
              "still stop such a run, exactly as it stops the fresh compilation. *Unsound* would be a batch the "
              "compiled-once program finished on while the fresh compilation failed or wrote different files.", ""]
    (RESULTS / "certificates.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
