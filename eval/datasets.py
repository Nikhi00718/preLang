"""Synthetic data for the PrepLang examples and the evaluation.

    py -3 eval/datasets.py examples     # rebuilds examples/images/
    py -3 eval/datasets.py benchmark    # writes eval/data/ (large, not committed)

Everything is generated from fixed seeds, so the files are the same on every run.
"""
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


if __name__ == "__main__":
    if sys.argv[1:] == ["examples"]:
        make_example_images()
    else:
        sys.exit("usage: py -3 eval/datasets.py examples")
