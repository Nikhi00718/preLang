"""Baselines for private compilation (RQ8): the same budget, spent without the program's numbers.

  generic    one Laplace histogram per column over fixed bins that do not depend on the data or
             the program: a logarithmic grid, [0, 0.001), [0.001, 0.01), ..., [100000, 1000000),
             the same for negative values, and the two outer bins. Same columns, same epsilon
             per column and same threshold rule as tools/release_facts.py; the facts are read
             off the occupied bins.
  synthetic  a table sampled from the same noisy histograms (columns independent, values uniform
             in their bin, missing values in proportion) and compiled normally: what an analyst
             sees who develops against differentially private synthetic or dummy data. For facts
             about single columns, one-column marginals are all such a generator could use.

Used by eval/reuse/private.py; never by the compiler.
"""
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from release_facts import NUMERIC, read_queries, read_table  # noqa: E402

POSITIVE = [10.0 ** k for k in range(-3, 7)]  # 0.001 ... 1,000,000
EDGES = [-math.inf] + [-x for x in reversed(POSITIVE)] + [0.0] + POSITIVE + [math.inf]  # bins [e_i, e_i+1)


def histograms(queries_file, epsilon, beta, seed):
    """The generic release for one program: the tables of the queries file, and per asked
    (file, column): (binned or not, noisy counts [missing, bins...], occupied flags)."""
    _, tables = read_queries(queries_file)
    asked = {}
    for _, _, kind, file, columns in tables:
        for column, ctype, need in columns:
            if need != "none":
                asked[(file, column)] = (kind, ctype, need)
    exact = {}
    for (file, column), (kind, ctype, need) in asked.items():
        values = read_table(file, kind, [column])[column]
        present = values.notna()
        found, binned = [int((~present).sum())], need == "range" and ctype in NUMERIC
        if binned:
            x = pd.to_numeric(values[present], errors="coerce").to_numpy(dtype=float)
            x = x[~np.isnan(x)]
            index = np.searchsorted(EDGES, x, side="right") - 1  # EDGES[i] <= x < EDGES[i+1]
            found += [int(n) for n in np.bincount(index, minlength=len(EDGES) - 1)]
        else:
            found.append(int(present.sum()))
        exact[(file, column)] = (binned, found)
    # The same budget as tools/release_facts.py: epsilon per table, split over its columns.
    columns_of = {file: sum(1 for (f, _) in asked if f == file) for (file, _) in asked}
    cells = sum(len(found) for _, found in exact.values())
    rng = np.random.default_rng(seed)
    released = {}
    for (file, column), (binned, found) in exact.items():
        e = epsilon / columns_of[file]
        t = 0.0 if math.isinf(e) else math.log(max(cells, 1) / beta) / e
        noise = np.zeros(len(found)) if math.isinf(e) else rng.laplace(scale=1 / e, size=len(found))
        noisy = np.asarray(found, dtype=float) + noise
        released[(file, column)] = (binned, noisy, noisy > t + 0.5)
    return tables, released


def generic_facts(queries_file, epsilon, beta, seed):
    """The facts file of the generic release: bounds and evidence from the occupied bins
    [e_i, e_i+1), which give values >= e_i and values < e_i+1."""
    tables, released = histograms(queries_file, epsilon, beta, seed)
    lines = ["preplang-facts\t1",
             f"mechanism\tgeneric Laplace histograms over fixed logarithmic bins, epsilon {epsilon:g} per table"]
    for load_path, name, kind, file, columns in tables:
        lines.append(f"table\t{load_path}\t{name}\t{kind}")
        for column, ctype, _ in columns:
            if (file, column) not in released:
                lines.append(f"column\t{column}\t{ctype}\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-")
                continue
            binned, _, occupied = released[(file, column)]
            missing = "1" if occupied[0] else "0"
            low, high, high_strict, below, above = "-inf", "inf", 0, ("-", "-"), ("-", "-")
            seen = [i for i, here in enumerate(occupied[1:]) if here]
            if binned and seen:
                first, last = seen[0], seen[-1]
                if not math.isinf(EDGES[first]):
                    low = repr(EDGES[first])
                if not math.isinf(EDGES[last + 1]):
                    high, high_strict = repr(EDGES[last + 1]), 1
                if not math.isinf(EDGES[first + 1]):
                    below = (repr(EDGES[first + 1]), "<")
                if not math.isinf(EDGES[last]):
                    above = (repr(EDGES[last]), ">=")
            lines.append(f"column\t{column}\t{ctype}\t{missing}\t{low}\t0\t{high}\t{high_strict}\t"
                         f"{below[0]}\t{below[1]}\t{above[0]}\t{above[1]}")
    return "\n".join(lines) + "\n"


def placeholder(ctype, rows):
    """Values of a column whose values the release says nothing about: only its type matters."""
    if ctype == "INTEGER":
        return pd.Series([0] * rows, dtype="Int64")
    if ctype == "FLOAT":
        return pd.Series([0.5] * rows, dtype="Float64")
    if ctype == "BOOLEAN":
        return pd.Series([True] * rows, dtype="boolean")
    if ctype == "DATETIME":
        return pd.Series(pd.to_datetime(["2000-01-01"] * rows))
    return pd.Series(["x"] * rows, dtype="string")


def write_synthetic(folder, queries_file, epsilon, beta, seed):
    """Samples a synthetic table from the generic release for every LOAD of the program and
    writes it where the LOAD reads it (CSV or Parquet). Values are uniform in their bin; the
    two outer bins are sampled within one decade of their finite edge."""
    tables, released = histograms(queries_file, epsilon, beta, seed)
    rng = np.random.default_rng(None if seed is None else seed + 1)
    low_edges = np.array([-1e7] + EDGES[1:-1])
    high_edges = np.array(EDGES[1:-1] + [1e7])
    for load_path, _, _, file, columns in tables:
        totals = [max(1.0, float(np.clip(noisy, 0, None).sum()))
                  for (f, _), (_, noisy, _) in released.items() if f == file]
        rows = int(round(np.median(totals))) if totals else 100
        frame = {}
        for column, ctype, _ in columns:
            if (file, column) not in released:
                frame[column] = placeholder(ctype, rows)
                continue
            binned, noisy, occupied = released[(file, column)]
            # Only occupied bins, as a careful generator would (post-processing, no extra budget).
            weights = np.where(occupied, np.clip(noisy, 0, None), 0.0)
            weights = weights / weights.sum() if weights.sum() > 0 else np.full(len(weights), 1 / len(weights))
            cell = rng.choice(len(weights), size=rows, p=weights)  # 0: missing
            if binned:
                bins = np.clip(cell - 1, 0, len(low_edges) - 1)
                x = rng.uniform(low_edges[bins], high_edges[bins])
                values = pd.Series(np.floor(x).astype("int64"), dtype="Int64") if ctype == "INTEGER" \
                    else pd.Series(x, dtype="Float64")
            else:
                values = placeholder(ctype, rows)
            values[cell == 0] = pd.NA
            frame[column] = values
        target = Path(folder) / load_path
        target.parent.mkdir(parents=True, exist_ok=True)
        table = pd.DataFrame(frame)
        if target.suffix.lower() == ".parquet":
            table.to_parquet(target, index=False)
        else:
            table.to_csv(target, index=False)
