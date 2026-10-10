"""Admission policies for reusing a compiled program on a new batch, and how each one is scored.

Every policy ends a batch in one of two ways: it runs a program on the batch
(which finishes and writes files, or stops at a contract check) or it stops
without running (refusal: recompile). The ground truth is a fresh compilation of
the batch, run: it finishes with some files, or stops (compile error or contract
error). A policy's outcome on a batch is

    correct run     it finished and wrote exactly the fresh files
    correct stop    it stopped (refused, or a check failed) and so did the fresh run
    needless stop   it refused, but the fresh compilation finishes: a recompilation it did not need
    wrong stop      a check failed (or the program crashed) although the fresh run finishes
    unsound         it finished, but the fresh run stopped or wrote different files

Policies:
    fingerprint   only the byte-identical file (the guard before certificates)
    full          the full data certificate: every proof must hold (--no-deopt)
    deopt         the admission certificate, and deoptimization of each removal whose
                  item certificate the batch breaks (PrepLang's default)
    <validator>   a data-validation tool (file facts, pandera, gx, tfdv) decides; if it
                  admits, the optimized program runs on the batch as it is
"""
import io
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pandas as pd

PYTHON = sys.executable
OUTCOMES = ("correct run", "correct stop", "needless stop", "wrong stop", "unsound")


def outputs(folder):
    """The files a run wrote (not the program or its manifest)."""
    files = {}
    for path in sorted(Path(folder).rglob("*")):
        if path.is_file() and path.suffix in (".csv", ".parquet", ".npy"):
            files[path.relative_to(folder).as_posix()] = path.read_bytes()
    return files


def same_outputs(a, b):
    if a.keys() != b.keys():
        return False
    for name in a:
        if name.endswith(".parquet"):
            if not pd.read_parquet(io.BytesIO(a[name])).equals(pd.read_parquet(io.BytesIO(b[name]))):
                return False
        elif a[name] != b[name]:
            return False
    return True


def compile_variants(compiler, folder, program):
    """Compiles `program` in `folder` twice: output/generated.py (with deoptimization)
    and output/generated_full.py (--no-deopt). Returns the compiler output text."""
    full = subprocess.run([str(compiler), program, "--python", "--no-deopt"], cwd=folder, capture_output=True,
                          text=True)
    out = Path(folder) / "output"
    if full.returncode == 0:
        shutil.copyfile(out / "generated.py", out / "generated_full.py")
    done = subprocess.run([str(compiler), program, "--python"], cwd=folder, capture_output=True, text=True)
    return done.returncode == 0 and full.returncode == 0, done.stdout + done.stderr


def run_program(folder, script, inputs, trust=False):
    """Runs output/<script> with name=file arguments. Returns a dict with the status
    (finished, refused, contract error, crash), the reason, the deoptimized removals
    and the files written."""
    out = Path(folder) / "output"
    for path in list(out.rglob("*")):
        if path.is_file() and not path.name.startswith(("generated", "preplang_contract")):
            path.unlink()
    env = dict(os.environ, PREPLANG_TRUST_INPUTS="1" if trust else "0")
    args = [f"{name}={path}" for name, path in inputs.items()]
    done = subprocess.run([PYTHON, str(out / script), *args], capture_output=True, text=True, env=env)
    text = done.stdout + done.stderr
    deopts = [line[len("Deoptimized "):] for line in done.stdout.splitlines() if line.startswith("Deoptimized ")]
    if done.returncode == 0:
        return {"status": "finished", "reason": "", "deopts": deopts, "files": outputs(out)}
    reason = next((line for line in text.splitlines() if line.startswith("ContractError")), text.strip()[-300:])
    if "is not the input the program was compiled with" in text:
        status = "refused"
    elif "ContractError" in text:
        status = "contract error"
    else:
        status = "crash"
    return {"status": status, "reason": reason, "deopts": deopts, "files": {}}


def run_fresh(compiler, folder, program):
    """Compiles and runs `program` in `folder` (whose inputs are already the batch)."""
    done = subprocess.run([str(compiler), program, "--run"], cwd=folder, capture_output=True, text=True)
    text = done.stdout + done.stderr
    if "Compilation failed" in text:
        return {"status": "compile error", "files": {}}
    if done.returncode != 0:
        return {"status": "contract error", "files": {}}
    return {"status": "finished", "files": outputs(Path(folder) / "output")}


def score(policy, fresh):
    """The outcome of one policy run (a run_program result, or {"status": "refused"}) against the fresh run."""
    fresh_ok = fresh["status"] == "finished"
    status = policy["status"]
    if status == "finished":
        return "correct run" if fresh_ok and same_outputs(policy["files"], fresh["files"]) else "unsound"
    if status == "refused":
        return "needless stop" if fresh_ok else "correct stop"
    return "wrong stop" if fresh_ok else "correct stop"  # contract error or crash


def validator_outcome(admits, trusted, fresh):
    """A validator gates the optimized program: refused, or the trusted run."""
    return score(trusted if admits else {"status": "refused"}, fresh)


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


def run_validators(anchor, batches, columns, out_json, tools=("pandera", "gx", "tfdv")):
    """Runs eval/baselines.py for `tools` (pandera and gx in eval/.venv-baselines,
    tfdv in WSL) and returns {tool: {batch path: admit or None}} for both the
    whole files (scope "all") and only `columns` (scope "read")."""
    root = Path(__file__).resolve().parent.parent
    venv = root / "eval" / ".venv-baselines" / "Scripts" / "python.exe"
    decisions = {}
    for scope, extra in (("all", []), ("read", ["--columns", ",".join(columns)])):
        local = [t for t in tools if t != "tfdv"]
        reports = {}
        if local and venv.exists():
            target = Path(out_json).with_suffix(f".{scope}.local.json")
            subprocess.run([str(venv), str(root / "eval" / "baselines.py"), str(target), str(anchor),
                            *map(str, batches), "--tools", ",".join(local), *extra],
                           capture_output=True, text=True)
            if target.exists():
                reports.update(__import__("json").loads(target.read_text(encoding="utf-8")))
        if "tfdv" in tools and shutil.which("wsl"):
            # TFDV has no Windows build: it runs in WSL (Ubuntu, a Python 3.12 venv in ~/tfdv;
            # eval/tfdv.Dockerfile sets up the same with Docker).
            target = Path(out_json).with_suffix(f".{scope}.tfdv.json")
            linux = lambda p: "/mnt/" + str(Path(p).resolve())[0].lower() + str(Path(p).resolve())[2:].replace("\\", "/")  # noqa: E731
            subprocess.run(["wsl", "-d", "Ubuntu", "--", "bash", "-lc",
                            " ".join(["~/tfdv/bin/python", repr(linux(root / "eval" / "baselines.py")), repr(linux(target)),
                                      repr(linux(anchor)), *[repr(linux(b)) for b in batches], "--tools", "tfdv",
                                      *extra])],
                           capture_output=True, text=True)
            if target.exists():
                found = __import__("json").loads(target.read_text(encoding="utf-8"))
                results = found.get("tfdv", {})
                if "error" not in results:  # paths inside WSL -> Windows paths
                    back = {linux(b): str(Path(b).resolve()) for b in batches}
                    results = {back.get(k, k): v for k, v in results.items()}
                reports["tfdv"] = results
        for tool, results in reports.items():
            if "error" in results:
                decisions[f"{tool}/{scope}"] = {str(Path(b).resolve()): None for b in batches}
                continue
            by_path = {str(Path(k).resolve()): v for k, v in results.items()}
            decisions[f"{tool}/{scope}"] = {str(Path(b).resolve()): by_path.get(str(Path(b).resolve()), {}).get("admit")
                                            for b in batches}
    return decisions
