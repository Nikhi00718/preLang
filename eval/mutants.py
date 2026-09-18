"""Fault-injection benchmark: each mutant is one example program with one
realistic preprocessing mistake injected by a single text edit.

Fault classes:
  name       a column or table that does not exist (typo, missing in some tables)
  type       an operation that does not fit the column or pixel type
  lifecycle  using data after it was removed, or an operation on the wrong kind of dataset
  contract   the data breaks a declared expectation (EXPECT)
  validity   input files that cannot be read
  value      a well-typed program that silently produces wrong values

`oracle` (optional) checks the baseline's output for silent corruption: it
returns True when the output is wrong.
"""
from pathlib import Path

import cv2
import numpy as np
import pandas as pd

EXAMPLES = Path(__file__).resolve().parent.parent / "examples"

SEEDS = {
    "students": EXAMPLES / "tabular" / "students.prep",
    "company": EXAMPLES / "tabular" / "tableset.prep",
    "split": EXAMPLES / "tabular" / "ml_split.prep",
    "images": EXAMPLES / "images" / "imageset.prep",
    "linked": EXAMPLES / "images" / "linked.prep",
}


def no_rows(output):
    return len(pd.read_csv(output / "clean.csv")) == 0


def text_in_age(output):
    return not pd.api.types.is_numeric_dtype(pd.read_csv(output / "clean.csv")["age"])


def black_images(folder):
    def oracle(output):
        files = [p for p in (output / folder).rglob("*") if p.is_file()]
        means = [cv2.imread(str(p), cv2.IMREAD_UNCHANGED).mean() for p in files]
        return bool(files) and max(means) < 2  # every written image is (almost) black
    return oracle


MUTANTS = [
    # id, seed, class, description, old text, new text, oracle
    ("T1", "students", "name", "misspelled column", "NORMALIZE students.attendance",
     "NORMALIZE students.attendence", None),
    ("T2", "students", "type", "NORMALIZE on a text column", "NORMALIZE students.attendance",
     "NORMALIZE students.name", None),
    ("T3", "students", "type", "number compared with text", "marks >= 50", 'marks >= "50"', None),
    ("T4", "students", "type", "text filled into an INTEGER column", 'FILL_NULL students.department WITH "UNKNOWN";',
     'FILL_NULL students.age WITH "UNKNOWN";', text_in_age),
    ("T5", "students", "lifecycle", "column used after SELECT removed it", 'SAVE students AS "clean.csv";',
     'FILL_NULL students.passed WITH FALSE;\nSAVE students AS "clean.csv";', None),
    ("T6", "students", "contract", "range contract broken by the data", "EXPECT students.marks BETWEEN 0 AND 100;",
     "EXPECT students.marks BETWEEN 0 AND 90;", None),
    ("T7", "students", "contract", "NOT_NULL on a column with missing values", "EXPECT students.name NOT_NULL;",
     "EXPECT students.department NOT_NULL;", None),
    ("T8", "students", "value", "threshold on a normalized column", "NORMALIZE students.attendance USING MINMAX;",
     "NORMALIZE students.marks USING MINMAX;", no_rows),
    ("S1", "company", "name", "wildcard column missing in some tables", "FILL_NULL company.*.salary WITH 0 IF_EXISTS;",
     "FILL_NULL company.*.salary WITH 0;", None),
    ("S2", "company", "type", "NORMALIZE on text columns of every table", "FILL_NULL company.interns.stipend WITH 0;",
     "FILL_NULL company.interns.stipend WITH 0;\nNORMALIZE company.*.name;", None),
    ("S3", "company", "contract", "foreign key into the wrong table", "REFERENCES company.departments.name",
     "REFERENCES company.employees.name", None),
    ("S4", "company", "lifecycle", "FILTER on a whole TABLESET", "DROP company.* [temporary_id] IF_EXISTS;",
     "FILTER company WHERE age > 0.5;\nDROP company.* [temporary_id] IF_EXISTS;", None),
    ("S5", "company", "contract", "range contract across tables", "EXPECT company.*.id UNIQUE IF_EXISTS;",
     "EXPECT company.*.id UNIQUE IF_EXISTS;\nEXPECT company.*.age BETWEEN 18 AND 40 IF_EXISTS;", None),
    ("M1", "split", "contract", "train and test schemas differ", "SCHEMA_SAME_AS test EXCEPT [label]",
     "SCHEMA_SAME_AS test", None),
    ("M2", "split", "name", "misspelled column in a contract", "EXPECT train.label BETWEEN 0 AND 1;",
     "EXPECT train.lable BETWEEN 0 AND 1;", None),
    ("I1", "images", "validity", "broken file not removed", "REMOVE_CORRUPT train;", "", None),
    ("I2", "images", "contract", "channel contract broken by a gray image", "EXPECT train MIN_WIDTH 200;",
     "EXPECT train CHANNELS 3;", None),
    ("I3", "images", "contract", "minimum width broken by one image", "EXPECT train MIN_WIDTH 200;",
     "EXPECT train MIN_WIDTH 300;", None),
    ("I4", "images", "lifecycle", "table operation on images", "GRAYSCALE train;", "DROP_NULL train;", None),
    ("I5", "images", "value", "float pixels saved as JPG", "CONVERT train TO JPG;",
     "NORMALIZE train RANGE 0 1;\nCONVERT train TO JPG;", black_images("processed/train")),
    ("I6", "images", "type", "zero width in RESIZE", "RESIZE train TO 224 224", "RESIZE train TO 0 224", None),
    ("L1", "linked", "contract", "labels file misses an image", 'LOAD "labels.csv"', 'LOAD "labels_partial.csv"', None),
    ("L2", "linked", "value", "float pixels saved as PNG", "CONVERT imgs TO NPY;", "CONVERT imgs TO PNG;",
     black_images("processed/ml_ready")),
    ("L3", "linked", "contract", "images linked on the wrong column", "ON filename", "ON label", None),
]


def absolute_paths(text, folder):
    """Makes LOAD paths absolute, so the mutant can live in any folder."""
    import re
    return re.sub(r'(LOAD\w*) "([^"]+)"', lambda m: f'{m.group(1)} "{(folder / m.group(2)).as_posix()}"', text)


def programs():
    """(id, class, description, program text, oracle) for every mutant and every seed (class 'control')."""
    result = []
    for name, path in SEEDS.items():
        result.append((f"C-{name}", "control", "original program", absolute_paths(path.read_text(), path.parent), None))
    for mutant_id, seed, fault, description, old, new, oracle in MUTANTS:
        path = SEEDS[seed]
        text = path.read_text()
        assert old in text, (mutant_id, old)
        result.append((mutant_id, fault, description, absolute_paths(text.replace(old, new, 1), path.parent), oracle))
    return result
