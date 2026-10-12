"""Releasing differentially private facts for a private compilation (run by the data owner).

    preplang program.prep --queries                      # writes output/preplang_queries.tsv
    py -3 tools/release_facts.py output/preplang_queries.tsv --epsilon 1 [--seed 0] [--out facts.tsv]
    preplang program.prep --facts facts.tsv              # the analyst: compiles without the data

A private compilation never opens the input files. The compiler's abstract state
needs, per column, whether values can be missing and bounds on the values, and it
compares bounds only with the numbers in the program (and 0). So the only questions
it needs answered are threshold questions at those numbers: how many values are
missing, how many are below k, how many are above k. Each is a count, which one row
changes by at most 1.

They are answered with AboveThreshold (the sparse vector technique, Dwork and Roth,
"The Algorithmic Foundations of Differential Privacy", Algorithm 1), which releases
only the first question in a sequence whose noisy count reaches a noisy threshold:

  * the lower bound of a column: "values < k1?", "values <= k1?", "values < k2?", ...
    for the numbers k1 < k2 < ... in the program. The counts only grow along the
    sequence, so the first one that is not about 0 marks the tightest bound that
    holds (for example "every value is >= k1" if the second question stops it);
  * the upper bound, the same from the largest number down;
  * missing values: one question.

Each sequence costs epsilon_chain, whatever its length; the total epsilon is divided
equally between the sequences (sequential composition), so the released file is
epsilon-differentially private for adding or removing one row of any table. Column
names and types are treated as public, as the header of a file usually is.

The answers are noisy, and that is safe: the compiler treats them as estimates, and
the generated program checks the certificate of every removed operation on the real
data, so a wrong answer costs a deoptimization, never a wrong result. The thresholds
are set from --beta so that, with probability at least 1 - beta over the noise, a
sequence stops only at a question whose true count is at least 1. Then every "values
below k exist" in the file is true (the compiler's "likely violated" warnings name real
violations), and no bound is looser than the tightest true one at the program's numbers
(the private compilation removes at least what a normal compilation of the file removes).
Small counts are claimed to be 0: those false facts cost deoptimizations.

The facts file gives, per column: whether missing values were seen (1) or the answer is
that none are missing (0); the two bounds, each with "strict"; and where each bound's
sequence stopped (values below or above that number were seen), or "-".

With --epsilon inf the counts are exact (no noise; nothing is private): the facts the
same questions give without noise, for comparison.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
import pandas as pd


def read_queries(path):
    """-> (constants, [(load path, name, format, file, [(column, type, needs range)])])"""
    constants, tables = [], []
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    if not lines or lines[0].split("\t") != ["preplang-queries", "1"]:
        sys.exit(f"{path} is not written by preplang --queries")
    for line in lines[1:]:
        fields = line.split("\t")
        if fields[0] == "constants":
            constants = [float(value) for value in fields[1:]]
        elif fields[0] == "table":
            tables.append((fields[1], fields[2], fields[3], fields[4], []))
        elif fields[0] == "column":
            tables[-1][4].append((fields[1], fields[2], fields[3] == "range" if len(fields) > 3 else True))
    return constants, tables


def read_table(file, kind, types):
    """The values as the generated program reads them: an empty CSV cell is missing."""
    if kind == "parquet":
        return pd.read_parquet(file, columns=list(types))
    return pd.read_csv(file, usecols=list(types), keep_default_na=False, na_values=[""], dtype=str)


def numbers(series, kind):
    """The present values of a numeric column as floats (NaN where missing)."""
    if kind == "parquet":
        return pd.to_numeric(series, errors="coerce").astype(float).to_numpy()
    return pd.to_numeric(series, errors="coerce").to_numpy(dtype=float)


class AboveThreshold:
    """One run of the sparse vector technique for counting queries (sensitivity 1)."""

    def __init__(self, epsilon, threshold, rng):
        self.epsilon, self.rng = epsilon, rng
        self.noisy_threshold = threshold if math.isinf(epsilon) else threshold + rng.laplace(scale=2 / epsilon)

    def reaches(self, count):
        noise = 0.0 if math.isinf(self.epsilon) else self.rng.laplace(scale=4 / self.epsilon)
        return count + noise >= self.noisy_threshold


def first_reached(counts, epsilon, threshold, rng):
    """Index of the first count whose noisy value reaches the noisy threshold, or None.
    Only this index is released; the sequence stops there."""
    test = AboveThreshold(epsilon, threshold, rng)
    for index, count in enumerate(counts):
        if test.reaches(count):
            return index
    return None


def threshold(epsilon, questions, beta):
    """The threshold of one question sequence. With probability at least 1 - beta, none of
    the `questions` noises is above a = (4/eps) ln(questions/beta) and the threshold's noise
    is not below -b = -(2/eps) ln(1/beta) (Laplace tails, union bound). A sequence then stops
    only at a question whose count is at least threshold - a - b = 1/2, so at least 1: every
    reported "values below k" is true, and a bound is never looser than the true one."""
    if math.isinf(epsilon):
        return 0.5
    return 4 / epsilon * math.log(questions / beta) + 2 / epsilon * math.log(1 / beta) + 0.5


def release(queries, epsilon, beta, seed):
    constants, tables = queries
    anchors = sorted(set(constants) | {0.0})
    chains = sum(len(columns) + 2 * sum(1 for _, kind, rng_ in columns if kind in ("INTEGER", "FLOAT") and rng_)
                 for *_, columns in tables)
    per_chain = epsilon / chains if chains else epsilon
    per_beta = beta / chains if chains else beta  # so that all sequences are right together w.p. 1 - beta
    bound_t, missing_t = threshold(per_chain, 2 * len(anchors), per_beta), threshold(per_chain, 1, per_beta)
    rng = np.random.default_rng(seed)
    out = ["preplang-facts\t1",
           f"mechanism\tAboveThreshold, epsilon {epsilon:g} in total over {chains} question sequences "
           f"({per_chain:.4g} each), beta {beta:g}: thresholds {bound_t:.4g} (bounds) and {missing_t:.4g} "
           f"(missing values) counts, seed {seed}"]
    for load_path, name, kind, file, columns in tables:
        frame = read_table(file, kind, [column for column, *_ in columns])
        out.append(f"table\t{load_path}\t{name}\t{kind}")
        for column, ctype, wants_range in columns:
            values = frame[column]
            missing = int(values.isna().sum())
            # "1": missing values were seen; "0": the answer is that none are missing
            may_miss = first_reached([missing], per_chain, missing_t, rng) is not None
            low, low_strict, high, high_strict = "-inf", 0, "inf", 0
            seen_below, seen_above = ("-", "-"), ("-", "-")  # where each sequence stopped, if it did
            if ctype in ("INTEGER", "FLOAT") and wants_range:
                x = numbers(values, kind)
                x = x[~np.isnan(x)]
                # lower bound: questions "values < k?", "values <= k?" for k ascending; the bound is the
                # last question before the sequence stopped ("< k" answered no: every value >= k)
                lower = [q for k in anchors for q in ((k, "<", int((x < k).sum())), (k, "<=", int((x <= k).sum())))]
                stop = first_reached([count for *_, count in lower], per_chain, bound_t, rng)
                if stop is not None:
                    seen_below = (repr(float(lower[stop][0])), lower[stop][1])
                if stop != 0:
                    k, op, _ = lower[(stop if stop is not None else len(lower)) - 1]
                    low, low_strict = repr(float(k)), int(op == "<=")
                upper = [q for k in reversed(anchors) for q in ((k, ">", int((x > k).sum())), (k, ">=", int((x >= k).sum())))]
                stop = first_reached([count for *_, count in upper], per_chain, bound_t, rng)
                if stop is not None:
                    seen_above = (repr(float(upper[stop][0])), upper[stop][1])
                if stop != 0:
                    k, op, _ = upper[(stop if stop is not None else len(upper)) - 1]
                    high, high_strict = repr(float(k)), int(op == ">=")
                # Noise can claim bounds that no value satisfies (both sequences stopped late).
                # Dropping them is computed from the released answers alone, so it costs nothing.
                if float(low) > float(high) or (float(low) == float(high) and (low_strict or high_strict)):
                    low, low_strict, high, high_strict = "-inf", 0, "inf", 0
            out.append(f"column\t{column}\t{ctype}\t{int(may_miss)}\t{low}\t{low_strict}\t{high}\t{high_strict}\t"
                       f"{seen_below[0]}\t{seen_below[1]}\t{seen_above[0]}\t{seen_above[1]}")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("queries", help="output/preplang_queries.tsv, from preplang --queries")
    parser.add_argument("--epsilon", type=float, required=True, help="total privacy budget (inf: exact counts)")
    parser.add_argument("--beta", type=float, default=0.05,
                        help="probability that some released answer is pessimistic by noise (default 0.05)")
    parser.add_argument("--seed", type=int, default=None, help="for repeatable experiments only")
    parser.add_argument("--out", help="facts file to write (default: standard output)")
    args = parser.parse_args()
    text = release(read_queries(args.queries), args.epsilon, args.beta, args.seed)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
