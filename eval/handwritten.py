"""A hand-written image script, written the careful way: one image at a time.

    py -3 eval/handwritten.py <input folder> <output folder> jpg|float-png

jpg        resize to 224x224, make gray, save as JPG (the pipeline the benchmarks use)
float-png  the same, but the pixels are scaled to floats in [0, 1] and saved as PNG,
           a mistake that raises no error
"""
import sys
from pathlib import Path

import cv2
import numpy as np

source, target, mode = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
target.mkdir(parents=True, exist_ok=True)
for path in sorted(source.glob("*.jpg")):
    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    image = cv2.resize(image, (224, 224), interpolation=cv2.INTER_AREA)
    image = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    if mode == "float-png":
        cv2.imwrite(str(target / (path.stem + ".png")), image.astype(np.float32) / 255)
    else:
        cv2.imwrite(str(target / path.name), image)
