"""Workload-aware baselines for private compilation (RQ8): HDMM and AIM, given the program's numbers.

    eval/.venv-dp/Scripts/python.exe eval/reuse/private_dp_baselines.py hdmm QUERIES --epsilon 1 --seed 0 --out facts.tsv
    eval/.venv-dp/Scripts/python.exe eval/reuse/private_dp_baselines.py aim QUERIES --epsilon 1 --seed 0 --out facts.tsv
    eval/.venv-dp/Scripts/python.exe eval/reuse/private_dp_baselines.py aim-synthetic QUERIES --epsilon 1 --seed 0 --folder DIR

Runs in eval/.venv-dp (Python 3.12; see eval/README.md): hdmm from dpcomp-org/hdmm at 5aa9df93a46e,
mbi from ryan112358/private-pgm at 57e22b46fa, and the AIM mechanism script of the same commit in
eval/.venv-dp/aim/. Both baselines get what tools/release_facts.py gets: the program's numbers and
0, which cut every numeric column into the same cells (below the first number, each number, between
two, above the last, missing), the same columns, and the same budget (epsilon for each table, the
tables in parallel, since a neighbouring dataset differs in one row of one table).

  hdmm           The High-Dimensional Matrix Mechanism (McKenna et al., VLDB 2018), given the
                 program's comparisons as its workload: for every column the number of values
                 below, at most, equal to, at least and above each number, of present values and
                 of missing ones. HDMM's Union template optimizes a strategy for each column (OPT_0:
                 identity, total, p-identity, identity plus total; best of several restarts) and
                 how the budget is split between the columns (weights proportional to the cube
                 root of each column's error). Laplace noise: epsilon-differentially private. The
                 cell counts are estimated by least squares. A cell counts as occupied if its
                 estimate is above tau + 1/2, where tau bounds the noise of every cell with
                 probability 1 - beta (two-sided, union bound over all cells, as in
                 tools/release_facts.py); tau depends only on the strategy and is found by
                 simulating its noise. The facts are read off the occupied cells as
                 tools/release_facts.py reads them.
  aim            AIM (McKenna et al., VLDB 2022), synthetic data over the same cells, with the
                 one-way marginals of the columns asked about as its workload, at (epsilon, 1e-9):
                 a weaker guarantee than epsilon-differential privacy; its default settings (16
                 rounds per column, model size 80). Synthetic rows that no table of the published
                 schema can hold (an INTEGER in a cell without an integer) are dropped. The facts are
                 read off the synthetic data: a cell is occupied if a synthetic row lies in it.
  aim-marginals  The same AIM run, read as tools/release_facts.py reads its histograms: a cell of
                 the model's one-way marginal is occupied if its estimate exceeds tau + 1/2, where tau
                 bounds the Gaussian noise of every cell with probability 1 - beta, from the noise of
                 AIM's measurements of that column (their inverse-variance combination).
  aim-synthetic  The same synthetic data, written as a table where the program loads it (values
                 inside their cells), for a normal compilation: what an analyst who develops
                 against AIM's synthetic data sees.

Used by eval/reuse/private.py; never by the compiler.
"""
import argparse
import contextlib
import io
import math
import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from release_facts import NUMERIC, cells, facts_of, read_queries, read_table  # noqa: E402

BETA = 0.05
DELTA = 1e-9  # AIM's delta, as in its paper
CALIBRATION_SAMPLES = 400_000


def columns_asked(queries_file):
    """The tables of the queries file and, per asked (file, column): (format, type, asked, cells)."""
    numbers, tables = read_queries(queries_file)
    anchors = sorted(set(numbers) | {0.0})
    asked = {}
    for _, _, kind, file, columns in tables:
        for column, ctype, need in columns:
            if need != "none":
                value_cells = cells(anchors) if need == "range" and ctype in NUMERIC else None
                asked[(file, column)] = (kind, ctype, need, value_cells)
    return tables, asked, anchors


def cell_of_rows(values, value_cells):
    """The cell of every row: 0 missing, then 1 + the index of its value cell (or 1: present); -1
    for a present value in no cell (one that does not parse as a number), which tools/release_facts.py
    does not count either."""
    present = values.notna().to_numpy()
    index = np.zeros(len(values), dtype=np.int64)
    if value_cells is None:
        index[present] = 1
        return index
    index[present] = -1
    x = pd.to_numeric(values, errors="coerce").to_numpy(dtype=float)
    for i, (low, high, shape) in enumerate(value_cells):
        inside = (x == low) if shape == "point" else (x > low) & (x < high)
        index[present & inside] = i + 1
    return index


def facts_file(tables, asked, occupied, mechanism):
    """The facts file of tools/release_facts.py, from the occupied cells of every asked column."""
    lines = ["preplang-facts\t1", f"mechanism\t{mechanism}"]
    for load_path, name, kind, file, columns in tables:
        lines.append(f"table\t{load_path}\t{name}\t{kind}")
        for column, ctype, _ in columns:
            if (file, column) not in asked:
                lines.append(f"column\t{column}\t{ctype}\t?\t-inf\t0\tinf\t0\t-\t-\t-\t-")
                continue
            value_cells = asked[(file, column)][3]
            missing, low, ls, high, hs, below, above = facts_of(list(occupied[(file, column)]), value_cells)
            lines.append(f"column\t{column}\t{ctype}\t{missing}\t{low}\t{ls}\t{high}\t{hs}\t"
                         f"{below[0]}\t{below[1]}\t{above[0]}\t{above[1]}")
    return "\n".join(lines) + "\n"


def by_table(asked):
    files = {}
    for (file, column) in asked:
        files.setdefault(file, []).append(column)
    return files


# ---------- HDMM ----------

def comparison_workload(value_cells):
    """The program's comparisons on one column, as rows over its cells [missing, value cells...]:
    missing, present, and for each number below, at most, equal, at least, above."""
    if value_cells is None:
        return np.eye(2)
    n = 1 + len(value_cells)
    rows = [np.eye(n)[0], np.r_[0.0, np.ones(n - 1)]]
    for p, (_, _, shape) in enumerate(value_cells, start=1):
        if shape != "point":
            continue
        for low, high in ((1, p), (1, p + 1), (p, p + 1), (p, n), (p + 1, n)):  # <, <=, ==, >=, >
            row = np.zeros(n)
            row[low:high] = 1.0
            rows.append(row)
    return np.array(rows)


def hdmm_strategy(workloads, seed, restarts=10):
    """HDMM's Union template over the columns: a strategy per column and its weight."""
    from hdmm import matrix, templates
    if not getattr(templates.TemplateStrategy, "_seed_fixed", False):
        # A template without a seed draws one with np.random.randint(2**32 - 1), which overflows
        # numpy's default 32-bit integer on Windows; draw it below 2**31 instead.
        original = templates.TemplateStrategy.__init__

        def init(self, seed=None):
            original(self, int(np.random.randint(0, 2**31 - 1)) if seed is None else seed)
        templates.TemplateStrategy.__init__ = init
        templates.TemplateStrategy._seed_fixed = True
        # EkteloMatrix.sensitivity relies on np.abs calling __abs__ ("this works because np.abs
        # calls self.__abs__"), which scipy's LinearOperator now forbids (__array_ufunc__ = None);
        # call __abs__ directly, as intended.
        matrix.EkteloMatrix.sensitivity = lambda self: np.max(abs(self).sum(axis=0))
    best, best_loss = None, math.inf
    for restart in range(restarts):
        np.random.seed(seed * 1000 + restart)  # the templates draw their initial parameters from np.random
        union = templates.Union([templates.OPT0(W.shape[1]) for W in workloads])
        loss = union.optimize([matrix.EkteloMatrix(W) for W in workloads])
        if loss < best_loss:
            best, best_loss = union, loss
    blocks = [np.asarray(T.strategy().dense_matrix(), dtype=float) for T in best._templates]
    return blocks, np.asarray(best._weights, dtype=float)


def hdmm_release(tables, asked, epsilon, seed):
    """{(file, column): occupied flags} and a description of the strategies."""
    exact = {}
    for key, (kind, _, _, value_cells) in asked.items():
        size = 1 + (len(value_cells) if value_cells else 1)
        index = cell_of_rows(read_table(key[0], kind, [key[1]])[key[1]], value_cells)
        exact[key] = np.bincount(index[index >= 0], minlength=size)
    total_cells = sum(len(x) for x in exact.values())
    rng = np.random.default_rng(seed)
    occupied, chosen = {}, []
    for file, columns in by_table(asked).items():
        keys = [(file, column) for column in columns]
        workloads = [comparison_workload(asked[key][3]) for key in keys]
        blocks, weights = hdmm_strategy(workloads, seed if seed is not None else 0)
        # A row has one cell in every column: the L1 sensitivity is the sum over the columns of
        # each weighted block's largest column sum.
        scaled = [w * A for w, A in zip(weights, blocks)]
        sensitivity = sum(np.abs(A).sum(axis=0).max() for A in scaled)
        scale = sensitivity / epsilon
        calibration = np.random.default_rng(20180801)  # public: the threshold depends on the strategy only
        for key, A in zip(keys, scaled):
            x = np.asarray(exact[key], dtype=float)
            pinv = np.linalg.pinv(A)
            estimate = pinv @ (A @ x + rng.laplace(scale=scale, size=A.shape[0]))
            tail = 1 - BETA / max(total_cells, 1)
            quantiles = []
            for chunk in range(0, CALIBRATION_SAMPLES, 50_000):
                z = calibration.laplace(scale=scale, size=(min(50_000, CALIBRATION_SAMPLES - chunk), A.shape[0]))
                quantiles.append(np.abs(z @ pinv.T))
            tau = np.quantile(np.vstack(quantiles), tail, axis=0)
            occupied[key] = estimate > tau + 0.5
            n = A.shape[1]
            shape = "identity" if A.shape[0] == n and np.allclose(A, np.diag(np.diag(A))) else f"{A.shape[0]} queries"
            chosen.append(f"{key[1]}: {shape} over {n} cells, weight {weights[keys.index(key)]:.2f}")
    return occupied, "; ".join(chosen)


# ---------- AIM ----------

def aim_synthetic(tables, asked, epsilon, seed):
    """{file: (columns, synthetic cell indices per column)} from AIM, one run per table."""
    import jax
    jax.config.update("jax_enable_x64", True)
    jax.config.update("jax_enable_compilation_cache", False)
    sys.path.insert(0, str(ROOT / "eval" / ".venv-dp" / "aim"))
    import aim as aim_script
    from mbi import Dataset, Domain
    # Every measurement AIM takes, with its noise: the marginals of its model are read with a
    # threshold from these (aim-marginals).
    measured = []
    if not getattr(aim_script, "_recording", False):
        original = aim_script.LinearMeasurement

        def recording(y, clique, *args, **kwargs):
            measured.append((tuple(clique), float(kwargs.get("stddev", args[0] if args else 1.0))))
            return original(y, clique, *args, **kwargs)
        aim_script.LinearMeasurement = recording
        aim_script._recording = True
    out, marginals = {}, {}
    for file, columns in by_table(asked).items():
        kind = asked[(file, columns[0])][0]
        frame = read_table(file, kind, columns)
        data = {column: cell_of_rows(frame[column], asked[(file, column)][3]) for column in columns}
        parsed = np.all([index >= 0 for index in data.values()], axis=0)  # as the release, leave out unparsed values
        data = {column: index[parsed] for column, index in data.items()}
        sizes = {column: 1 + (len(asked[(file, column)][3]) if asked[(file, column)][3] else 1) for column in columns}
        dataset = Dataset(data, Domain(tuple(columns), tuple(sizes[c] for c in columns)))
        np.random.seed(seed)  # mbi's synthetic data draws from np.random
        mechanism = aim_script.AIM(epsilon, DELTA, prng=np.random.RandomState(seed))
        measured.clear()
        with contextlib.redirect_stdout(io.StringIO()):
            model, synth = mechanism.run(dataset, [((column,), 1.0) for column in columns])
        for column in columns:
            # The model's marginal combines every measurement of the column; its noise is about that
            # of their inverse-variance combination.
            sigmas = [sigma for clique, sigma in measured if clique == (column,)]
            marginals[(file, column)] = (np.asarray(model.project((column,)).datavector(), dtype=float),
                                         sum(1 / s ** 2 for s in sigmas) ** -0.5 if sigmas else math.inf)
        cells_of = {column: np.asarray(synth.data[column]) for column in columns}
        # The schema is public: a synthetic row whose INTEGER column lies in a cell that holds no
        # integer (strictly between 0 and 1, say) is not a possible row, and is dropped.
        keep = np.ones(len(cells_of[columns[0]]), dtype=bool)
        for column in columns:
            _, ctype, _, value_cells = asked[(file, column)]
            keep &= possible_cells(value_cells, ctype)[cells_of[column]]
        out[file] = {column: index[keep] for column, index in cells_of.items()}
    return out, marginals


def integers_inside(low, high):
    """The integers strictly inside (low, high) as (first, last), or None; an unbounded side
    extends one step of max(1, |edge|) beyond the finite edge, as for the floats."""
    if math.isinf(low):
        low = high - max(1.0, abs(high)) - 1
    if math.isinf(high):
        high = low + max(1.0, abs(low)) + 1
    first, last = math.floor(low) + 1, math.ceil(high) - 1
    return (first, last) if first <= last else None


def possible_cells(value_cells, ctype):
    """Which cells [missing, value cells...] a value of this type can lie in."""
    if value_cells is None or ctype != "INTEGER":
        return np.ones(1 + (len(value_cells) if value_cells else 1), dtype=bool)
    return np.array([True] + [low == round(low) if shape == "point" else integers_inside(low, high) is not None
                              for low, high, shape in value_cells])


def aim_marginals_occupied(marginals):
    """AIM read as the release is read: a cell of a column's model marginal counts as occupied if
    its estimate is above tau + 1/2, where tau bounds every cell's Gaussian noise with probability
    1 - beta (two-sided, union bound over all cells: tau = sigma sqrt(2 ln(2C / beta)))."""
    total = sum(len(estimate) for estimate, _ in marginals.values())
    return {key: estimate > sigma * math.sqrt(2 * math.log(2 * total / BETA)) + 0.5
            for key, (estimate, sigma) in marginals.items()}


def aim_occupied(asked, synthetic):
    occupied = {}
    for (file, column), (_, _, _, value_cells) in asked.items():
        size = 1 + (len(value_cells) if value_cells else 1)
        occupied[(file, column)] = np.bincount(synthetic[file][column], minlength=size) > 0
    return occupied


def values_in_cells(index, value_cells, ctype, rng):
    """Values for synthetic cell indices: missing, the number of a point cell, or a value strictly
    inside an open cell (an integer if the column is INTEGER and one fits)."""
    from private_baselines import placeholder
    if value_cells is None:
        values = placeholder(ctype, len(index))
        values[index == 0] = pd.NA
        return values
    x = np.full(len(index), np.nan)
    for i, (low, high, shape) in enumerate(value_cells, start=1):
        here = index == i
        if not here.any():
            continue
        if shape == "point":
            x[here] = low
            continue
        if ctype == "INTEGER":  # aim_synthetic dropped the rows of cells without an integer
            first, last = integers_inside(low, high)
            x[here] = rng.integers(first, last + 1, size=int(here.sum()))
            continue
        lo = high - max(1.0, abs(high)) if math.isinf(low) else low
        hi = low + max(1.0, abs(low)) if math.isinf(high) else high
        x[here] = lo + (hi - lo) * rng.uniform(0.05, 0.95, size=int(here.sum()))
    if ctype == "INTEGER":
        values = pd.Series(pd.array(np.where(np.isnan(x), 0, x).astype("int64"), dtype="Int64"))
    else:
        values = pd.Series(x, dtype="Float64")
    values[index == 0] = pd.NA
    return values


def write_aim_synthetic(folder, tables, asked, synthetic, seed):
    from private_baselines import placeholder
    rng = np.random.default_rng(None if seed is None else seed + 1)
    for load_path, _, _, file, columns in tables:
        rows = len(next(iter(synthetic[file].values()))) if file in synthetic else 100
        frame = {}
        for column, ctype, _ in columns:
            if (file, column) in asked:
                frame[column] = values_in_cells(synthetic[file][column], asked[(file, column)][3], ctype, rng)
            else:
                frame[column] = placeholder(ctype, rows)
        target = Path(folder) / load_path
        target.parent.mkdir(parents=True, exist_ok=True)
        table = pd.DataFrame(frame)
        if target.suffix.lower() == ".parquet":
            table.to_parquet(target, index=False)
        else:
            table.to_csv(target, index=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("method", choices=("hdmm", "aim", "aim-marginals", "aim-synthetic"))
    parser.add_argument("queries")
    parser.add_argument("--epsilon", type=float, required=True)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--out", help="facts file (hdmm, aim)")
    parser.add_argument("--folder", help="the analyst's folder, for the synthetic tables (aim-synthetic)")
    args = parser.parse_args()
    tables, asked, _ = columns_asked(args.queries)
    if args.method == "hdmm":
        occupied, strategy = hdmm_release(tables, asked, args.epsilon, args.seed)
        text = facts_file(tables, asked, occupied,
                          f"HDMM (Union of OPT_0) for the program's comparisons, epsilon {args.epsilon:g} per table, "
                          f"beta {BETA:g}: {strategy}")
        Path(args.out).write_text(text, encoding="utf-8")
        return
    synthetic, marginals = aim_synthetic(tables, asked, args.epsilon, args.seed)
    if args.method == "aim-marginals":
        text = facts_file(tables, asked, aim_marginals_occupied(marginals),
                          f"AIM's model marginals over the program's cells, ({args.epsilon:g}, {DELTA:g}) per table, "
                          f"read with a Gaussian threshold at beta {BETA:g}")
        Path(args.out).write_text(text, encoding="utf-8")
    elif args.method == "aim":
        text = facts_file(tables, asked, aim_occupied(asked, synthetic),
                          f"AIM synthetic data over the program's cells, ({args.epsilon:g}, {DELTA:g}) per table")
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        write_aim_synthetic(args.folder, tables, asked, synthetic, args.seed)


if __name__ == "__main__":
    os.environ.setdefault("JAX_PLATFORMS", "cpu")
    main()
