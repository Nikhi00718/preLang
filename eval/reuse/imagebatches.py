"""Reusing a compiled image pipeline on new folders (part of RQ3).

    py -3 eval/reuse/imagebatches.py [path/to/preplang.exe]

The pipeline makes images gray and 64x64 and saves them as PNG. It is compiled on a folder of
gray 64x64 images, so the optimizer removes GRAYSCALE and RESIZE speculatively. New folders, five
of each kind: more gray 64x64 images; some color images; other sizes; both; a file that is not an
image; 16-bit images. Every folder is run by the compiled program (with deoptimization, and with
--no-deopt) and, as ground truth, compiled and run fresh; the written images are compared byte by
byte. Writes eval/results/imagebatches.json and .md.
"""
import json
import shutil
import sys
from collections import Counter
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from policies import compile_variants, run_fresh, run_program, score  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "eval" / "work" / "imagebatches"
RESULTS = ROOT / "eval" / "results"
PROGRAM = """# Gray 64x64 PNG images for a small classifier.
LOAD_IMAGES "imgs/" AS imgs;
GRAYSCALE imgs;
RESIZE imgs TO 64 64;
CONVERT imgs TO PNG;
SAVE_IMAGES imgs TO "small/";
"""
KINDS = ("gray 64x64", "some color", "other sizes", "color and sizes", "a broken file", "16-bit")


def folder(path, kind, seed, count=12):
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)
    rng = np.random.default_rng(seed)
    for i in range(count):
        size = (64, 64)
        if kind in ("other sizes", "color and sizes") and i % 3 == 0:
            size = (int(rng.integers(32, 200)), int(rng.integers(32, 200)))
        color = kind in ("some color", "color and sizes") and i % 4 == 0
        depth = np.uint16 if kind == "16-bit" and i % 2 == 0 else np.uint8
        top = 65535 if depth == np.uint16 else 255
        image = rng.integers(0, top, (size[1], size[0], 3) if color else (size[1], size[0]), dtype=depth)
        cv2.imwrite(str(path / f"img{seed:02d}_{i:02d}.png"), image)
    if kind == "a broken file":
        (path / "img_broken.png").write_bytes(b"<html>404 Not Found</html>")


def main(compiler):
    compiled = WORK / "compiled"
    if compiled.exists():
        shutil.rmtree(compiled)
    compiled.mkdir(parents=True)
    (compiled / "program.prep").write_text(PROGRAM, encoding="utf-8")
    folder(compiled / "imgs", "gray 64x64", 1000)
    ok, text = compile_variants(compiler, compiled, "program.prep")
    assert ok, text
    rows = []
    for kind in KINDS:
        for seed in range(5):
            batch = WORK / "batches" / f"{kind.replace(' ', '_')}_{seed}"
            folder(batch, kind, seed)
            deopt = run_program(compiled, "generated.py", {"imgs": batch})
            full = run_program(compiled, "generated_full.py", {"imgs": batch})
            trusted = run_program(compiled, "generated_full.py", {"imgs": batch}, trust=True)
            fresh_folder = WORK / "fresh"
            if fresh_folder.exists():
                shutil.rmtree(fresh_folder)
            fresh_folder.mkdir(parents=True)
            (fresh_folder / "program.prep").write_text(PROGRAM, encoding="utf-8")
            shutil.copytree(batch, fresh_folder / "imgs")
            fresh = run_fresh(compiler, fresh_folder, "program.prep")
            fresh["files"] = {k: v for k, v in fresh["files"].items()}
            deopt["files"] = {k: v for k, v in deopt["files"].items()}
            rows.append({"kind": kind, "seed": seed, "fresh": fresh["status"], "deopt": deopt["status"],
                         "deopts": deopt["deopts"], "reason": deopt["reason"], "full": full["status"],
                         "trusted": trusted["status"],
                         "outcome": {"fingerprint": score({"status": "refused"}, fresh), "full": score(full, fresh),
                                     "deopt": score(deopt, fresh), "trusted": score(trusted, fresh)}})
            print(kind, seed, "deopt", deopt["status"], len(deopt["deopts"]), "full", full["status"], "fresh",
                  fresh["status"], rows[-1]["outcome"]["deopt"], flush=True)
    RESULTS.mkdir(parents=True, exist_ok=True)
    (RESULTS / "imagebatches.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    lines = ["# Reusing a compiled image pipeline on new folders", "",
             "| New folder | Folders | Full certificate | Deoptimization | Trusted optimized program | "
             "Removals re-enabled |", "|---|---:|---|---|---|---|"]
    for kind in KINDS:
        items = [r for r in rows if r["kind"] == kind]
        cell = lambda p: ", ".join(f"{n} {o}" for o, n in Counter(r["outcome"][p] for r in items).items())  # noqa: E731
        again = Counter(d.split(":")[0] + ":" + d.split(":")[1] for r in items for d in r["deopts"])
        lines.append(f"| {kind} | {len(items)} | {cell('full')} | {cell('deopt')} | {cell('trusted')} | "
                     f"{'; '.join(f'{n} x {k}' for k, n in again.items()) or '-'} |")
    lines.append("")
    (RESULTS / "imagebatches.md").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "preplang.exe")
