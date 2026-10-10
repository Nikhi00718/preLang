"""Data-validation tools as admission baselines.

    python eval/baselines.py OUT.json ANCHOR BATCH... [--tools pandera,gx,tfdv] [--columns a,b,c]

Each tool infers what valid data looks like from the anchor file (the file the
pipeline was compiled with), the way a data engineer would set it up, and then
validates every batch: admit if no expectation fails. With --columns only those
columns are given to the tools (the columns the compiled program reads), a
setting that favours the tools; without it they see the whole file.

    pandera  pandera.infer_schema: dtypes, nullability, numeric min/max
    gx       Great Expectations 0.18 onboarding data assistant (estimation="exact")
    tfdv     TensorFlow Data Validation: infer_schema + validate_statistics

pandera and gx run in eval/.venv-baselines (Python 3.11); tfdv runs in Docker
(see eval/tfdv.Dockerfile), because it has no Windows build. Writes one JSON
object: {tool: {batch path: {"admit": bool, "problems": [...], "seconds": s}}}.
"""
import json
import sys
import time
import warnings
from pathlib import Path

import pandas as pd

warnings.filterwarnings("ignore")


def read(path, columns):
    path = Path(path)
    frame = pd.read_parquet(path) if path.suffix == ".parquet" else pd.read_csv(path)
    if columns:
        frame = frame[[c for c in columns if c in frame.columns]]
    return frame


def pandera_tool(anchor, batches):
    import pandera.pandas as pa

    schema = pa.infer_schema(anchor)
    results = {}
    for name, frame in batches.items():
        start = time.perf_counter()
        problems = []
        try:
            schema.validate(frame, lazy=True)
        except pa.errors.SchemaErrors as error:
            cases = error.failure_cases
            problems = sorted({f"{row.column}: {row.check}" for row in cases.itertuples()})[:10]
        except Exception as error:  # a column missing, a dtype that cannot be coerced, ...
            problems = [f"{type(error).__name__}: {error}"[:200]]
        results[name] = {"admit": not problems, "problems": problems, "seconds": time.perf_counter() - start}
    return results


def gx_tool(anchor, batches):
    import great_expectations as gx

    context = gx.get_context(mode="ephemeral")
    asset = context.sources.add_pandas("baseline").add_dataframe_asset(name="frame")
    request = asset.build_batch_request(dataframe=anchor)
    assistant = context.assistants.onboarding.run(batch_request=request, estimation="exact")
    suite = assistant.get_expectation_suite(expectation_suite_name="anchor")
    results = {}
    for name, frame in batches.items():
        start = time.perf_counter()
        validator = context.get_validator(batch_request=asset.build_batch_request(dataframe=frame),
                                          expectation_suite=suite)
        outcome = validator.validate()
        problems = sorted({f"{r.expectation_config.kwargs.get('column', '')}: {r.expectation_config.expectation_type}"
                           for r in outcome.results if not r.success})[:10]
        results[name] = {"admit": bool(outcome.success), "problems": problems, "seconds": time.perf_counter() - start}
    return results


def tfdv_tool(anchor, batches):
    import tensorflow_data_validation as tfdv

    schema = tfdv.infer_schema(tfdv.generate_statistics_from_dataframe(anchor))
    results = {}
    for name, frame in batches.items():
        start = time.perf_counter()
        anomalies = tfdv.validate_statistics(statistics=tfdv.generate_statistics_from_dataframe(frame), schema=schema)
        problems = sorted(f"{feature}: {info.short_description}" for feature, info in anomalies.anomaly_info.items())
        results[name] = {"admit": not problems, "problems": problems[:10], "seconds": time.perf_counter() - start}
    return results


TOOLS = {"pandera": pandera_tool, "gx": gx_tool, "tfdv": tfdv_tool}


def run_jobs(jobs_file, out, tools, columns_scope):
    """Many anchors in one process: jobs_file holds [{"anchor": path, "batches": [...], "columns": [...]}].
    Writes {tool: {anchor: {batch: result}}}."""
    jobs = json.loads(Path(jobs_file).read_text(encoding="utf-8"))
    report = {tool: {} for tool in tools}
    for job in jobs:
        columns = job["columns"] if columns_scope == "read" else None
        try:
            anchor = read(job["anchor"], columns)
            batches = {path: read(path, columns) for path in job["batches"]}
        except Exception as error:  # a file the tool's environment cannot read: no decision
            for tool in tools:
                report[tool][job["anchor"]] = {"error": f"{type(error).__name__}: {error}"[:500]}
            continue
        for tool in tools:
            try:
                report[tool][job["anchor"]] = TOOLS[tool](anchor, batches)
            except Exception as error:
                report[tool][job["anchor"]] = {"error": f"{type(error).__name__}: {error}"[:500]}
        print("done", Path(job["anchor"]).name, file=sys.stderr, flush=True)
    Path(out).write_text(json.dumps(report, indent=1), encoding="utf-8")


def main():
    args = sys.argv[1:]
    if args and args[0] == "--jobs":  # --jobs JOBS.json OUT.json TOOLS all|read
        run_jobs(args[1], args[2], args[3].split(","), args[4])
        return
    tools, columns = ["pandera", "gx"], None
    if "--tools" in args:
        i = args.index("--tools")
        tools = args[i + 1].split(",")
        del args[i:i + 2]
    if "--columns" in args:
        i = args.index("--columns")
        columns = args[i + 1].split(",")
        del args[i:i + 2]
    out, anchor_path, batch_paths = args[0], args[1], args[2:]
    anchor = read(anchor_path, columns)
    batches = {path: read(path, columns) for path in batch_paths}
    report = {}
    for tool in tools:
        start = time.perf_counter()
        try:
            report[tool] = TOOLS[tool](anchor, batches)
        except Exception as error:
            report[tool] = {"error": f"{type(error).__name__}: {error}"[:500]}
        print(f"{tool}: {time.perf_counter() - start:.1f} s for {len(batches)} batches", file=sys.stderr)
    Path(out).write_text(json.dumps(report, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
