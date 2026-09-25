"""Synthetic data for the PrepLang examples and the evaluation.

    py -3 eval/datasets.py examples     # rebuilds examples/images/
    py -3 eval/datasets.py benchmark    # writes eval/data/ (large, not committed)
    py -3 eval/datasets.py scale        # Parquet tables and a broken image folder, also in eval/data/

Everything is generated from fixed seeds, so the files are the same on every run.
"""
import os
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parent.parent


def synthetic_image(width, height, channels, seed):
    """A smooth gradient with a few shapes, so resizing and compression behave like on photos."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:height, 0:width]
    base = (x / width * 180 + y / height * 60).astype(np.uint8)
    img = np.dstack([base, np.flipud(base), np.full_like(base, rng.integers(40, 200))])
    for _ in range(4):
        center = (int(rng.integers(0, width)), int(rng.integers(0, height)))
        color = [int(c) for c in rng.integers(0, 256, 3)]
        cv2.circle(img, center, int(rng.integers(10, min(width, height) // 3)), color, -1)
    return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) if channels == 1 else img


def write(path, img):
    path.parent.mkdir(parents=True, exist_ok=True)
    ok, data = cv2.imencode(path.suffix, img)
    assert ok, path
    data.tofile(str(path))


def make_example_images():
    folder = ROOT / "examples" / "images"
    images = {
        "cats/cat_01.jpg": (320, 240, 3),
        "cats/cat_02.jpg": (400, 300, 3),
        "cats/cat_03.jpg": (640, 480, 3),
        "dogs/dog_01.png": (300, 300, 3),
        "dogs/dog_02.png": (512, 384, 3),
        "dogs/dog_03.png": (256, 256, 1),  # grayscale
    }
    for seed, (name, (width, height, channels)) in enumerate(images.items()):
        write(folder / "dataset" / name, synthetic_image(width, height, channels, seed))
    (folder / "dataset" / "broken.jpg").write_bytes(b"this file is not an image\n")

    labels = {name: name.split("/")[0][:-1] for name in images}
    lines = ["filename,label"] + [f"{name},{label}" for name, label in labels.items()]
    (folder / "labels.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    # One label is missing and one has no image, for the MATCH_RATE error example.
    partial = [line for line in lines if not line.startswith("dogs/dog_03")] + ["cats/cat_04.jpg,cat"]
    (folder / "labels_partial.csv").write_text("\n".join(partial) + "\n", encoding="utf-8")
    print("wrote", folder)


DATA = ROOT / "eval" / "data"


def make_wide_table(rows=200_000):
    """id, 30 floats (f1 has 1% missing values), 5 small integers and 4 text columns."""
    rng = np.random.default_rng(7)
    import pandas as pd
    frame = pd.DataFrame({"id": np.arange(rows)})
    for i in range(30):
        frame[f"f{i}"] = rng.random(rows).round(6)
    frame.loc[rng.random(rows) < 0.01, "f1"] = np.nan
    for i in range(5):
        frame[f"k{i}"] = rng.integers(0, 5, rows)
    for i in range(4):
        frame[f"s{i}"] = rng.choice(["red", "green", "blue", "gray"], rows)
    frame.to_csv(DATA / "wide.csv", index=False)


def make_tall_tables():
    """Tables of 10k, 100k and 1M rows with 10 columns, for compile-time scaling."""
    rng = np.random.default_rng(11)
    import pandas as pd
    for rows in (10_000, 100_000, 1_000_000):
        frame = pd.DataFrame({f"c{i}": rng.random(rows).round(4) for i in range(8)})
        frame["n"] = rng.integers(0, 1000, rows)
        frame["t"] = rng.choice(["a", "b", "c"], rows)
        frame.to_csv(DATA / f"tall_{rows}.csv", index=False)


def make_image_folders():
    """640x480 JPG images: folders of 100, 1000 and 3000 files."""
    for count in (100, 1000, 3000):
        folder = DATA / f"images_{count}"
        for i in range(count):
            write(folder / f"img_{i:05d}.jpg", synthetic_image(640, 480, 3, i))


def make_benchmark_data():
    DATA.mkdir(parents=True, exist_ok=True)
    make_wide_table()
    make_tall_tables()
    make_image_folders()
    print("wrote", DATA)


def make_event_tables():
    """Parquet tables of 1M, 4M and 16M rows: id, a in [0, 1), b in [0, 1) with 1% missing,
    k in 0..4, amount in 0..999 and a text column s."""
    import pyarrow as pa
    import pyarrow.parquet as pq
    rng = np.random.default_rng(13)
    for rows in (1_000_000, 4_000_000, 16_000_000):
        table = pa.table({
            "id": np.arange(rows, dtype=np.int64),
            "a": rng.random(rows),
            "b": pa.array(rng.random(rows), mask=rng.random(rows) < 0.01),
            "k": rng.integers(0, 5, rows, dtype=np.int32),
            "amount": rng.integers(0, 1000, rows, dtype=np.int64),
            "s": pa.array(rng.choice(np.array(["red", "green", "blue", "gray"]), rows)),
        })
        pq.write_table(table, DATA / f"events_{rows}.parquet")


def make_broken_image_folder():
    """images_3000 plus one broken file that sorts last, so a script meets it at the very end.
    The 3,000 good files are hard links, so they take no extra space."""
    source, target = DATA / "images_3000", DATA / "images_3000_broken"
    target.mkdir(exist_ok=True)
    for path in source.glob("*.jpg"):
        if not (target / path.name).exists():
            os.link(path, target / path.name)
    (target / "zz_broken.jpg").write_bytes(b"this file is not an image\n")


def make_scale_data():
    DATA.mkdir(parents=True, exist_ok=True)
    make_event_tables()
    make_broken_image_folder()
    print("wrote", DATA)


if __name__ == "__main__":
    if sys.argv[1:] == ["examples"]:
        make_example_images()
    elif sys.argv[1:] == ["benchmark"]:
        make_benchmark_data()
    elif sys.argv[1:] == ["scale"]:
        make_scale_data()
    else:
        sys.exit("usage: py -3 eval/datasets.py examples|benchmark|scale")
