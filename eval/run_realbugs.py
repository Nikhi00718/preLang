"""Real-bug study (RQ6): reproduces the issues of realbugs_catalog.py and checks the compiler.

    py -3 eval/run_realbugs.py [preplang.exe] [preplang.exe before the study's fixes]

Every issue with the verdict compile, contract or design has a reproduction below: a small
PrepLang program and data that recreate the situation described in the issue. The expected
result is stated with the case: a compile error whose text names the cause, or (for design)
a program that compiles and runs. Writes eval/results/realbugs.md and realbugs.json.
"""
import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

import cv2
import numpy as np

from realbugs_catalog import EXCLUDED, ISSUES, SIGNATURES

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "eval" / "work" / "realbugs"
RESULTS = ROOT / "eval" / "results"
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe"
BEFORE = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None


def image(width, height, channels, seed=0):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:height, 0:width]
    base = ((x + y + rng.integers(0, 50)) % 256).astype(np.uint8)
    if channels == 1:
        return base
    planes = [base, np.flipud(base), np.full_like(base, 90)] + ([np.full_like(base, 200)] if channels == 4 else [])
    return np.dstack(planes)


def png(width, height, channels, seed=0):
    return cv2.imencode(".png", image(width, height, channels, seed))[1].tobytes()


def jpg(width, height, seed=0):
    return cv2.imencode(".jpg", image(width, height, 3, seed))[1].tobytes()


def text(lines):
    return ("\n".join(lines) + "\n").encode("utf-8")


# case: (program, {path: bytes}, expected)  expected is ("error", text) or ("runs", files written)
CASES = {
    "fluxgym": ("""LOAD_IMAGES "dataset/" AS dataset;
RESIZE dataset TO 512 512 KEEP_ASPECT;
SAVE_IMAGES dataset TO "resized/";
""", {"dataset/photo_01.jpg": jpg(640, 480, 1), "dataset/photo_02.jpg": jpg(500, 400, 2),
      "dataset/screenshot.jpg": png(300, 200, 4)}, ("error", "JPG cannot store transparency")),

    "comt": ("""LOAD_IMAGES "images/" AS images;
RESIZE images TO 336 336;
SAVE_IMAGES images TO "resized/";
""", {"images/0001.jpg": jpg(400, 300, 1), "images/0002.jpg": jpg(400, 300, 2),
      "images/0003.jpg": bytes(100)}, ("error", "cannot be decoded")),

    "crawler": ("""LOAD_IMAGES "blobs/" AS tiles;
CONVERT tiles TO PNG;
SAVE_IMAGES tiles TO "combined/";
""", {"blobs/1352.jpg": jpg(256, 256, 1), "blobs/1353.jpg": jpg(256, 256, 2),
      "blobs/1354.jpg": b"<html><body><h1>404 Not Found</h1></body></html>\n"}, ("error", "cannot be decoded")),

    "packt": ("""LOAD_IMAGE "images/dog.jpg" AS image;
RESIZE image TO 416 416;
SAVE_IMAGE image AS "dog_416.jpg";
""", {"images/cat.jpg": jpg(640, 480, 1)}, ("error", "cannot open image file")),

    "tuigan": ("""LOAD_IMAGES "trainA/" AS train_a;
EXPECT train_a CHANNELS 3;
RESIZE train_a TO 256 256;
SAVE_IMAGES train_a TO "prepared/trainA/";
""", {"trainA/a1.png": png(104, 101, 1, 1), "trainA/a2.png": png(120, 100, 1, 2)}, ("error", "CHANNELS 3")),

    "wavect": ("""LOAD_IMAGES "content/" AS content;
EXPECT content CHANNELS 3;
RESIZE content TO 512 512;
SAVE_IMAGES content TO "prepared/";
""", {"content/1.png": png(300, 300, 3, 1), "content/2.png": png(300, 300, 1, 2)}, ("error", "CHANNELS 3")),

    "rfr": ("""LOAD_IMAGES "train_images/" AS train;
EXPECT train CHANNELS 3;
RESIZE train TO 224 224;
SAVE_IMAGES train TO "prepared/";
""", {"train_images/1.png": png(256, 256, 3, 1), "train_images/2.png": png(256, 256, 4, 2),
      "train_images/3.jpg": jpg(256, 256, 3)}, ("error", "CHANNELS 3")),

    "popsim": ("""LOAD "crosswalk.csv" AS crosswalk;
LOAD "gq_landuse.csv" AS gq;
EXPECT crosswalk.TAZ REFERENCES gq.TAZ;
SAVE crosswalk AS "crosswalk_checked.csv";
SAVE gq AS "gq_checked.csv";
""", {"crosswalk.csv": text(["TAZ,PUMA"] + [f"{t},{100 + t % 2}" for t in range(1, 7)]),
      "gq_landuse.csv": text(["TAZ,gq_pop", "1,40", "3,12", "5,7"])}, ("error", "no match in gq.TAZ")),

    "umami": ("""LOAD "export.csv" AS events;
SELECT events [session_id, hostname, browser, os, country, subdivision1, subdivision2];
SAVE events AS "sessions.csv";
""", {"export.csv": text(["session_id,hostname,browser,os,country,region",
                          "a1,example.com,chrome,Windows,IN,TN", "b2,example.com,firefox,Linux,DE,BY"])},
     ("error", "subdivision1")),

    "sjaracne": ("""LOAD "expression.csv" AS expression;
NORMALIZE expression.value_1 USING ZSCORE;
SAVE expression AS "expression_normalized.csv";
""", {"expression.csv": text(["gene,value_1,value_2", "TP53,5.2,4.9", "MYC,7.1,7.3", "RPL10_iso,RPL10,6.0",
                              "EGFR,3.3,3.1"])}, ("error", "RPL10")),

    "kstar": ("""LOAD "network.csv" AS network;
NORMALIZE network.* EXCEPT [substrate_acc, site] USING MINMAX;
SAVE network AS "network_pruned.csv";
""", {"network.csv": text(["accession,site,net_1,net_2", "A0AVT1,Y55,0.8,0.1", "P04637,S15,0.3,0.9"])},
     ("error", "accession")),

    "gnps": ("""LOAD "quant.csv" AS quant;
FILTER quant WHERE mz > 100;
SAVE quant AS "features.csv";
""", {"quant.csv": text(["id,mz,rt,area", "1,150.2,3.1,1200", "2,unknonw,4.0,800", "3,300.7,5.5,450"])},
     ("error", "unknonw")),

    "clothing": ("""LOAD "list_category.csv" AS categories;
FILTER categories WHERE category <= 21;
SAVE categories AS "upper_body.csv";
""", {"list_category.csv": text(["image_name,category", "img_0001.jpg,3", "img_0002.jpg,48",
                                 "image_name,category_label", "img_0003.jpg,17"])}, ("error", "category_label")),

    "cp1252": ("""LOAD "issues.csv" AS issues;
SAVE issues AS "labeled.csv";
""", {"issues.csv": b"number,title\n1,Login fails\n2,Export for caf\xe9 accounts fails\n"}, ("error", "not UTF-8")),

    "buildium": ("""LOAD "Rental_Owner_Statement.csv" AS statement;
SAVE statement AS "stessa.csv";
""", {"Rental_Owner_Statement.csv": b"date,description,amount\n2021-01-05,Rent \x96 Unit 4,1200\n"},
     ("error", "not UTF-8")),

    "aly6140": ("""LOAD "MisleInjury.txt" AS injuries;
SAVE injuries AS "injuries.csv";
""", {"MisleInjury.txt": b"id\tvessel\tinjury\n1\tMar\xeda\tminor\n"}, ("error", "not UTF-8")),

    "junction": ("""LOAD "train_features.csv" AS train;
LOAD "predict_features.csv" AS predict;
EXPECT predict SCHEMA_SAME_AS train EXCEPT [label];
SAVE predict AS "predict_checked.csv";
SAVE train AS "train_checked.csv";
""", {"train_features.csv": text(["customer_id,avg_order_size,avg_purchase_size,fulfillment_rate,label",
                                  "1,3.5,20.1,0.9,1", "2,1.2,5.0,0.7,0"]),
      "predict_features.csv": text(["customer_id,fulfillment_rate", "3,0.8"])}, ("error", "avg_order_size")),

    "flower": ("""LOAD_IMAGES "flower_images/" AS images;
LOAD "flower_labels.csv" AS labels;
LINK images WITH labels ON file AS flowers;
EXPECT flowers MATCH_RATE 100%;
SAVE_IMAGES images TO "prepared/";
SAVE labels AS "labels.csv";
""", dict({f"flower_images/{i:04d}.png": png(128, 128, 3, i) for i in range(1, 7)},
          **{"flower_labels.csv": text(["file,label"] + [f"{i:04d}.png,{i % 3}" for i in range(1, 5)])}),
     ("error", "match rate")),

    "inigo": ("""LOAD_IMAGES "photos/" AS photos;
RESIZE photos TO 128 128 KEEP_ASPECT;
SAVE_IMAGES photos TO "backup/";
""", {"photos/card.jpg": jpg(400, 300, 1), "photos/tree.jpg": jpg(300, 400, 2),
      "photos/christmascard2013.psd": b"8BPS" + bytes(60)}, ("runs", 2)),

    "gogul": ("""LOAD_IMAGES "train/" AS train;
RESIZE train TO 500 500;
SAVE_IMAGES train TO "resized/";
""", {"train/image_3.jpg": jpg(320, 240, 1), "train/image_7.jpg": jpg(320, 240, 2),
      "train/img_x.jpg": jpg(320, 240, 3)}, ("runs", 3)),

    "datacleaner": ("""LOAD "test2.csv" AS data;
FILL_NULL data.code WITH "unknown";
SAVE data AS "my_clean_data.csv";
""", {"test2.csv": text(["name,code,value", "a,10,1.5", "b,x7,2.5", "c,,3.0", "d,12,4.5"])}, ("runs", 1)),
}


def compile_case(compiler, folder):
    """Exit code and every error, as its message followed by its hint and notes."""
    done = subprocess.run([str(compiler), "program.prep", "--source"], cwd=folder, capture_output=True, text=True)
    errors, current = [], None
    for line in done.stdout.splitlines():
        if line.startswith(("error[", "warning[")):
            current = [line.split(": ", 1)[1]] if line.startswith("error[") else None
            if current:
                errors.append(current)
        elif current is not None and line.strip().startswith("= "):
            current.append(line.strip()[2:])
    return done.returncode, [" / ".join(parts) for parts in errors]


def written_files(folder):
    output = folder / "output"
    return [p for p in output.rglob("*") if p.is_file() and p.name not in ("generated.py", "preplang_manifest.json")]


def check(compiler, name):
    program, files, (kind, expected) = CASES[name]
    folder = WORK / f"{name}_{compiler.parent.parent.parent.name}"
    shutil.rmtree(folder, ignore_errors=True)
    for path, data in files.items():
        (folder / path).parent.mkdir(parents=True, exist_ok=True)
        (folder / path).write_bytes(data)
    (folder / "program.prep").write_text(program, encoding="utf-8")
    code, errors = compile_case(compiler, folder)
    if kind == "error":
        matching = [e for e in errors if expected in e]
        if code != 0 and matching:
            return True, matching[0]
        return False, errors[0] if errors else "compiled without an error"
    if code != 0:
        return False, "compile error: " + errors[0]
    run = subprocess.run([str(compiler), "program.prep", "--source", "--run"], cwd=folder, capture_output=True, text=True)
    count = len(written_files(folder))
    return run.returncode == 0 and count >= expected, f"compiled and ran; {count} output file(s)"


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    results = []
    for signature, issue, cause, now, before, case, note in ISSUES:
        row = {"signature": signature, "issue": issue, "cause": cause, "verdict": now, "before": before,
               "case": case, "note": note}
        if case:
            row["reproduced"], row["compiler_says"] = check(COMPILER, case)
            if BEFORE:
                passed, says = check(BEFORE, case)
                row["before_measured"] = says if not passed else "same as now"
        results.append(row)
        status = "" if not case else ("ok " if row["reproduced"] else "FAILED ") + row["compiler_says"][:90]
        print(f"{signature:4} {issue[:45]:45} {now:9} {status}")
    (RESULTS / "realbugs.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    write_markdown(results)


def write_markdown(results):
    total = len(results)
    now, before = Counter(r["verdict"] for r in results), Counter(r["before"] for r in results)
    early = lambda c: c["compile"] + c["contract"]  # noqa: E731
    reproduced = sum(r.get("reproduced", False) for r in results)
    cases = sum(1 for r in results if r["case"])
    lines = [
        "# RQ6: real data-preparation bugs from GitHub issues",
        "",
        f"- Qualifying issues: {total} (from {len(SIGNATURES)} error signatures; "
        f"{len(EXCLUDED)} more results were read and did not qualify)",
        f"- Reported before any data is processed: {early(now)} of {total} ({100 * early(now) / total:.0f}%): "
        f"{now['compile']} with no annotation, {now['contract']} once the requirement is declared with EXPECT",
        f"- Cannot occur in a PrepLang program: {now['design']}; not covered: {now['none']}",
        f"- Before the two fixes the study led to (format from file content, UTF-8 check): "
        f"{early(before)} of {total} ({100 * early(before) / total:.0f}%)",
        f"- Reproductions checked against the compiler: {reproduced} of {cases} pass",
        "",
        "## Why the others are not covered",
        "",
        "| Reason | Issues |",
        "|---|---:|",
    ]
    reasons = Counter(r["note"].split(":")[0] for r in results if r["verdict"] == "none")
    lines += [f"| {reason} | {count} |" for reason, count in reasons.most_common()]
    lines += [
        "",
        "## Issues",
        "",
        "| Sig. | Issue | Root cause | Verdict | Before fixes | Compiler says (reproduction) |",
        "|---|---|---|---|---|---|",
    ]
    for r in results:
        owner, number = r["issue"].split("#")
        link = f"[{r['issue']}](https://github.com/{owner}/issues/{number})"
        says = (("✓ " if r["reproduced"] else "✗ ") + r["compiler_says"]) if r["case"] else r["note"]
        lines.append(f"| {r['signature']} | {link} | {r['cause']} | {r['verdict']} | {r['before']} | "
                     f"{says.replace('|', '/')} |")
    lines += ["", "## Signatures", ""] + [f"- {key}: {value}" for key, value in SIGNATURES.items()]
    lines += ["", "## Read but not qualifying", ""] + [f"- {s} {issue}: {why}" for s, issue, why in EXCLUDED]
    (RESULTS / "realbugs.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[2:8]))


if __name__ == "__main__":
    main()
