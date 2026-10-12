"""Writes paper/numbers.tex: every measured number in the paper, taken from eval/results/.

    py -3 eval/paper_numbers.py

The paper uses these macros instead of typed-in numbers, so it always matches the results.
"""
import json
import math
import statistics
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "eval" / "results"


def load(name):
    return json.loads((RESULTS / name).read_text(encoding="utf-8"))


def main():
    numbers = {}
    rq1 = load("rq1.json")
    ms = [row["preplang"]["ms"] for row in rq1]
    numbers.update(RqOneMsMin=f"{min(ms):.0f}", RqOneMsMax=f"{max(ms):.0f}", RqOneMsMed=f"{statistics.median(ms):.0f}")

    rq3 = load("rq3.json")
    t = rq3["table_optimizations"]
    process_opt, process_o0 = t["optimized_s"] - t["python_start_s"], t["O0_s"] - t["python_start_s"]
    numbers.update(TabOpt=f"{t['optimized_s']:.2f}", TabOz=f"{t['O0_s']:.2f}", TabBase=f"{t['baseline_s']:.2f}",
                   TabSpeedup=f"{t['speedup_vs_O0']:.2f}", TabStart=f"{t['python_start_s']:.2f}",
                   TabProcOpt=f"{process_opt:.2f}", TabProcOz=f"{process_o0:.2f}",
                   TabCut=f"{100 * (1 - process_opt / process_o0):.0f}")
    i = rq3["image_streaming"]
    numbers.update(ImgPl=f"{i['preplang_s']:.2f}", ImgPlMem=f"{i['preplang_peak_mib']:.0f}",
                   ImgBase=f"{i['baseline_s']:.2f}", ImgBaseMem=f"{i['baseline_peak_mib']:.0f}",
                   ImgHw=f"{i['handwritten_s']:.2f}", ImgHwMem=f"{i['handwritten_peak_mib']:.0f}")
    f = rq3["resize_fusion"]
    numbers.update(FusExact=f"{f['exact_s']:.2f}", FusApprox=f"{f['approx_s']:.2f}",
                   FusSave=f"{100 * (1 - f['approx_s'] / f['exact_s']):.0f}", FusPsnr=f"{f['mean_psnr_db']:.1f}",
                   FusPsnrMin=f"{f['min_psnr_db']:.1f}")
    names = ["CsvTenK", "CsvHundredK", "CsvMillion", "ImgHundred", "ImgThousand", "ImgThreeK"]
    for name, row in zip(names, rq3["compile_cost"]):
        numbers[name] = f"{row['compile_s']:.2f}"
        numbers[name + "Read"] = f"{row['read_all_s']:.2f}"
    million = rq3["compile_cost"][2]
    numbers["CsvRatio"] = f"{million['compile_s'] / million['read_all_s']:.1f}"
    numbers["CsvSpeedup"] = f"{million['read_all_s'] / million['compile_s']:.1f}"
    numbers["CsvArrowRead"] = f"{million['read_arrow_s']:.2f}"
    numbers["CsvArrow"] = f"{million['read_arrow_s'] / million['compile_s']:.1f}"
    numbers["HdrRatio"] = f"{rq3['compile_cost'][5]['read_all_s'] / rq3['compile_cost'][5]['compile_s']:.0f}"

    scale = load("scale.json")
    for name, row in zip(["PqOne", "PqFour", "PqSixteen"], scale["parquet_compile_cost"]):
        numbers[name] = f"{row['compile_s']:.3f}"
        numbers[name + "Read"] = f"{row['read_all_s']:.2f}"
        numbers[name + "Size"] = f"{row['mib']:.0f}"
    for name, row in zip(["ScTabOk", "ScTabName", "ScTabContract", "ScImgOk", "ScImgBroken", "ScImgFloat"],
                         scale["table_cases"] + scale["image_cases"]):
        numbers[name + "Pl"] = f"{row['preplang_compile_s']:.2f}"
        numbers[name + "Script"] = f"{row['script_s']:.2f}"
        if "preplang_run_s" in row:
            numbers[name + "Run"] = f"{row['preplang_run_s']:.2f}"

    bugs = load("realbugs.json")
    now, before = Counter(b["verdict"] for b in bugs), Counter(b["before"] for b in bugs)
    early, early_before = now["compile"] + now["contract"], before["compile"] + before["contract"]
    cases = [b for b in bugs if b["case"]]
    reasons = Counter(b["note"].split(":")[0] for b in bugs if b["verdict"] == "none")
    numbers.update(RbTotal=len(bugs), RbEarly=early, RbEarlyPct=f"{100 * early / len(bugs):.0f}",
                   RbCompile=now["compile"], RbContract=now["contract"], RbDesign=now["design"], RbNone=now["none"],
                   RbBefore=early_before, RbBeforePct=f"{100 * early_before / len(bugs):.0f}",
                   RbCases=len(cases), RbPass=sum(b["reproduced"] for b in cases),
                   RbFormat=reasons["input format"], RbOperation=reasons["operation not in the language"],
                   RbDates=reasons["dates"], RbLabels=reasons["per-image label files"],
                   RbProduced=reasons["input produced during the run"], RbContractMissing=reasons["no suitable contract"],
                   RbModel=reasons["model code"])

    certificates(load("certificates.json"), numbers)
    realbatches(load("realbatches.json"), numbers)
    corpora(numbers)
    drift_corpora(numbers)
    if (RESULTS / "composition.json").exists():
        composition(load("composition.json"), numbers)
    if (RESULTS / "overhead.json").exists():
        overhead(load("overhead.json"), numbers)
    if (RESULTS / "imagebatches.json").exists():
        imagebatches(load("imagebatches.json"), numbers)
    if (RESULTS / "certcost.json").exists():
        certcost(load("certcost.json"), numbers)
    if (RESULTS / "certsize.json").exists():
        certsize(load("certsize.json"), numbers)
    if (RESULTS / "private.json").exists() and (RESULTS / "private_diagnostics.json").exists():
        private(load("private.json"), load("private_diagnostics.json"), numbers)
    soundness_tests(numbers)

    numbers["TestCount"] = test_count()
    lean = {name: len((ROOT / "proofs" / f"{name}.lean").read_text(encoding="utf-8").splitlines())
            for name in ("Deopt", "Core")}
    numbers.update(LeanLines=sum(lean.values()), LeanDeoptLines=lean["Deopt"], LeanCoreLines=lean["Core"])
    numbers["SrcLines"] = f"{sum(len(f.read_text(encoding='utf-8').splitlines()) for f in (ROOT / 'src').rglob('*.[ch]pp')):,}"
    numbers["RealDeoptCorrectPct"] = numbers.get("RealDeoptCorrectPct", "--")

    for table, label in (("certtable", "tab:rq-edits"), ("realtable", "tab:rq-real"),
                         ("corpustable", "tab:rq-corpus"), ("comptable", "tab:rq-compose"),
                         ("privatetable", "tab:rq-private"), ("privatediagtable", "tab:rq-private-diag")):
        path = ROOT / "paper" / "generated" / f"{table}.tex"
        if not path.exists():  # its experiment has not run yet
            path.write_text(r"\begin{table}[t]\caption{Results pending.}\label{" + label + r"}\end{table}" + "\n",
                            encoding="utf-8")
            print("not in the results yet:", table)

    # A macro the paper uses that no result defines is printed as ?? and reported.
    import re
    used = set()
    for tex in (ROOT / "paper").rglob("*.tex"):
        if tex.name not in ("numbers.tex", "preplang_ieee_v1.tex"):
            used |= set(re.findall(r"\\([A-Z][A-Za-z]+)(?=[{\\\s,.%)\-]|$)", tex.read_text(encoding="utf-8")))
    known = {"PL", "Leftrightarrow", "Rightarrow", "Leftarrow", "Latex", "LaTeX", "TeX", "Large", "Huge", "LARGE"}
    missing = sorted(name for name in used - set(numbers) - known)
    for name in missing:
        numbers[name] = r"\textbf{??}"
    if missing:
        print("not in the results yet:", ", ".join(missing))
    lines = ["% Generated by eval/paper_numbers.py from eval/results/. Do not edit."]
    lines += [f"\\newcommand{{\\{name}}}{{{value}}}" for name, value in numbers.items()]
    (ROOT / "paper" / "generated" / "numbers.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote paper/generated/numbers.tex ({len(numbers)} numbers)")


def test_count():
    """Unit tests that pass, counted by running every test program of the Release build."""
    import subprocess
    total = 0
    for test in sorted((ROOT / "build" / "Release").glob("test_*.exe")):
        done = subprocess.run([str(test)], cwd=ROOT / "build", capture_output=True, text=True)
        total += done.stdout.count("  PASS  ")
    return total


POLICIES = [  # (key in "outcome", macro name, table label)
    ("fingerprint", "Fp", "Fingerprint"),
    ("file facts", "Facts", "File facts"),
    ("pandera/all", "PanderaAll", "pandera, every column"),
    ("pandera/read", "PanderaRead", "pandera, columns read"),
    ("gx/all", "GxAll", "Great Expectations, every column"),
    ("gx/read", "GxRead", "Great Expectations, columns read"),
    ("tfdv/all", "TfdvAll", "TFDV, every column"),
    ("tfdv/read", "TfdvRead", "TFDV, columns read"),
    ("full", "Full", "Full certificate"),
    ("deopt", "Deopt", "Selective deoptimization"),
]


def tally(rows, policy):
    """(correct, needless stops, unsound or wrong stops, scored) for one policy; None if it was not run."""
    outcomes = [r["outcome"].get(policy) for r in rows]
    scored = [o for o in outcomes if o is not None]
    if not scored:
        return None
    correct = sum(o in ("correct run", "correct stop") for o in scored)
    needless = sum(o == "needless stop" for o in scored)
    wrong = sum(o in ("unsound", "wrong stop") for o in scored)
    return correct, needless, wrong, len(scored)


def policy_numbers(rows, prefix, numbers):
    for key, name, _ in POLICIES:
        found = tally(rows, key)
        if found:
            correct, needless, wrong, scored = found
            numbers[f"{prefix}{name}Correct"] = correct
            numbers[f"{prefix}{name}Needless"] = needless
            numbers[f"{prefix}{name}Unsound"] = wrong
            numbers[f"{prefix}{name}CorrectPct"] = f"{100 * correct / scored:.0f}"
            numbers[f"{prefix}{name}NeedlessPct"] = f"{100 * needless / scored:.0f}"


def policy_table(rows, groups, caption, label, path):
    """A table* with one row per policy and correct / needless / unsound per group of batches."""
    header = " & ".join(rf"\multicolumn{{3}}{{c}}{{{title}}}" for title, _ in groups)
    rules = "".join(rf"\cmidrule(l){{{2 + 3 * i}-{4 + 3 * i}}}" for i in range(len(groups)))
    lines = [r"\begin{table*}[t]", rf"\caption{{{caption}}}", rf"\label{{{label}}}", r"\centering", r"\footnotesize",
             r"\begin{tabular}{@{}l" + "rrr" * len(groups) + "@{}}", r"\toprule",
             rf"& {header} \\", rules, "Policy & " + " & ".join(["ok", "ref.", "wrong"] * len(groups)) + r" \\",
             r"\midrule"]
    for key, _, title in POLICIES:
        cells = []
        for _, test in groups:
            found = tally([r for r in rows if test(r)], key)
            cells += [str(v) for v in found[:3]] if found else ["--"] * 3
        if all(c == "--" for c in cells):
            continue
        bold = key == "deopt"
        lines.append((rf"\textbf{{{title}}}" if bold else title) + " & " +
                     " & ".join(rf"\textbf{{{c}}}" if bold else c for c in cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table*}"]
    (ROOT / "paper" / "generated" / path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def certificates(rows, numbers):
    """RQ7: synthetic edits of the example workloads."""
    numbers.update(CertBatches=len(rows), CertDeopted=sum(bool(r.get("deopts")) for r in rows),
                   CertRescued=sum(r.get("full") == "refused" and r.get("deopt") != "refused" for r in rows))
    policy_numbers(rows, "Cert", numbers)
    groups = [("all batches", lambda r: True)]
    for workload in dict.fromkeys(r["workload"] for r in rows):
        pass
    policy_table(rows, [("tables", lambda r: r["workload"] != "linked_images"),
                        ("images + labels", lambda r: r["workload"] == "linked_images"),
                        ("all", lambda r: True)],
                 "Reusing compiled programs on edited batches (RQ3): per policy, batches handled correctly "
                 "(ok: the fresh files, or a stop where the fresh run stops), refused although a fresh "
                 "compilation runs (ref.), and finished with other files or where the fresh run fails (wrong).",
                 "tab:rq-edits", "certtable.tex")


def realbatches(rows, numbers):
    """RQ8: real monthly batches."""
    rows = [r for r in rows if r["fresh"] is not None]
    numbers.update(RealRuns=len(rows), RealDeopted=sum(bool(r["deopts"]) for r in rows),
                   RealRescued=sum(r["full"] == "refused" and r["deopt"] != "refused" for r in rows),
                   RealFreshFails=sum(r["fresh"] != "finished" for r in rows),
                   RealTypeRefusals=sum(r["deopt"] == "refused" and "is Int64, not" in r["reason"] for r in rows))
    policy_numbers(rows, "Real", numbers)
    stops = [100 * tally(rows, key)[1] / tally(rows, key)[3] for key, _, _ in POLICIES[2:8]
             if tally(rows, key) and tally(rows, key)[2] == 0]
    numbers["RealValidatorStopMin"] = f"{min(stops):.0f}" if stops else "--"
    numbers["RealValidatorStopMax"] = f"{max(stops):.0f}" if stops else "--"
    numbers["RealValidatorWrongMax"] = max([tally(rows, key)[2] for key, _, _ in POLICIES[2:8] if tally(rows, key)]
                                           or [0])
    names = {"traffic": "Traffic (CSV)", "bikes": "Bikes (CSV)", "taxi": "Taxi (Parquet)"}
    policy_table(rows, [(names[w], (lambda w: lambda r: r["workload"] == w)(w)) for w in names] +
                 [(f"all {len(rows)}", lambda r: True)],
                 "Reusing compiled pipelines on real monthly batches (RQ4), both deployments (compiled on the first "
                 "month, and on the month before). ok: handled as a fresh compilation of the month would; ref.: refused "
                 "although a fresh compilation runs; wrong: finished with other files than the fresh run, or where it "
                 "fails. Validators gate the optimized program (Section~\\ref{sec:rq-real}).",
                 "tab:rq-real", "realtable.tex")
    timeline(rows)


TIMELINE_STYLES = {  # outcome -> TikZ style of a month's cell
    "rerun": "fill=deopt, draw=deopt", "correct run": "fill=black!65, draw=black!65",
    "correct stop": "fill=white, draw=black, line width=0.5pt", "needless stop": "fill=black!15, draw=black!15",
    "unsound": "pattern=north east lines, pattern color=black, draw=black",
}


def timeline(rows):
    """Figure: every later month of the first-month deployment, per policy, on a calendar axis."""
    policies = (("TFDV read", "tfdv/read"), ("Full cert.", "full"), ("Deopt.", "deopt"))
    names = {"traffic": "Traffic", "bikes": "Bikes", "taxi": "Taxi"}
    cell, gap, label = 0.215, 0.035, 1.55  # cm
    lines = [r"\begin{figure*}[t]", r"\centering", r"\definecolor{deopt}{RGB}{217,95,2}",
             r"\begin{tikzpicture}[font=\scriptsize]"]
    y = 0.0
    for workload, title in names.items():
        months = sorted((r for r in rows if r["workload"] == workload and r["mode"] == "first"),
                        key=lambda r: r["batch"])
        if not months:
            continue
        first_year, first_month = map(int, months[0]["batch"].split("-"))

        def x(batch):
            year, month = map(int, batch.split("-"))
            return label + ((year - first_year) * 12 + month - first_month) * cell

        lines.append(rf"\node[anchor=west, inner sep=0pt, font=\scriptsize\bfseries] at (0,{y:.3f}) {{{title}}};")
        years = sorted({r["batch"][:4] for r in months})
        for year in years:
            if int(year) == first_year and first_month > 9:  # too few months to hold the label
                continue
            start = max(x(f"{year}-01"), x(months[0]["batch"]))
            lines.append(rf"\node[anchor=west, inner sep=0pt, text=black!60] at ({start:.3f},{y:.3f}) {{{year}}};")
        y -= 0.13
        for name, key in policies:
            y -= cell + gap
            lines.append(rf"\node[anchor=east] at ({label - 0.08:.3f},{y + cell / 2:.3f}) {{{name}}};")
            for r in months:
                outcome = r["outcome"][key]
                if outcome == "wrong stop":
                    outcome = "unsound"
                if key == "deopt" and outcome == "correct run" and r["deopts"]:
                    outcome = "rerun"
                left = x(r["batch"])
                lines.append(rf"\path[{TIMELINE_STYLES[outcome]}] ({left:.3f},{y:.3f}) rectangle "
                             rf"++({cell - gap:.3f},{cell:.3f});")
        y -= 0.42
    legend = (("correct run", "ran, wrote the fresh compilation's files"),
              ("rerun", "ran with removals re-enabled, as fresh"),
              ("correct stop", "stopped where the fresh compilation stops"),
              ("needless stop", "refused although fresh runs"), ("unsound", "wrong output"))
    lines.append(r"\matrix[anchor=north west, column sep=2pt, ampersand replacement=\&] at "
                 rf"({label:.3f},{y + 0.25:.3f}) {{")
    lines.append(" \\& ".join(rf"\path[{TIMELINE_STYLES[k]}] (0,0) rectangle ++({cell - gap:.3f},{cell:.3f}); \& "
                              rf"\node[anchor=west, inner sep=1pt] {{{text}}};" for k, text in legend) + r" \\")
    lines += ["};", r"\end{tikzpicture}",
              r"\caption{RQ4, compiled on the first month: every later month on a calendar axis (the traffic data has "
              r"a gap), for the best validator, the full certificate and selective deoptimization. White: a fresh "
              r"compilation stops too (temperatures of 0\,K in 2014, taxi fares above \$1,000). The grey run in the "
              r"taxi rows begins in February 2023, when the publisher changed two column types; TFDV admits those "
              r"months, and the optimized program writes them with the old types.}", r"\label{fig:timeline}", r"\end{figure*}"]
    (ROOT / "paper" / "generated" / "figure-timeline.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def corpus_numbers(records, prefix, numbers, held_out_from):
    test = [r for r in records if r.get("rank", 0) > held_out_from] if held_out_from else records
    if held_out_from:
        test = [r for i, r in enumerate(records) if i >= held_out_from]
    lifted = [r for r in test if r["outcome"] in ("lifted", "compile error")]
    runs = [b for r in test for b in r.get("batches", [])]
    deopt_admit = [b for b in runs if b["prep"] != "refused"]
    full_admit = [b for b in runs if b.get("prep_full") != "refused"]
    numbers.update({
        f"{prefix}All": len(records), f"{prefix}Test": len(test), f"{prefix}Lifted": len(lifted),
        f"{prefix}Compiled": sum(r["outcome"] == "lifted" for r in test),
        f"{prefix}Statements": sum(r["lifted"] for r in lifted), f"{prefix}Changes": sum(r["changes"] for r in lifted),
        f"{prefix}Complete": sum(r["complete"] for r in lifted), f"{prefix}Runs": len(runs),
        f"{prefix}Admit": len(deopt_admit), f"{prefix}AdmitPct": f"{100 * len(deopt_admit) / max(1, len(runs)):.0f}",
        f"{prefix}Same": sum(b["prep"] == "finished" and bool(b["same"]) for b in runs),
        f"{prefix}Unsound": sum(b["prep"] == "finished" and not b["same"] for b in runs),
        f"{prefix}Stopped": sum(b["prep"] == "contract error" for b in runs),
        f"{prefix}StoppedRuns": sum(b["prep"] == "contract error" and b["script"] == "runs" for b in runs),
        f"{prefix}RefusedCrash": sum(b["prep"] == "refused" and b["script"] == "crashes" for b in runs),
        f"{prefix}RefusedRuns": sum(b["prep"] == "refused" and b["script"] == "runs" for b in runs),
        f"{prefix}Deopted": sum(bool(b.get("deopts")) for b in runs),
        f"{prefix}FullAdmit": len(full_admit),
        f"{prefix}FullSame": sum(b.get("prep_full") == "finished" and bool(b.get("same_full")) for b in runs),
        f"{prefix}FullUnsound": sum(b.get("prep_full") == "finished" and not b.get("same_full") for b in runs),
        f"{prefix}FullRefusedRuns": sum(b.get("prep_full") == "refused" and b["script"] == "runs" for b in runs),
    })
    return test, runs


DRIFT = (("traffic", "Traffic", "development"), ("bikes", "Bikes, daily", "held out"),
         ("hours", "Bikes, hourly", "held out"))


def drift_numbers(records):
    """Scripts by other people, compiled on the first month and run on every later month (RQ5)."""
    lifted = [r for r in records if r["outcome"] in ("lifted", "compile error")]
    runs = [m for r in records for m in r.get("months", [])]
    count = lambda test: sum(1 for m in runs if test(m))  # noqa: E731
    statements = [line.split()[0] for r in lifted for line in r.get("program", "").splitlines()]
    return {
        "All": len(records), "Lifted": len(lifted), "Compiled": sum(r["outcome"] == "lifted" for r in records),
        "CompileErrors": sum(r["outcome"] == "compile error" for r in records),
        "Statements": sum(r["lifted"] for r in lifted), "Derive": statements.count("DERIVE"),
        "WithRemovals": sum(bool(r.get("removals")) for r in records),
        "DatesAdmitted": sum(bool(r.get("dates_admitted")) for r in records),
        "ParseDate": statements.count("PARSE_DATE"), "Runs": len(runs),
        "Same": count(lambda m: m["prep"] == "finished" and m["same"]),
        "Wrong": count(lambda m: (m["prep"] == "finished" and not m["same"]) or
                       (m["prep"] == "contract error" and m["script"] == "runs")),
        "Stops": count(lambda m: m["prep"] != "finished" and m["script"] == "crashes"),
        "Needless": count(lambda m: m["prep"] == "refused" and m["script"] == "runs"),
        "Deopted": count(lambda m: bool(m.get("deopts"))),
        "FullSame": count(lambda m: m["prep_full"] == "finished" and m["same_full"]),
        "FullWrong": count(lambda m: m["prep_full"] == "finished" and not m["same_full"]),
        "FullNeedless": count(lambda m: m["prep_full"] == "refused" and m["script"] == "runs"),
    }


def drift_corpora(numbers):
    found = [(key, title, role, drift_numbers(load(f"drift_corpus_{key}.json"))) for key, title, role in DRIFT
             if (RESULTS / f"drift_corpus_{key}.json").exists()]
    if not found:
        return
    total = {k: sum(n[k] for _, _, _, n in found) for k in found[0][3]}
    held = {k: sum(n[k] for key, _, role, n in found if role == "held out") for k in found[0][3]}
    for key, _, _, n in found:
        numbers.update({f"Drift{key.capitalize()}{k}": v for k, v in n.items()})
    numbers.update({f"Drift{k}": v for k, v in total.items()})
    numbers.update({f"DriftHeld{k}": v for k, v in held.items()})
    rows = [("GitHub files", "All"), ("lifted, in scope", "Lifted"), ("compiled on the first month", "Compiled"),
            (r"\quad with a removal that can run again", "WithRemovals"),
            ("statements lifted", "Statements"), (r"\quad of them \texttt{DERIVE}", "Derive"),
            (r"\quad of them \texttt{PARSE\_DATE}", "ParseDate"), ("runs on later months", "Runs"),
            (r"\emph{With deoptimization:} as the script", "Same"),
            (r"\quad stopped where the script fails", "Stops"), (r"\quad refused, script runs", "Needless"),
            (r"\quad different (wrong)", "Wrong"), (r"\quad ran with removals re-enabled", "Deopted"),
            (r"\emph{Full certificate:} as the script", "FullSame"), (r"\quad refused, script runs", "FullNeedless"),
            (r"\quad different (wrong)", "FullWrong")]
    lines = [r"\begin{table}[t]", r"\caption{Other people's pandas scripts for the monthly traffic and bike data "
             r"(RQ5): lifted with derived columns and dates, compiled on the first month and run on every later "
             r"month; the ground truth is the lifted pandas statements run on the same month. The traffic scripts "
             r"were the development set of the second lifter version.}", r"\label{tab:rq-drift}", r"\centering",
             r"\footnotesize", r"\setlength{\tabcolsep}{4pt}",r"\begin{tabular}{@{}l" + "r" * len(found) + "@{}}", r"\toprule",
             " & " + " & ".join(f"{title}" for _, title, _, _ in found) + r" \\",
             " & " + " & ".join(f"({role})" for _, _, role, _ in found) + r" \\", r"\midrule"]
    for label, key in rows:
        lines.append(label + " & " + " & ".join(f"{n[key]:,}" for _, _, _, n in found) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    (ROOT / "paper" / "generated" / "drifttable.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def corpora(numbers):
    rows = []
    both_scripts = both_runs = both_same = both_lifted = 0
    for name, file, prefix, held in (("Titanic", "pandas_corpus.json", "Corpus", 100),
                                     ("House Prices", "pandas_corpus_houses.json", "Houses", 0)):
        if not (RESULTS / file).exists():
            continue
        test, runs = corpus_numbers(load(file), prefix, numbers, held)
        both_scripts += len(test)
        both_runs += len(runs)
        both_same += numbers[f"{prefix}Same"]
        both_lifted += numbers[f"{prefix}Compiled"]
        rows.append((name, prefix))
    numbers.update(CorpusBothScripts=both_scripts, CorpusBothRuns=both_runs, CorpusBothSame=both_same,
                   CorpusBothLifted=both_lifted)
    # The same held-out scripts lifted again with the second lifter version (dates, derived columns).
    derived = {"Runs": 0, "Same": 0, "Unsound": 0, "Statements": 0, "Compiled": 0, "Deopted": 0}
    for file, prefix, held in (("pandas_corpus_derived.json", "CorpusDer", 100),
                               ("pandas_corpus_houses_derived.json", "HousesDer", 0)):
        if (RESULTS / file).exists():
            corpus_numbers(load(file), prefix, numbers, held)
            for key in derived:
                derived[key] += numbers[prefix + key]
    numbers.update({"CorpusBothDer" + key: value for key, value in derived.items()})
    numbers["CorpusBothStatements"] = numbers.get("CorpusStatements", 0) + numbers.get("HousesStatements", 0)
    lines = [r"\begin{table}[t]", r"\caption{Real pandas scripts from GitHub (RQ5): held-out scripts, lifted, "
             r"compiled once on the training file and run on new batches; the ground truth is the lifted pandas "
             r"statements run on the same batch.}", r"\label{tab:rq-corpus}", r"\centering", r"\footnotesize",
             r"\begin{tabular}{@{}l" + "r" * len(rows) + "@{}}", r"\toprule",
             "& " + " & ".join(name for name, _ in rows) + r" \\", r"\midrule"]
    for label, key in (("Held-out scripts", "Test"), ("Lifted, in scope", "Lifted"), ("Compiled", "Compiled"),
                       ("Runs on new batches", "Runs"), (r"\emph{Full certificate:} admitted", "FullAdmit"),
                       (r"\quad identical to the script", "FullSame"),
                       (r"\quad refused, script runs", "FullRefusedRuns"),
                       (r"\emph{With deoptimization:} admitted", "Admit"),
                       (r"\quad identical to the script", "Same"), (r"\quad stopped by a check", "Stopped"),
                       (r"\quad different (unsound)", "Unsound"), (r"\quad refused, script runs", "RefusedRuns"),
                       (r"\quad ran with removals re-enabled", "Deopted")):
        lines.append(label + " & " + " & ".join(str(numbers[prefix + key]) for _, prefix in rows) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    (ROOT / "paper" / "generated" / "corpustable.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def composition(results, numbers):
    lines = [r"\begin{table}[t]", r"\caption{Composing two compiled stages on real monthly data (RQ6). Facts: what "
             r"stage~2 relies on (admission and removals); Guar.: implied by stage~1's guarantee; Months: run through "
             r"both stages; Deopt.: months stage~2 deoptimized; by a guaranteed fact: never.}",
             r"\label{tab:rq-compose}", r"\centering", r"\footnotesize", r"\begin{tabular}{@{}llrrrrr@{}}", r"\toprule",
             r"Data & Stage 1 & Facts & Guar. & Months & Deopt. & Guar.\ broken \\", r"\midrule"]
    total_broken = total_months = 0
    written = [r for r in results if not r["strengthened"]]
    numbers["ComposeFacts"] = sum(link.get("facts", 0) for r in written for link in r["composition"].get("links", []))
    numbers["ComposeGuaranteed"] = sum(link.get("guaranteed", 0) for r in written
                                       for link in r["composition"].get("links", []))
    numbers["ComposeStageOneStops"] = sum(m["stage1"] != "finished" for r in written for m in r["months"])
    numbers["ComposeLikeFresh"] = sum(m.get("outcome") in ("correct run", "correct stop")
                                      for r in results for m in r["months"] if m.get("stage2"))
    for r in results:
        links = r["composition"].get("links", [{}])
        facts = sum(link.get("facts", 0) for link in links)
        held = sum(link.get("guaranteed", 0) for link in links)
        through = [m for m in r["months"] if m.get("stage2")]
        broken = sum(m["guaranteed_fact_broken"] for m in through)
        total_broken += broken
        total_months += len(through)
        lines.append(f"{r['workload']} & {'strengthened' if r['strengthened'] else 'as written'} & {facts} & {held} & "
                     f"{len(through)} & {sum(bool(m['stage2_deopts']) for m in through)} & {broken} \\\\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    (ROOT / "paper" / "generated" / "comptable.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    numbers.update(ComposeMonths=total_months, ComposeBroken=total_broken)


def overhead(rows, numbers):
    finished = [r for r in rows if r["trusted_status"] == r["deopt_status"] == r["full_status"] == "finished"]
    if not finished:
        return
    med = lambda key: statistics.median(r[key] for r in finished)  # noqa: E731
    ratios = sorted(100 * (r["deopt"] - r["trusted"]) / r["trusted"] for r in finished)
    low, _, high = statistics.quantiles(ratios, n=4)
    numbers.update(OhMonths=len(finished), OhTrusted=f"{med('trusted'):.2f}", OhFull=f"{med('full'):.2f}",
                   OhDeopt=f"{med('deopt'):.2f}", OhPct=f"${statistics.median(ratios):+.0f}$",
                   OhPctLow=f"${low:+.0f}$", OhPctHigh=f"${high:+.0f}$", OhCompile=f"{med('compile'):.2f}",
                   OhCompileMs=f"{1000 * med('compile'):.0f}", OhFresh=f"{med('fresh'):.2f}")


def certcost(rows, numbers):
    """Re-analyses and compile time of certification, with and without deoptimization."""
    deopt = [r["deopt"]["analyses"] for r in rows]
    full = [r["full"]["analyses"] for r in rows]
    ms = [1000 * r[k]["seconds"] for r in rows for k in ("deopt", "full")]
    numbers.update(CcPrograms=len(rows), CcAnalysesMin=min(deopt), CcAnalysesMax=max(deopt),
                   CcFullAnalysesMax=max(full), CcMsMin=f"{min(ms):.0f}", CcMsMax=f"{max(ms):.0f}",
                   CcRatioMax=f"{max(d / f for d, f in zip(deopt, full) if f):.1f}")


def certsize(rows, numbers):
    """Facts in the full, admission and item certificates of the table programs."""
    items = [n for r in rows for n in r["items"]]
    numbers.update(CsPrograms=len(rows), CsFullMin=min(r["full"] for r in rows), CsFullMax=max(r["full"] for r in rows),
                   CsItems=len(items), CsItemMin=min(items), CsItemMax=max(items),
                   CsAdmissionTypesOnly="yes" if all(r["admission"] == r["columns"] for r in rows) else "no")
    for r in rows:
        if r["program"] in ("traffic", "bikes", "taxi"):
            name = r["program"].capitalize()
            numbers[f"Cs{name}Cols"], numbers[f"Cs{name}FileCols"] = r["columns"], r["file_columns"]


def imagebatches(rows, numbers):
    numbers["ImgBatches"] = len(rows)
    policy_numbers(rows, "ImgB", numbers)
    numbers["ImgBTrustedUnsound"] = sum(r["outcome"]["trusted"] in ("unsound", "wrong stop") for r in rows)
    numbers["ImgBDeopted"] = sum(bool(r["deopts"]) for r in rows)


def private(results, diagnostics, numbers):
    """RQ8 of the paper: private compilation and its baselines (eval/reuse/private.py)."""
    import re
    import subprocess
    records = results["records"]
    runs = [run for r in records for run in r["runs"]]
    numbers.update(PrivCompilations=f"{len(records):,}", PrivRuns=f"{len(runs):,}",
                   PrivUnsound=sum(run["outcome"] in ("unsound", "wrong stop") for run in runs),
                   PrivNeedless=sum(run["outcome"] == "needless stop" for run in runs),
                   PrivRandom=sum(r["kind"] == "random" for r in records),
                   PrivRandomRuns=f"{sum(len(r['runs']) for r in records if r['kind'] == 'random'):,}",
                   PrivTitanicScripts=len({r["item"] for r in records if r["workload"] == "titanic"}))
    numbers["PrivNeedlessTaxi"] = sum(run["outcome"] == "needless stop" for r in records if r["item"] == "taxi"
                                      for run in r["runs"])

    def workload(r):
        return r["item"] if r["workload"] == "rq8" else r["workload"]

    def overlap(chosen):
        found = sum(len(set(r["removed"]) & set(r["normal_removed"])) for r in chosen if r["compiled"])
        made = sum(len(r["removed"]) for r in chosen if r["compiled"])
        normal = sum(len(r["normal_removed"]) for r in chosen if r["compiled"])
        return found, made, normal

    def recall(chosen):
        found, _, normal = overlap(chosen)
        return 100 * found / normal if normal else None

    def precision(chosen):
        found, made, _ = overlap(chosen)
        return 100 * found / made if made else None

    def pct(value):
        return "--" if value is None else f"{value:.0f}"

    def of(kind, key=None):
        return [r for r in records if r["kind"] == kind and (key is None or workload(r) == key)]

    numbers["PrivExactRecall"] = pct(recall(of("exact")))
    for kind, name in (("eps=1", "One"), ("eps=10", "Ten")):
        numbers[f"PrivTitanicDeoptEps{name}"] = sum(any(run["anchor"] and run["deopts"] for run in r["runs"])
                                                    for r in of(kind, "titanic"))
        for key in ("traffic", "bikes", "taxi", "titanic"):
            for method, prefix in (("", "Prog"), ("generic ", "Gen"), ("synthetic ", "Syn"), ("corpus ", "Corp")):
                chosen = of(f"{method}{kind}", key)
                if chosen:
                    label = f"Priv{prefix}{key.capitalize()}Eps{name}"
                    numbers[label + "Recall"] = pct(recall(chosen))
                    numbers[label + "Precision"] = pct(precision(chosen))
                    numbers[label + "Compiled"] = f"{sum(r['compiled'] for r in chosen)}/{len(chosen)}"
    for kind, prefix in (("synthetic eps=1", "SynEpsOne"), ("synthetic eps=10", "SynEpsTen")):
        chosen = of(kind)
        numbers[f"Priv{prefix}Failed"] = sum(not r["compiled"] for r in chosen)
        numbers[f"Priv{prefix}All"] = len(chosen)
        numbers[f"Priv{prefix}FalseViolations"] = sum(
            sum("contract violated" in e for e in r["errors"]) for r in chosen if not r["compiled"])
    for kind, name in (("optimistic", "Optimistic"), ("none", "None")):
        numbers[f"Priv{name}Recall"] = pct(recall(of(kind)))
        numbers[f"Priv{name}Precision"] = pct(precision(of(kind)))

    real = ROOT / "eval" / "data" / "real"
    for name, folder in (("Traffic", "traffic"), ("Bikes", "bikes")):
        sizes = [len(path.read_text(encoding="utf-8").splitlines()) - 1 for path in (real / folder).glob("*.csv")]
        numbers[f"Priv{name}Rows"] = f"{statistics.median(sizes):,.0f}"
    import pyarrow.parquet
    sizes = [pyarrow.parquet.ParquetFile(path).metadata.num_rows for path in (real / "green").glob("*.parquet")]
    numbers["PrivTaxiRows"] = f"{statistics.median(sizes):,.0f}"

    # Table: recall and precision against the normal removals, per workload and kind of facts.
    kinds = (["exact"] + [f"eps={e}" for e in ("0.1", "1", "10", "100")] + ["corpus eps=1", "corpus eps=10"] +
             [f"{base} eps={e}" for base in ("generic", "synthetic") for e in ("1", "10")] +
             ["optimistic", "random", "none"])
    names = {"exact": "program's cells, no noise", "random": "random (true or false)", "none": "none (claims nothing)",
             "optimistic": "optimistic (no data)"}
    workloads = [("traffic", "Traffic"), ("bikes", "Bikes"), ("taxi", "Taxi"), ("titanic", "Titanic")]
    lines = [r"\begin{table*}[t]",
             r"\caption{Private compilation (RQ8). Per workload, recall/precision (\%) of the compilation's removals "
             r"against the normal compilation of the same file: recall is the share of normal removals it also "
             r"makes, precision the share of its removals that rest on true facts. Compiled: compilations without "
             r"errors. Deopt.\ on file: compilations whose run on the compiled file itself deoptimizes (a released "
             r"fact was false). Every run is scored against a fresh compilation of its batch. $\varepsilon$ is the "
             r"budget of one program; ``one release'' serves all Titanic scripts with one budget. Generic: fixed "
             r"logarithmic bins; synthetic: a table sampled from them, compiled normally.}",
             r"\label{tab:rq-private}", r"\centering", r"\footnotesize",
             r"\begin{tabular}{@{}lrrrrrrrrr@{}}", r"\toprule",
             r"Facts & Traffic & Bikes & Taxi & Titanic & Compilations & Compiled & Deopt.\ on file & Runs & Wrong \\",
             r"\midrule"]
    for kind in kinds:
        chosen = of(kind)
        if not chosen:
            continue
        cells = []
        for key, _ in workloads:
            items = [r for r in chosen if workload(r) == key]
            cells.append("/".join(pct(f(items)) for f in (recall, precision)) if items else "")
        compiled = [r for r in chosen if r["compiled"]]
        anchor = [any(run["anchor"] and run["deopts"] for run in r["runs"]) for r in compiled]
        all_runs = [run for r in chosen for run in r["runs"]]
        wrong = sum(run["outcome"] in ("unsound", "wrong stop") for run in all_runs)
        if kind in names:
            label = names[kind]
        else:
            method, epsilon = kind.rsplit(" ", 1) if " " in kind else ("program's cells", kind)
            method = {"corpus": "one release", "generic": "generic bins",
                      "synthetic": "synthetic data"}.get(method, method)
            label = f"{method}, $\\varepsilon = {epsilon[4:]}$"
        deopt = f"{100 * sum(anchor) / len(anchor):.0f}\\%" if anchor else "--"
        lines.append(f"{label} & " + " & ".join(cells) + f" & {len(chosen):,} & {len(compiled):,} & {deopt} & "
                     f"{len(all_runs):,} & {wrong} \\\\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table*}"]
    (ROOT / "paper" / "generated" / "privatetable.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # Reports on the 1,000,000-row file.
    numbers["PrivDiagRows"] = "1{,}000{,}000"
    sizes = sorted({r["rows"] for r in diagnostics})

    def always_from(method, epsilon):
        """The smallest m from which every compilation reports the broken contract, for every
        mistake and every larger m."""
        chosen = [r for r in diagnostics if r["method"] == method and r["epsilon"] == epsilon]
        good = [m for m in sizes if m > 0 and all(r["warned"] for r in chosen if r["rows"] >= m)]
        return f"{min(good):,}" if good and chosen else "never"

    for method, prefix in (("program", ""), ("generic", "Generic"), ("synthetic", "Synthetic")):
        chosen = [r for r in diagnostics if r["method"] == method]
        numbers[f"PrivDiag{prefix}Compilations"] = len(chosen)
        numbers[f"PrivDiag{prefix}False"] = sum(r["false_warnings"] for r in chosen)
        numbers[f"PrivDiag{prefix}FalseRuns"] = sum(r["false_warnings"] > 0 for r in chosen)
        numbers[f"PrivDiag{prefix}EpsOne"] = always_from(method, 1)
        numbers[f"PrivDiag{prefix}EpsTen"] = always_from(method, 10)
    columns = sorted({(r["method"], r["epsilon"]) for r in diagnostics if r["method"] == "program"
                      or r["epsilon"] in (1, 10)},
                     key=lambda c: (("program", "generic", "synthetic").index(c[0]), c[1]))
    kinds = list(dict.fromkeys(r["kind"] for r in diagnostics))
    lines = [r"\begin{table*}[t]",
             r"\caption{Private compilation (RQ8): share of compilations that report the contract a mistake in $m$ "
             r"of 1{,}000{,}000 rows breaks (program's cells: 20 seeds, one without noise; baselines: 10 seeds). A "
             r"private compilation warns that it is likely violated; one from synthetic data reports it violated. "
             r"In brackets: compilations that report a contract the data does not break.}",
             r"\label{tab:rq-private-diag}", r"\centering", r"\footnotesize",
             r"\begin{tabular}{@{}lr" + "r" * len(columns) + r"@{}}", r"\toprule",
             r" & & \multicolumn{" + str(sum(c[0] == "program" for c in columns)) + r"}{c}{program's cells} & "
             r"\multicolumn{2}{c}{generic bins} & \multicolumn{2}{c}{synthetic data} \\",
             "Mistake & $m$ & " + " & ".join("no noise" if math.isinf(e) else f"$\\varepsilon = {e:g}$"
                                               for _, e in columns) + r" \\", r"\midrule"]
    for kind in kinds:
        for m in sizes:
            cells = []
            for method, e in columns:
                found = [r for r in diagnostics if r["kind"] == kind and r["rows"] == m and r["epsilon"] == e
                         and r["method"] == method]
                share = f"{100 * sum(r['warned'] for r in found) / len(found):.0f}\\%" if found else "--"
                false = sum(r["false_warnings"] > 0 for r in found)
                cells.append(share + (f" ({false})" if false else ""))
            lines.append(f"{kind.replace('_', ' ')} & {m:,} & " + " & ".join(cells) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table*}"]
    (ROOT / "paper" / "generated" / "privatediagtable.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")

    test = ROOT / "build" / "Release" / "test_private.exe"
    if test.exists():
        text = subprocess.run([str(test)], cwd=ROOT / "build", capture_output=True, text=True).stdout
        facts = re.search(r"(\d+) of \d+ random facts files compile", text)
        deopt = re.search(r"(\d+) inputs admitted, (\d+) of them deoptimize", text)
        if facts and deopt:
            numbers.update(PrivTestFacts=facts.group(1), PrivTestDeopt=deopt.group(2))


def soundness_tests(numbers):
    """The randomized soundness test of the running example: inputs admitted with and without deoptimization."""
    import re
    import subprocess
    test = ROOT / "build" / "Release" / "test_certificate.exe"
    if not test.exists():
        return
    text = subprocess.run([str(test)], cwd=ROOT / "build", capture_output=True, text=True).stdout
    full = re.search(r"(\d+) of 400 random inputs admitted by the full certificate", text)
    deopt = re.search(r"(\d+) admitted with deoptimization, (\d+) of them deoptimize", text)
    folders = re.findall(r"(\d+) folders admitted with deoptimization", text)
    if full and deopt:
        numbers.update(RandFull=full.group(1), RandDeopt=deopt.group(1), RandDeopted=deopt.group(2))
    if folders:
        numbers["RandFolders"] = sum(int(f) for f in folders)
    dates = re.search(r"(\d+) of 400 random inputs admitted with deoptimization, (\d+) of them deoptimize", text)
    if dates:
        numbers.update(RandDates=dates.group(1), RandDatesDeopted=dates.group(2))


if __name__ == "__main__":
    main()
