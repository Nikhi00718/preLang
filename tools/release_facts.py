"""Releasing differentially private facts for a private compilation (run by the data owner).

    preplang program.prep --queries                      # writes output/preplang_queries.tsv
    py -3 tools/release_facts.py output/preplang_queries.tsv --epsilon 1 --schema-declared --out facts.tsv
    preplang program.prep --facts facts.tsv              # the analyst: compiles without the data

One release can serve several programs over the same files (a corpus, or the edits of one
program): give every queries file and an output folder; the budget is spent once.

    py -3 tools/release_facts.py a/queries.tsv b/queries.tsv --epsilon 1 --schema-declared --out-dir facts/

What is asked. The compiler's abstract state needs, per column, whether values can be
missing and bounds on the values, and it compares bounds only with the numbers written in
the program (and 0). Those numbers k1 < k2 < ... < km cut a column into cells: below k1,
exactly k1, between k1 and k2, exactly k2, ..., above km, and missing. Every decision of the
analysis depends only on which cells hold some row, so that is all that is released. A row
lies in exactly one cell of a column, so the histogram of one column changes by 1 when a row
is added or removed: one Laplace histogram per column (noise Lap(1/epsilon_column) in every
cell) is epsilon_column-differentially private. A row is in every column of its table, so
those columns compose sequentially, and a neighbouring dataset differs in one row of one
table, so the tables compose in parallel: epsilon is divided equally between the columns of
each table asked about, and every table gets the whole epsilon.
Only columns the program names are asked about (all of them if it uses * or a DROP_NULL of
whole rows), and of those only missing values if the program compares no number with them.

What is public. Column names and types (the schema) and the program are inputs to the
release, not outputs: the guarantee holds for a fixed schema. preplang --queries writes the
types it infers from the data; that is the data owner's draft. A type inferred from the
values (INTEGER until one cell holds 3.5, or a Parquet column written as float because it
has missing values) is a fact about the data: the owner must review the schema and publish
the types as declared, not as found, and confirm it with --schema-declared. Which columns
are asked about depends only on the
program and the schema, never on an answer, so the release is non-adaptive.

What the analyst learns. A cell counts as occupied if its noisy count is above
t + 1/2, where t = ln(cells / beta) / epsilon_column. With probability at least 1 - beta
over the noise, every noise in the release is smaller than t (Laplace tails, union bound
over all cells), and then:
  * a cell is occupied only if it holds at least one row, so every bound in the file holds
    for the bounds at the program's numbers, and every "likely violated" warning the
    compiler gives names a contract the data breaks;
  * a cell with at least 2t + 1 rows is occupied, so a fact in the file (no missing values,
    nothing below k) is false only if fewer than 2t + 1 rows break it.
Such false facts cost deoptimizations when the program runs, never a wrong result: the
generated program checks the certificate of every removed operation on the real data.
No differentially private release can certify that no row breaks a fact (that output must
also occur, with probability at least exp(-k epsilon) times as often, when k rows break it),
which is why those run-time checks are needed.

The facts file gives, per column: whether missing values were seen (1), the answer that none
are missing (0), or not asked (?); the two bounds, each with "strict"; and the cells that
give evidence: values below k ("<" or "<=" k) and above k (">" or ">=" k), or "-".

A person may have many rows; then a row is the wrong unit to protect. With --unit COLUMN,
every row with the same value of COLUMN belongs to one unit, at most --max-rows rows of
each unit are counted (the first ones), and the release is epsilon-differentially private
for adding or removing a whole unit: one unit changes each count by at most max-rows, and
since a unit may have rows in every table, all columns of all tables share epsilon. Counting
only some of a unit's rows makes the counts wrong, which elsewhere in differential privacy
is a cost in accuracy. Here it is not: a fact the truncation makes false (a missing value
in a row that was not counted) is checked on the real data when the program runs, so it
costs a deoptimization, never a wrong result.

--seed makes experiments repeatable. The seed is never written to the facts file: whoever
knows it can recompute the noise, so it must not be published. With --epsilon inf the counts
are exact (no noise, not private), for comparison.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd

NUMERIC = ("INTEGER", "FLOAT")


def read_queries(path):
    """-> (numbers, [(load path, name, format, file, [(column, type, asked)])]); asked is
    "range", "missing" or "none"."""
    numbers, tables = [], []
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    if not lines or lines[0].split("\t") != ["preplang-queries", "1"]:
        sys.exit(f"{path} is not written by preplang --queries")
    for line in lines[1:]:
        fields = line.split("\t")
        if fields[0] == "constants":
            numbers = [float(value) for value in fields[1:]]
        elif fields[0] == "table":
            tables.append((fields[1], fields[2], fields[3], fields[4], []))
        elif fields[0] == "column":
            tables[-1][4].append((fields[1], fields[2], fields[3]))
    return numbers, tables


def read_table(file, kind, columns):
    """The values as the generated program reads them: an empty CSV cell is missing."""
    if kind == "parquet":
        return pd.read_parquet(file, columns=columns)
    return pd.read_csv(file, usecols=columns, keep_default_na=False, na_values=[""], dtype=str)


def cells(anchors):
    """The cells the numbers cut a numeric column into: (low, high, kind), kind "open" for
    the values strictly between low and high, "point" for exactly low (= high)."""
    edges = [-math.inf] + anchors + [math.inf]
    out = []
    for i in range(len(edges) - 1):
        if i > 0:
            out.append((edges[i], edges[i], "point"))
        out.append((edges[i], edges[i + 1], "open"))
    return out


def counts(values, kind, ctype, asked, anchors):
    """The exact histogram of one column: [missing, then one count per value cell]."""
    present = values.notna()
    missing = int((~present).sum())
    if asked != "range" or ctype not in NUMERIC:
        return [missing, int(present.sum())], None
    x = pd.to_numeric(values[present], errors="coerce").to_numpy(dtype=float)
    x = x[~np.isnan(x)]
    found = []
    for low, high, shape in cells(anchors):
        inside = (x == low) if shape == "point" else (x > low) & (x < high)
        found.append(int(inside.sum()))
    return [missing] + found, cells(anchors)


def facts_of(occupied, value_cells):
    """The facts a column's occupied cells give: (missing, low, low strict, high, high strict,
    seen below, seen above)."""
    missing = "1" if occupied[0] else "0"
    low, low_strict, high, high_strict = "-inf", 0, "inf", 0
    below, above = ("-", "-"), ("-", "-")
    if value_cells is None:
        return missing, low, low_strict, high, high_strict, below, above
    seen = [cell for cell, here in zip(value_cells, occupied[1:]) if here]
    if not seen:
        return missing, low, low_strict, high, high_strict, below, above  # no value seen: no bounds
    first, last = seen[0], seen[-1]
    if not math.isinf(first[0]):
        low, low_strict = repr(first[0]), int(first[2] == "open")
    if not math.isinf(last[1]):
        high, high_strict = repr(last[1]), int(last[2] == "open")
    if not math.isinf(first[1]):  # values below first[1] (or at it, for a point) exist
        below = (repr(first[1]), "<" if first[2] == "open" else "<=")
    if not math.isinf(last[0]):   # values above last[0] (or at it) exist
        above = (repr(last[0]), ">" if last[2] == "open" else ">=")
    return missing, low, low_strict, high, high_strict, below, above


def release(queries_files, epsilon, beta, seed, unit=None, max_rows=1):
    """One release for every queries file: {queries file: facts file text}. With `unit`, the
    privacy is for a unit (a person: every row with the same value of that column), not a row."""
    parsed = {path: read_queries(path) for path in queries_files}
    for _, tables in parsed.values():
        for load_path, _, _, _, columns in tables:
            if unit and unit not in [column for column, *_ in columns]:
                sys.exit(f"{load_path} has no column {unit!r}: its rows cannot be counted per unit")
    # Every (file, column) asked about by some program, with the numbers of all of them.
    asked, anchors, kinds, types = {}, {}, {}, {}
    rank = {"none": 0, "missing": 1, "range": 2}
    for numbers, tables in parsed.values():
        for _, _, kind, file, columns in tables:
            for column, ctype, need in columns:
                key = (file, column)
                if need == "none":
                    continue
                asked[key] = max(asked.get(key, "missing"), need, key=rank.get)
                anchors.setdefault(key, set()).update(numbers + [0.0])
                kinds[key], types[key] = kind, ctype
    histograms, frames = {}, {}
    for (file, column) in asked:
        if file not in frames:
            names = sorted({c for (f, c) in asked if f == file} | ({unit} if unit else set()))
            frames[file] = read_table(file, kinds[(file, column)], names)
            if unit:
                # At most max_rows rows of each unit (person): the facts describe the truncated table,
                # which the run-time checks make harmless, and one unit changes each count by at most
                # max_rows. Rows without a unit are not counted: merged into one unit, a person without
                # one could change counts by more than max_rows.
                frame = frames[file][frames[file][unit].notna()]
                frames[file] = frame[frame.groupby(unit).cumcount() < max_rows]
        histograms[(file, column)] = counts(frames[file][column], kinds[(file, column)], types[(file, column)],
                                            asked[(file, column)], sorted(anchors[(file, column)]))
    # A neighbouring dataset differs in one row of one table: the tables compose in parallel, the
    # columns of one table sequentially, so each table's columns share the whole epsilon. A unit may
    # have rows in several tables, so with units every column of every table shares it, and each
    # count changes by up to max_rows.
    columns_of = {file: sum(1 for (f, _) in asked if f == file) for (file, _) in asked}
    per_column = {file: epsilon / (len(asked) if unit else count) for file, count in columns_of.items()}
    sensitivity = max_rows if unit else 1
    total_cells = sum(len(found) for found, _ in histograms.values())
    threshold = {file: 0.5 if math.isinf(e) else sensitivity * math.log(max(total_cells, 1) / beta) / e + 0.5
                 for file, e in per_column.items()}
    rng = np.random.default_rng(seed)
    occupied = {}
    for (file, column), (found, value_cells) in histograms.items():
        e = per_column[file]
        noise = np.zeros(len(found)) if math.isinf(e) else rng.laplace(scale=sensitivity / e, size=len(found))
        occupied[(file, column)] = (list(np.asarray(found) + noise > threshold[file]), value_cells)
    privacy = (f"for each unit {unit!r} (at most {max_rows} rows of it counted), epsilon {epsilon:g} over all "
               f"{len(asked)} columns") if unit else (
        f"epsilon {epsilon:g} for each of {len(columns_of)} tables, split over its columns "
        f"({', '.join(f'{c} columns' for c in columns_of.values())})")
    mechanism = (f"Laplace histograms at the program's numbers, {privacy}, beta {beta:g}: a cell counts as "
                 f"seen above {', '.join(f'{v:.4g}' for v in threshold.values())}")
    out = {}
    for path, (_, tables) in parsed.items():
        lines = ["preplang-facts\t1", f"mechanism\t{mechanism}"]
        for load_path, name, kind, file, columns in tables:
            lines.append(f"table\t{load_path}\t{name}\t{kind}")
            for column, ctype, _ in columns:
                if (file, column) not in asked:
                    lines.append(f"column\t{column}\t{ctype}\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-")
                    continue
                missing, low, ls, high, hs, below, above = facts_of(*occupied[(file, column)])
                lines.append(f"column\t{column}\t{ctype}\t{missing}\t{low}\t{ls}\t{high}\t{hs}\t"
                             f"{below[0]}\t{below[1]}\t{above[0]}\t{above[1]}")
        out[path] = "\n".join(lines) + "\n"
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                     fromfile_prefix_chars="@")
    parser.add_argument("queries", nargs="+",
                        help="output/preplang_queries.tsv of each program (@list.txt: one per line)")
    parser.add_argument("--epsilon", type=float, required=True, help="total privacy budget (inf: exact counts)")
    parser.add_argument("--beta", type=float, default=0.05,
                        help="probability that some released cell is wrong by more than the bound (default 0.05)")
    parser.add_argument("--seed", type=int, default=None, help="for repeatable experiments only; never published")
    parser.add_argument("--out", help="facts file to write, for one queries file (default: standard output)")
    parser.add_argument("--out-dir", help="folder for one facts file per queries file, named 1.tsv, 2.tsv, ...")
    parser.add_argument("--unit", help="column naming the person (or other unit) to protect, instead of a row")
    parser.add_argument("--max-rows", type=int, default=1, help="rows of one unit that are counted (default 1)")
    parser.add_argument("--schema-declared", action="store_true",
                        help="confirm that the column types in the queries files are the published schema, "
                             "declared rather than inferred from these values (required)")
    args = parser.parse_args()
    if not args.schema_declared:
        sys.exit("The column types in the queries file were inferred from the data, and a type inferred from the "
                 "values is a fact about them (one 3.5 makes a column FLOAT). Review them, publish them as the "
                 "declared schema, and run again with --schema-declared.")
    texts = release(args.queries, args.epsilon, args.beta, args.seed, args.unit, args.max_rows)
    if args.out_dir:
        Path(args.out_dir).mkdir(parents=True, exist_ok=True)
        for i, path in enumerate(args.queries, 1):
            (Path(args.out_dir) / f"{i}.tsv").write_text(texts[path], encoding="utf-8")
    elif args.out:
        Path(args.out).write_text(texts[args.queries[0]], encoding="utf-8")
    else:
        sys.stdout.write(texts[args.queries[0]])


if __name__ == "__main__":
    main()
