"""Correctness: does a compiled program compute what the program says?

Random table programs over random CSV files, each

    compiled with --O0 and run,
    compiled with the optimizer (the default) and run,
    evaluated by reference(): an interpreter of the language written here in plain
    Python, without pandas and without any code of the compiler,

and the three results compared: the same outcome (saved rows, or a contract that fails
at the same line) and the same output, cell by cell (floats to 1e-9 relative).
The compiler stops a program at compile time when it proves a contract violated; that
counts as the same outcome as the reference failing at that line.

Programs may JOIN a random lookup table and AGGREGATE. Each optimized program is also
reused on a second random batch (other files with the same columns, given as name=file):
it must refuse the batch, or end as the reference does on that batch (the same rows, or a
failure at the same line). Anything else is a wrong reuse.

    py -3 eval/compilation/differential.py [--programs 600] [--seed 1] [--workers 6]

Writes eval/results/differential.json.gz and .md.
"""
import argparse
import contextlib
import csv
import gzip
import io
import json
import math
import os
import random
import re
import runpy
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "eval" / "work" / os.environ.get("PREPLANG_DIFF_WORK", "differential")
# PREPLANG_RESULTS: another folder for the results (eval/evaluate.py --quick)
RESULTS = Path(os.environ.get("PREPLANG_RESULTS") or ROOT / "eval" / "results")
# PREPLANG_COMPILER: another build to test, such as one before a fix
COMPILER = Path(os.environ.get("PREPLANG_COMPILER") or ROOT / "build" / ("Release/preplang.exe" if os.name == "nt" else "preplang"))

# ---------------------------------------------------------------- random data and programs

CATEGORIES = ["red", "green", "blue", "grey"]


def random_table(rng):
    """Columns: id (unique INTEGER), two INTEGER, two FLOAT, one STRING, one BOOLEAN;
    every column but id has missing values in some tables."""
    rows = rng.randint(4, 40)
    columns = {"id": ("INTEGER", list(range(1, rows + 1)))}
    def nulls(values):
        rate = rng.choice([0, 0, 0.1, 0.3])
        out = [None if rng.random() < rate else v for v in values]
        out[rng.randrange(rows)] = values[0]  # one value at least: a column of only missing cells is STRING
        return out
    for name in ("a", "b"):
        low = rng.randint(-50, 50)
        columns[name] = ("INTEGER", nulls([rng.randint(low, low + rng.randint(0, 200)) for _ in range(rows)]))
    for name in ("x", "y"):
        columns[name] = ("FLOAT", nulls([round(rng.uniform(-100, 100), 2) + 0.25 for _ in range(rows)]))
    columns["cat"] = ("STRING", nulls([rng.choice(CATEGORIES) for _ in range(rows)]))
    columns["flag"] = ("BOOLEAN", nulls([rng.random() < 0.5 for _ in range(rows)]))
    columns["k"] = ("INTEGER", nulls([rng.randint(1, 6) for _ in range(rows)]))
    return columns


def flip_kind(rng, table):
    """Now and then (3 in 10) one numeric column of a second batch has the other numeric type:
    whole numbers become x.5, decimals are rounded. Returns its name, or None."""
    if rng.random() >= 0.3:
        return None
    name = rng.choice(["a", "b", "x", "y"])
    kind, values = table[name]
    if kind == "INTEGER":
        table[name] = ("FLOAT", [None if v is None else v + 0.5 for v in values])
    else:
        table[name] = ("INTEGER", [None if v is None else int(round(v)) for v in values])
    return name


def random_lookup(rng):
    """The lookup table u: key k (some of 1..6, now and then one repeated), w (INTEGER) and
    label (STRING), with missing values in some tables."""
    keys = rng.sample(range(1, 7), rng.randint(2, 5))
    if rng.random() < 0.12:
        keys.append(keys[0])  # a repeated key: JOIN must stop
    rng.shuffle(keys)
    def nulls(values):
        out = [None if rng.random() < rng.choice([0, 0.2]) else v for v in values]
        out[0] = values[0]
        return out
    return {"k": ("INTEGER", keys), "w": ("INTEGER", nulls([rng.randint(-20, 40) for _ in keys])),
            "label": ("STRING", nulls([rng.choice(CATEGORIES) for _ in keys]))}


def write_csv(path, table):
    names = list(table)
    rows = len(next(iter(table.values()))[1])
    with open(path, "w", newline="", encoding="utf-8") as f:
        out = csv.writer(f)
        out.writerow(names)
        for i in range(rows):
            out.writerow([cell_text(table[n][1][i]) for n in names])


def cell_text(value):
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return repr(value)
    return str(value)


def random_program(rng, table):
    """A type-correct program over table t: the statement mix of the examples, sometimes a
    JOIN with the lookup table u and an AGGREGATE. Returns the source text. The generator
    tracks column types so the program only fails where the data breaks a contract (or a
    lookup key repeats, or a statistic has no values)."""
    types = {name: kind for name, (kind, _) in table.items()}
    joins = rng.random() < 0.4
    lines = ['LOAD "data.csv" AS t;'] + (['LOAD "lookup.csv" AS u;'] if joins else [])
    count = rng.randint(2, 9)
    join_at = rng.randrange(count) if joins else -1
    names = iter(f"d{i}" for i in range(1, 100))
    for step in range(count):
        if step == join_at and types.get("k") == "INTEGER" and "w" not in types and "label" not in types:
            lines.append("JOIN t WITH u ON k MANY;" if rng.random() < 0.4 else "JOIN t WITH u ON k;")
            types.update(w="INTEGER", label="STRING")
        line = statement(rng, types, names)
        if line:
            lines.append(line)
    if rng.random() < 0.35 and any(k != "FLOAT" for k in types.values()):
        groups = [c for c, k in types.items() if k != "FLOAT"]
        by = rng.sample(groups, min(len(groups), rng.randint(1, 2)))
        numeric = [c for c, k in types.items() if k in ("INTEGER", "FLOAT") and c not in by]
        others = [c for c in types if c not in by]
        items, new = [], {c: types[c] for c in by}
        for _ in range(rng.randint(1, 3)):
            function = rng.choice(["COUNT", "SUM", "MEAN", "MEDIAN", "MIN", "MAX", "STD"]) if numeric else "COUNT"
            if others and rng.random() < 0.15:
                function = "COUNT_DISTINCT"
            name = next(names)
            if function == "COUNT":
                items.append(f"COUNT AS {name}")
                new[name] = "INTEGER"
            elif function == "COUNT_DISTINCT":
                items.append(f"COUNT_DISTINCT {rng.choice(others)} AS {name}")
                new[name] = "INTEGER"
            else:
                column = rng.choice(numeric)
                items.append(f"{function} {column} AS {name}")
                new[name] = "FLOAT" if function in ("MEAN", "MEDIAN", "STD") else types[column]
        lines.append(f"AGGREGATE t BY [{', '.join(by)}] {', '.join(items)};")
        types.clear()
        types.update(new)
        for _ in range(rng.randint(0, 2)):
            line = statement(rng, types, names)
            if line:
                lines.append(line)
    lines.append('SAVE t AS "out.csv";')
    return "\n".join(lines) + "\n"


def statement(rng, types, names):
    """One random statement over t for the current column types (updated), or None."""
    numeric = [c for c, k in types.items() if k in ("INTEGER", "FLOAT")]
    op = rng.choice(["filter", "filter", "fill", "fillstat", "normalize", "dropnull", "derive",
                     "expect_range", "expect_notnull", "drop", "select"])
    if op == "filter":
        return f"FILTER t WHERE {condition(rng, types, 2)};"
    if op == "fill":
        column = rng.choice(list(types))
        value = {"INTEGER": str(rng.randint(-5, 5)), "FLOAT": f"{rng.randint(-5, 5)}.5",
                 "STRING": '"none"', "BOOLEAN": rng.choice(["TRUE", "FALSE"])}[types[column]]
        return f"FILL_NULL t.{column} WITH {value};"
    if op == "fillstat" and numeric:
        column = rng.choice(numeric)
        stat = rng.choice(["MEAN", "MEDIAN", "MODE"])
        if stat != "MODE":
            types[column] = "FLOAT"
        return f"FILL_NULL t.{column} WITH {stat};"
    if op == "normalize" and numeric:
        column = rng.choice([c for c in numeric if c != "id"] or numeric)
        types[column] = "FLOAT"
        return f"NORMALIZE t.{column} USING {rng.choice(['MINMAX', 'ZSCORE'])};"
    if op == "dropnull":
        if rng.random() < 0.5:
            return "DROP_NULL t;"
        return f"DROP_NULL t [{', '.join(rng.sample(list(types), min(len(types), rng.randint(1, 2))))}];"
    if op == "derive" and numeric:
        name = next(names)
        left, right = rng.choice(numeric), rng.choice(numeric)
        sign = rng.choice(["+", "-", "*"])
        constant = rng.choice(["", f" {rng.choice(['+', '*'])} {rng.randint(1, 4)}"])
        types[name] = "INTEGER" if types[left] == types[right] == "INTEGER" else "FLOAT"
        return f"DERIVE t.{name} = {left} {sign} {right}{constant};"
    if op == "expect_range" and numeric:
        low = rng.randint(-120, 0)
        return f"EXPECT t.{rng.choice(numeric)} BETWEEN {low} AND {low + rng.randint(50, 300)};"
    if op == "expect_notnull":
        return f"EXPECT t.{rng.choice(list(types))} NOT_NULL;"
    if op == "drop" and len(types) > 3:
        column = rng.choice([c for c in types if c != "id"])
        del types[column]
        return f"DROP t [{column}];"
    if op == "select" and len(types) > 3:
        keep = rng.sample(list(types), rng.randint(2, len(types) - 1))
        for column in [c for c in types if c not in keep]:
            del types[column]
        return f"SELECT t [{', '.join(keep)}];"
    return None


def condition(rng, types, depth):
    if depth > 0 and rng.random() < 0.35:
        joiner = rng.choice(["AND", "OR"])
        return f"({condition(rng, types, depth - 1)} {joiner} {condition(rng, types, depth - 1)})"
    if depth > 0 and rng.random() < 0.1:
        return f"NOT {condition(rng, types, depth - 1)}"
    column = rng.choice(list(types))
    kind = types[column]
    if kind == "STRING":
        return f'{column} {rng.choice(["==", "!="])} "{rng.choice(CATEGORIES)}"'
    if kind == "BOOLEAN":
        return f'{column} {rng.choice(["==", "!="])} {rng.choice(["TRUE", "FALSE"])}'
    op = rng.choice(["<", "<=", ">", ">=", "==", "!="])
    value = rng.randint(-60, 120) if kind == "INTEGER" else round(rng.uniform(-1.5, 100), 1)
    return f"{column} {op} {value}"


# ---------------------------------------------------------------- the reference interpreter

class Failure(Exception):
    """A contract that does not hold, or a statistic without values, at a program line."""
    def __init__(self, line, why):
        super().__init__(f"line {line}: {why}")
        self.line = line


TOKEN = re.compile(r'\s*(?:(\d+\.\d+|\d+)|("[^"]*")|([A-Za-z_][A-Za-z0-9_]*)|(<=|>=|==|!=|[<>()\[\],;.=+\-*/]))')


def tokens(text):
    out, pos = [], 0
    while pos < len(text.rstrip()):
        m = TOKEN.match(text, pos)
        number, string, name, symbol = m.groups()
        out.append(float(number) if number and "." in number else int(number) if number else
                   string[1:-1] if string else (name,) if name else symbol)
        pos = m.end()
    return out


def reference(source, table, lookup, failed=None):
    """Runs the program on the table t (and the lookup table u): (columns, kinds) of t with
    typed Python values. A contract that does not hold raises Failure; with a list `failed`,
    its line is added to the list instead and the program goes on (as if that check were
    not there)."""
    def fail(line, why):
        if failed is None:
            raise Failure(line, why)
        failed.append(line)
    columns = {name: list(values) for name, (_, values) in table.items()}
    kinds = {name: kind for name, (kind, _) in table.items()}
    for number, text in enumerate(source.splitlines(), 1):
        if not text.strip():
            continue
        t = tokens(text)
        word = t[0][0]
        rows = len(next(iter(columns.values())))
        if word == "FILTER":
            mask = Expr(t[3:-1]).boolean(columns)
            keep = [i for i in range(rows) if mask[i] is True]
            columns = {n: [v[i] for i in keep] for n, v in columns.items()}
        elif word == "FILL_NULL":
            column, value = t[3][0], t[5]
            present = [v for v in columns[column] if v is not None]
            if value in (("MEAN",), ("MEDIAN",), ("MODE",)):
                if not present and None in columns[column]:  # missing values, and nothing to fill them with
                    fail(number, f"{column} has no values")
                fill = None  # an empty table: nothing to compute, nothing to fill
                if value == ("MODE",) and present:
                    counts = {}
                    for v in present:
                        counts[v] = counts.get(v, 0) + 1
                    top = max(counts.values())
                    fill = min(v for v, c in counts.items() if c == top)
                elif value != ("MODE",):
                    if present:
                        fill = float(statistics.fmean(present) if value == ("MEAN",) else statistics.median(present))
                    columns[column] = [None if v is None else float(v) for v in columns[column]]
                    kinds[column] = "FLOAT"
            else:
                fill = {"TRUE": True, "FALSE": False}.get(value[0], value) if isinstance(value, tuple) else value
                if t[5] == "-":
                    fill = -t[6]
                if kinds[column] == "FLOAT" and fill is not None:
                    fill = float(fill)
            columns[column] = [fill if v is None else v for v in columns[column]]
        elif word == "NORMALIZE":
            column, how = t[3][0], t[5][0]
            values = columns[column]
            present = [float(v) for v in values if v is not None]
            if how == "ZSCORE":
                centre = statistics.fmean(present) if present else None
                spread = statistics.stdev(present) if len(present) > 1 else None
            else:
                centre = min(present) if present else None
                spread = max(present) - min(present) if present else None
            if spread is not None and spread > 0:
                columns[column] = [None if v is None else (v - centre) / spread for v in values]
            else:
                columns[column] = [None if v is None else 0.0 for v in values]
            kinds[column] = "FLOAT"
        elif word == "DROP_NULL":
            names = [x[0] for x in t[3:-2] if isinstance(x, tuple)] if t[2] == "[" else list(columns)
            keep = [i for i in range(rows) if all(columns[n][i] is not None for n in names)]
            columns = {n: [v[i] for i in keep] for n, v in columns.items()}
        elif word == "DERIVE":
            name = t[3][0]
            columns[name] = Expr(t[5:-1]).values(columns)
            kinds[name] = "FLOAT" if any(isinstance(v, float) for v in columns[name]) or any(
                kinds.get(x[0]) == "FLOAT" for x in t[5:-1] if isinstance(x, tuple)) else "INTEGER"
            if kinds[name] == "FLOAT":
                columns[name] = [None if v is None else float(v) for v in columns[name]]
        elif word == "EXPECT":
            column = t[3][0]
            if t[4] == ("NOT_NULL",):
                if any(v is None for v in columns[column]):
                    fail(number, f"{column} has missing values")
            else:
                low = -t[6] if t[5] == "-" else t[5]
                rest = t[7:] if t[5] == "-" else t[6:]
                high = -rest[2] if rest[1] == "-" else rest[1]
                if any(v is not None and not (low <= v <= high) for v in columns[column]):
                    fail(number, f"{column} outside {low} to {high}")
        elif word == "DROP":
            for x in t[3:-2]:
                if isinstance(x, tuple):
                    columns.pop(x[0])
        elif word == "SELECT":
            columns = {x[0]: columns[x[0]] for x in t[3:-2] if isinstance(x, tuple)}
        elif word == "JOIN" and len(t) > 7 and t[6] == ("MANY",):
            # left join with every row of u that has the key, in u's order; a row without one stays,
            # with missing values; a missing key matches nothing
            key = t[5][0]
            matches = [[j for j, value in enumerate(lookup[key][1]) if value is not None and value == v]
                       for v in columns[key]]
            order = [(i, j) for i, found in enumerate(matches) for j in (found or [None])]
            for name in list(columns):
                columns[name] = [columns[name][i] for i, _ in order]
            for name, (kind, values) in lookup.items():
                if name != key:
                    columns[name] = [None if j is None else values[j] for _, j in order]
                    kinds[name] = kind
        elif word == "JOIN":  # left join; each key once in u; a missing key matches nothing
            key = t[5][0]
            found = {}
            for i, value in enumerate(lookup[key][1]):
                if value is None:
                    continue
                if value in found:
                    fail(number, f"{key} {value} repeats")
                    continue
                found[value] = i
            for name, (kind, values) in lookup.items():
                if name != key:
                    columns[name] = [None if v not in found else values[found[v]] for v in columns[key]]
                    kinds[name] = kind
        elif word == "AGGREGATE":  # one row per group of non-missing BY values, sorted by them
            close = t.index("]")
            by = [x[0] for x in t[4:close] if isinstance(x, tuple)]
            groups = {}
            for i in range(rows):
                group = tuple(columns[c][i] for c in by)
                if None not in group:
                    groups.setdefault(group, []).append(i)
            ordered = sorted(groups)
            result = {c: [g[j] for g in ordered] for j, c in enumerate(by)}
            new_kinds = {c: kinds[c] for c in by}
            items, current = [], []
            for x in t[close + 1:-1]:
                if x == ",":
                    items.append(current)
                    current = []
                else:
                    current.append(x)
            items.append(current)
            for item in items:
                function, name = item[0][0], item[-1][0]
                if function == "COUNT":
                    result[name] = [len(groups[g]) for g in ordered]
                    new_kinds[name] = "INTEGER"
                    continue
                column = item[1][0]
                values = [[columns[column][i] for i in groups[g] if columns[column][i] is not None] for g in ordered]
                if function == "COUNT_DISTINCT":  # missing values are not counted
                    result[name] = [len(set(v)) for v in values]
                    new_kinds[name] = "INTEGER"
                    continue
                if function == "SUM":
                    zero = 0.0 if kinds[column] == "FLOAT" else 0
                    result[name] = [sum(v, zero) for v in values]
                elif function == "MEAN":
                    result[name] = [statistics.fmean(v) if v else None for v in values]
                elif function == "MEDIAN":
                    result[name] = [float(statistics.median(v)) if v else None for v in values]
                elif function == "STD":  # the sample standard deviation, as pandas computes it
                    result[name] = [statistics.stdev(v) if len(v) >= 2 else None for v in values]
                else:
                    result[name] = [(min(v) if function == "MIN" else max(v)) if v else None for v in values]
                new_kinds[name] = "FLOAT" if function in ("MEAN", "MEDIAN", "STD") else kinds[column]
            columns, kinds = result, new_kinds
    return columns, kinds


class Expr:
    """FILTER conditions (three-valued: None is unknown) and DERIVE values over columns."""
    def __init__(self, items):
        self.items, self.pos = items, 0

    def peek(self):
        return self.items[self.pos] if self.pos < len(self.items) else None

    def take(self):
        self.pos += 1
        return self.items[self.pos - 1]

    def boolean(self, columns):
        self.columns, self.rows = columns, len(next(iter(columns.values())))
        return self.disjunction()

    def disjunction(self):
        left = self.conjunction()
        while self.peek() == ("OR",):
            self.take()
            right = self.conjunction()
            left = [True if a is True or b is True else None if a is None or b is None else False
                    for a, b in zip(left, right)]
        return left

    def conjunction(self):
        left = self.negation()
        while self.peek() == ("AND",):
            self.take()
            right = self.negation()
            left = [False if a is False or b is False else None if a is None or b is None else True
                    for a, b in zip(left, right)]
        return left

    def negation(self):
        if self.peek() == ("NOT",):
            self.take()
            return [None if v is None else not v for v in self.negation()]
        if self.peek() == "(":
            self.take()
            inner = self.disjunction()
            self.take()
            return inner
        column, op = self.take()[0], self.take()
        value = self.take()
        if value == "-":
            value = -self.take()
        if isinstance(value, tuple):
            value = {"TRUE": True, "FALSE": False}[value[0]]
        compare = {"<": lambda a, b: a < b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b,
                   ">=": lambda a, b: a >= b, "==": lambda a, b: a == b, "!=": lambda a, b: a != b}[op]
        return [None if v is None else compare(v, value) for v in self.columns[column]]

    def values(self, columns):
        self.columns, self.rows = columns, len(next(iter(columns.values())))
        return self.sum()

    def sum(self):
        left = self.product()
        while self.peek() in ("+", "-"):
            op = self.take()
            right = self.product()
            left = [None if a is None or b is None else a + b if op == "+" else a - b for a, b in zip(left, right)]
        return left

    def product(self):
        left = self.operand()
        while self.peek() in ("*", "/"):
            op = self.take()
            right = self.operand()
            left = [None if a is None or b is None else a * b if op == "*" else a / b for a, b in zip(left, right)]
        return left

    def operand(self):
        item = self.take()
        if item == "(":
            inner = self.sum()
            self.take()
            return inner
        if isinstance(item, tuple):
            return list(self.columns[item[0]])
        return [item] * self.rows


# ---------------------------------------------------------------- running and comparing

def compile_program(folder, extra):
    done = subprocess.run([str(COMPILER), "program.prep", "--python", *extra], cwd=folder, capture_output=True,
                          text=True)
    text = done.stdout + done.stderr
    if done.returncode == 0:
        return {"status": "compiled", "text": text}
    lines = re.findall(r"error\[(\w+)\]: .*\n\s*--> program\.prep:(\d+)", text)
    return {"status": "compile error", "kinds": sorted({k for k, _ in lines}),
            "line": int(lines[0][1]) if lines else None, "text": text[-600:]}


def run_generated(folder, script, inputs=()):
    out = folder / "output"
    target = out / "out.csv"
    if target.exists():
        target.unlink()
    text, argv = io.StringIO(), sys.argv
    sys.argv = [str(out / script), *inputs]
    os.environ["PREPLANG_TRUST_INPUTS"] = "0"
    start = time.perf_counter()
    try:
        with contextlib.redirect_stdout(text), contextlib.redirect_stderr(text):
            runpy.run_path(str(out / script), run_name="__main__")
    except Exception as error:  # noqa: BLE001 - the program's failure is a result
        found = re.match(r"line (\d+):", str(error))
        status = "contract error" if type(error).__name__ == "ContractError" else "crash"
        if "is not the input the program was compiled with" in str(error):
            status = "refused"
        return {"status": status, "line": int(found.group(1)) if found else None,
                "reason": f"{type(error).__name__}: {str(error)[:300]}", "seconds": time.perf_counter() - start,
                "deopts": text.getvalue().count("Deoptimized ")}
    finally:
        sys.argv = argv
    seconds = time.perf_counter() - start
    with open(target, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    return {"status": "saved", "header": rows[0], "rows": rows[1:], "seconds": seconds,
            "deopts": text.getvalue().count("Deoptimized ")}


def same_cell(text, value, kind):
    if value is None:
        return text == ""
    if text == "":
        return False
    if kind == "BOOLEAN":
        return text == str(value)
    if kind == "STRING":
        return text == value
    number = float(text)
    return math.isclose(number, float(value), rel_tol=1e-9, abs_tol=1e-9)


def agrees(run, expected):
    """'' if the run matches the reference outcome, else what differs."""
    if expected["status"] == "fails":
        # The compiler may prove a later contract violated while it leaves an earlier one,
        # which it cannot decide, to run time: right if that later contract does fail.
        if run["status"] == "compile error" and "contract" in run.get("kinds", []):
            return "" if run["line"] in expected["failing"] else f"compile error at line {run['line']}, which holds; reference fails at lines {expected['failing']}"
        if run["status"] == "contract error":
            return "" if run["line"] == expected["line"] else f"contract error at line {run['line']}, reference at line {expected['line']}"
        return f"{run['status']}, but the reference fails at line {expected['line']}"
    if run["status"] != "saved":
        return f"{run['status']} ({run.get('reason') or run.get('kinds')}), but the reference saves {len(expected['rows'])} rows"
    if run["header"] != expected["header"]:
        return f"columns {run['header']} instead of {expected['header']}"
    if len(run["rows"]) != len(expected["rows"]):
        return f"{len(run['rows'])} rows instead of {len(expected['rows'])}"
    for r, (got, want) in enumerate(zip(run["rows"], expected["rows"]), 1):
        for name, text, value in zip(expected["header"], got, want):
            if not same_cell(text, value, expected["kinds"][name]):
                return f"row {r}, column {name}: {text!r} instead of {value!r}"
    return ""


def expectation(source, table, lookup):
    """The reference outcome: the rows saved, or the line of the first failing contract
    (and every line whose contract fails)."""
    try:
        columns, kinds = reference(source, table, lookup)
        names = list(columns)
        rows = list(zip(*columns.values())) if names else []
        return {"status": "saves", "header": names, "rows": rows, "kinds": kinds}
    except Failure as failure:
        failing = []
        reference(source, table, lookup, failing)
        return {"status": "fails", "line": failure.line, "failing": failing}


def check_one(index, seed):
    rng = random.Random(seed * 100003 + index)
    table, lookup = random_table(rng), random_lookup(rng)
    source = random_program(rng, table)
    batch, batch_lookup = random_table(rng), random_lookup(rng)  # the second batch, for reuse
    # Its own random stream, so that the programs and batches of every seed stay as they were.
    flipped = flip_kind(random.Random(seed * 7919 + index), batch)
    folder = WORK / f"p{index:04d}"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    write_csv(folder / "data.csv", table)
    write_csv(folder / "lookup.csv", lookup)
    write_csv(folder / "data_b.csv", batch)
    write_csv(folder / "lookup_b.csv", batch_lookup)
    (folder / "program.prep").write_text(source, encoding="utf-8")
    expected = expectation(source, table, lookup)
    result = {"index": index, "program": source, "expected": expected["status"],
              "expected_rows": len(expected.get("rows", [])), "variants": {},
              "join": "JOIN " in source, "join_many": " MANY;" in source, "aggregate": "AGGREGATE " in source}
    for variant, extra in (("O0", ["--O0"]), ("optimized", [])):
        compiled = compile_program(folder, extra)
        run = compiled if compiled["status"] != "compiled" else run_generated(folder, "generated.py")
        if compiled["status"] == "compiled":
            shutil.copyfile(folder / "output" / "generated.py", folder / "output" / f"generated_{variant}.py")
            run["warnings"] = compiled["text"].count("warning[")
        result["variants"][variant] = {"status": run["status"], "line": run.get("line"),
                                       "seconds": run.get("seconds"), "rows": len(run.get("rows", [])),
                                       "problem": agrees(run, expected),
                                       "detail": run.get("reason") or run.get("text", "")[-300:] if run["status"] != "saved" else ""}
        result["variants"][variant]["_run"] = run
    a, b = result["variants"]["O0"].pop("_run"), result["variants"]["optimized"].pop("_run")
    result["same_O0_optimized"] = (a["status"] == b["status"] and a.get("line") == b.get("line")
                                   and a.get("header") == b.get("header") and a.get("rows") == b.get("rows"))
    # Reuse: the optimized program (output/generated.py) on the second batch
    if result["variants"]["optimized"]["status"] != "compile error":
        run = run_generated(folder, "generated.py", [f"t={folder / 'data_b.csv'}", f"u={folder / 'lookup_b.csv'}"])
        truth = expectation(source, batch, batch_lookup)
        if flipped is not None:
            # With the other numeric type the program may not even compile (a decimal fill of a
            # column that is now INTEGER, say); the reference does not model type errors, so a
            # fresh compilation of the batch decides. Then only a refusal is right.
            fresh = folder / "fresh"
            fresh.mkdir()
            shutil.copyfile(folder / "data_b.csv", fresh / "data.csv")
            shutil.copyfile(folder / "lookup_b.csv", fresh / "lookup.csv")
            shutil.copyfile(folder / "program.prep", fresh / "program.prep")
            compiled = compile_program(fresh, [])
            if compiled["status"] == "compile error" and "type" in compiled["kinds"]:
                truth = {"status": "compile error"}
        if truth["status"] == "compile error":
            outcome, problem = ("refused", "") if run["status"] == "refused" else                 ("wrong", f"ran ({run['status']}) where a fresh compilation fails: {compiled['text'][-200:]}")
        elif run["status"] == "refused":
            outcome, problem = "refused", ""
        else:
            # A compile error is not possible here, so a failure must be at the first failing line.
            problem = agrees(run, dict(truth, failing=[truth.get("line")]))
            outcome = "wrong" if problem else "correct stop" if run["status"] == "contract error" else "correct run"
        result["reuse"] = {"outcome": outcome, "problem": problem, "deopts": run.get("deopts", 0),
                           "truth": truth["status"], "detail": run.get("reason", ""), "flipped": flipped}
    return result


def report(results, seconds):
    n = len(results)
    lines = ["# Correctness: compiled programs against a reference interpreter", "",
             f"{n} random table programs over random CSV files (eval/compilation/differential.py), "
             "each compiled with --O0 and with the optimizer, run, and compared with an interpreter of the "
             "language written without pandas or compiler code.", ""]
    agree = {v: sum(1 for r in results if not r["variants"][v]["problem"]) for v in ("O0", "optimized")}
    fails = sum(1 for r in results if r["expected"] == "fails")
    static = sum(1 for r in results if r["expected"] == "fails" and r["variants"]["optimized"]["status"] == "compile error")
    same = sum(1 for r in results if r["same_O0_optimized"])
    rows = sum(r["expected_rows"] for r in results)
    lines += ["| Measure | Value |", "|---|---:|",
              f"| programs | {n} |",
              f"| reference saves a table / a contract fails | {n - fails} / {fails} |",
              f"| rows saved by the reference, in all | {rows:,} |",
              f"| --O0 agrees with the reference | {agree['O0']} / {n} |",
              f"| optimized agrees with the reference | {agree['optimized']} / {n} |",
              f"| --O0 and optimized give byte-identical outputs or the same failure | {same} / {n} |",
              f"| failing contracts reported at compile time (before any data is processed) | {static} / {fails} |",
              f"| programs with a JOIN (of them MANY) / an AGGREGATE | {sum(r['join'] for r in results)} "
              f"({sum(r.get('join_many', False) for r in results)}) / {sum(r['aggregate'] for r in results)} |",
              f"| time for the whole run (s) | {seconds:.0f} |", ""]
    reused = [r["reuse"] for r in results if "reuse" in r]
    outcomes = {o: sum(1 for u in reused if u["outcome"] == o) for o in ("correct run", "correct stop", "refused", "wrong")}
    lines += ["## Reuse on a second batch", "",
              "The optimized program of every program that compiled, run on another random batch with the "
              "same columns (`t=data_b.csv u=lookup_b.csv`), against the reference on that batch.", "",
              "| Outcome | Programs |", "|---|---:|",
              f"| ran, same rows as the reference | {outcomes['correct run']} |",
              f"| stopped at the line where the reference fails | {outcomes['correct stop']} |",
              f"| refused (the batch breaks the admission certificate) | {outcomes['refused']} |",
              f"| wrong (other rows, another line, or a crash) | {outcomes['wrong']} |",
              f"| runs that re-enabled at least one removed check or operation | "
              f"{sum(1 for u in reused if u['deopts'])} |", ""]
    flips = [u for u in reused if u.get("flipped")]
    if flips:
        count = {o: sum(1 for u in flips if u["outcome"] == o) for o in ("correct run", "correct stop", "refused", "wrong")}
        lines += [f"In {len(flips)} of these batches one numeric column had the other numeric type (INTEGER "
                  f"values made x.5, or FLOAT values rounded): {count['correct run']} ran with the reference's rows, "
                  f"{count['correct stop']} stopped where it fails, {count['refused']} were refused "
                  f"({sum(1 for u in flips if u['truth'] == 'compile error')} of the batches do not compile at all), "
                  f"{count['wrong']} were wrong.", ""]
    wrong = [r for r in results if any(r["variants"][v]["problem"] for v in ("O0", "optimized")) or not r["same_O0_optimized"]
             or r.get("reuse", {}).get("outcome") == "wrong"]
    if wrong:
        lines += ["## Disagreements", ""]
        for r in wrong:
            lines += [f"### p{r['index']:04d}", "", "```text", r["program"].rstrip(), "```", ""]
            for v in ("O0", "optimized"):
                lines.append(f"- {v}: {r['variants'][v]['status']}; {r['variants'][v]['problem'] or 'agrees'}"
                             + (f" ({r['variants'][v]['detail'].strip()[:200]})" if r['variants'][v]['detail'] else ""))
            if r.get("reuse", {}).get("outcome") == "wrong":
                lines.append(f"- reuse on the second batch: {r['reuse']['problem']} ({r['reuse']['detail'][:200]})")
            lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--programs", type=int, default=600)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--workers", type=int, default=6)
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    with ProcessPoolExecutor(args.workers) as pool:
        results = list(pool.map(check_one, range(args.programs), [args.seed] * args.programs, chunksize=8))
    seconds = time.perf_counter() - start
    RESULTS.mkdir(exist_ok=True)
    # gzipped, with a fixed header time: the same results give the same bytes
    text = json.dumps({"seed": args.seed, "results": results}, indent=1).encode("utf-8")
    (RESULTS / "differential.json.gz").write_bytes(gzip.compress(text, mtime=0))
    text = report(results, seconds)
    (RESULTS / "differential.md").write_text(text + "\n", encoding="utf-8")
    print(text[:3000])


if __name__ == "__main__":
    main()
