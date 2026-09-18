"""Baseline for the evaluation: a naive, script-style runner for .prep programs.

It executes each statement directly with pandas and OpenCV, in order, the way
a hand-written preprocessing script would: default pandas type inference, all
images loaded into memory, and EXPECT contracts as run-time assertions. There
is no static analysis, so every mistake surfaces only while the data is being
processed (as an exception), or not at all (a silently wrong output).

    py -3 eval/baseline.py program.prep output_folder
"""
import re
import sys
from pathlib import Path

import cv2
import numpy as np
import pandas as pd

IMAGE_TYPES = {".png", ".jpg", ".jpeg"}


class Images:
    """An image dataset held in memory, as a typical script does."""

    def __init__(self, items):
        self.items = items  # list of [relative path, array or None]
        self.format = None
        self.quality = 0


def literal(text):
    text = text.strip()
    if text.startswith('"'):
        return text[1:-1]
    if text.upper() in ("TRUE", "FALSE"):
        return text.upper() == "TRUE"
    return float(text) if "." in text else int(text)


def statements(source):
    source = "\n".join(line.split("#", 1)[0] for line in source.splitlines())
    return [" ".join(s.split()) for s in source.split(";") if s.strip()]


class Runner:
    def __init__(self, program_path, output_folder):
        self.base = Path(program_path).parent
        self.out = Path(output_folder)
        self.data = {}
        self.links = {}

    # ---------- targets ----------

    def frames(self, path):
        """(container, key) pairs for 'dataset' or 'dataset.table' or 'dataset.*'."""
        parts = path.split(".")
        value = self.data[parts[0]]
        if isinstance(value, pd.DataFrame):
            return [(self.data, parts[0])]
        if len(parts) == 1 or parts[1] == "*":
            return [(value, name) for name in value]
        return [(value, parts[1])]

    def columns(self, selector):
        """(container, key, column) for a column selector, with naive wildcard handling."""
        match = re.match(r"(\S+)(?: WHERE TYPE (\w+))?(?: EXCEPT \[([^\]]*)\])?", selector)
        path, type_filter, excluded = match.groups()
        excluded = [c.strip() for c in excluded.split(",")] if excluded else []
        parts = path.split(".")
        is_table = isinstance(self.data[parts[0]], pd.DataFrame)
        table_path = parts[0] if is_table else ".".join(parts[:2])
        column = parts[1] if is_table and len(parts) > 1 else (parts[2] if len(parts) > 2 else "*")
        result = []
        for container, key in self.frames(table_path):
            frame = container[key]
            if column != "*":
                names = [column]
            elif type_filter == "NUMERIC":
                names = list(frame.select_dtypes(include="number").columns)
            else:
                names = list(frame.columns)
            result += [(container, key, name) for name in names if name not in excluded]
        return result

    # ---------- statements ----------

    def run(self, source):
        for statement in statements(source):
            keyword = statement.split()[0].upper()
            getattr(self, "do_" + keyword.lower())(statement)

    def do_load(self, s):
        path, name = re.match(r'LOAD "([^"]+)" AS (\w+)', s).groups()
        self.data[name] = pd.read_csv(self.base / path)

    def do_load_tables(self, s):
        path, name = re.match(r'LOAD_TABLES "([^"]+)" AS (\w+)', s).groups()
        self.data[name] = {p.stem: pd.read_csv(p) for p in sorted((self.base / path).glob("*.csv"))}

    def do_load_images(self, s):
        path, name, recursive = re.match(r'LOAD_IMAGES "([^"]+)" AS (\w+)( RECURSIVE)?', s).groups()
        folder = self.base / path
        found = folder.rglob("*") if recursive else folder.glob("*")
        files = sorted(p for p in found if p.is_file() and p.suffix.lower() in IMAGE_TYPES)
        self.data[name] = Images([[p.relative_to(folder).as_posix(), cv2.imread(str(p), cv2.IMREAD_UNCHANGED)]
                                  for p in files])

    def do_fill_null(self, s):
        selector, value, if_exists = re.match(r"FILL_NULL (.+?) WITH (.+?)( IF_EXISTS)?$", s).groups()
        for container, key, column in self.columns(selector):
            if if_exists and column not in container[key]:
                continue
            container[key][column] = container[key][column].fillna(literal(value))

    def do_drop_null(self, s):
        for container, key in self.frames(s.split()[1]):
            container[key] = container[key].dropna()

    def do_normalize(self, s):
        if " RANGE " in s:
            name, low, high = re.match(r"NORMALIZE (\w+) RANGE (\S+) (\S+)", s).groups()
            low, high = float(low), float(high)
            for item in self.data[name].items:
                item[1] = item[1].astype(np.float32) / 255 * (high - low) + low
            return
        match = re.match(r"NORMALIZE (.+?)(?: USING (ZSCORE|MINMAX))?( IF_EXISTS)?$", s)
        selector, method, if_exists = match.groups()
        for container, key, column in self.columns(selector):
            if if_exists and column not in container[key]:
                continue
            x = container[key][column]
            container[key][column] = (x - x.min()) / (x.max() - x.min()) if method == "MINMAX" else (x - x.mean()) / x.std()

    def do_filter(self, s):
        path, condition = re.match(r"FILTER (\S+) WHERE (.+)$", s).groups()
        for container, key in self.frames(path):
            frame = container[key]
            container[key] = frame[self.condition(condition, frame)]

    def condition(self, text, frame):
        """Evaluates AND/OR/NOT/comparisons with plain pandas operators."""
        tokens = re.findall(r'"[^"]*"|>=|<=|==|!=|[<>()]|[\w.\-]+', text)
        position = 0

        def peek():
            return tokens[position] if position < len(tokens) else None

        def take():
            nonlocal position
            position += 1
            return tokens[position - 1]

        def parse_or():
            left = parse_and()
            while peek() and peek().upper() == "OR":
                take()
                left = left | parse_and()
            return left

        def parse_and():
            left = parse_not()
            while peek() and peek().upper() == "AND":
                take()
                left = left & parse_not()
            return left

        def parse_not():
            if peek() and peek().upper() == "NOT":
                take()
                return ~parse_not()
            if peek() == "(":
                take()
                value = parse_or()
                take()
                return value
            column, op, value = take(), take(), literal(take())
            series = frame[column]
            return {">": series > value, ">=": series >= value, "<": series < value, "<=": series <= value,
                    "==": series == value, "!=": series != value}[op]

        return parse_or()

    def do_select(self, s):
        path, names = re.match(r"SELECT (\S+) \[([^\]]*)\]", s).groups()
        for container, key in self.frames(path):
            container[key] = container[key][[n.strip() for n in names.split(",")]]

    def do_drop(self, s):
        path, names, if_exists = re.match(r"DROP (\S+) \[([^\]]*)\]( IF_EXISTS)?", s).groups()
        for container, key in self.frames(path):
            names_here = [n.strip() for n in names.split(",")]
            container[key] = container[key].drop(columns=names_here, errors="ignore" if if_exists else "raise")

    def do_save(self, s):
        path, file = re.match(r'SAVE (\S+) AS "([^"]+)"', s).groups()
        for container, key in self.frames(path):
            target = self.out / file
            target.parent.mkdir(parents=True, exist_ok=True)
            container[key].to_csv(target, index=False)

    def do_save_tables(self, s):
        name, folder = re.match(r'SAVE_TABLES (\w+) TO "([^"]+)"', s).groups()
        for table, frame in self.data[name].items():
            target = self.out / folder / f"{table}.csv"
            target.parent.mkdir(parents=True, exist_ok=True)
            frame.to_csv(target, index=False)

    def do_expect(self, s):
        match = re.match(r"EXPECT (.+?) (NUMBER|STRING|BOOLEAN|NOT_NULL|UNIQUE|BETWEEN .+?|REFERENCES .+?|"
                         r"SCHEMA_SAME_AS .+?|CHANNELS .+?|MIN_WIDTH .+?|MIN_HEIGHT .+?|MATCH_RATE .+?)( IF_EXISTS)?$", s)
        target, rule, if_exists = match.groups()
        word = rule.split()[0]
        if word in ("CHANNELS", "MIN_WIDTH", "MIN_HEIGHT"):
            limit = int(rule.split()[1])
            for path, img in self.data[target].items:
                h, w = img.shape[:2]
                channels = 1 if img.ndim == 2 else img.shape[2]
                ok = {"CHANNELS": channels == limit, "MIN_WIDTH": w >= limit, "MIN_HEIGHT": h >= limit}[word]
                assert ok, f"{path} breaks {rule}"
            return
        if word == "MATCH_RATE":
            images, names = self.links[target]
            rate = 100 * len(images & names) / max(len(images), len(names), 1)
            assert rate >= float(rule.split()[1].rstrip("%")), f"match rate {rate:.2f}%"
            return
        if word == "SCHEMA_SAME_AS":
            other, excluded = re.match(r"SCHEMA_SAME_AS (\S+)(?: EXCEPT \[([^\]]*)\])?", rule).groups()
            excluded = {c.strip() for c in excluded.split(",")} if excluded else set()
            (a_container, a_key), (b_container, b_key) = self.frames(target)[0], self.frames(other)[0]
            assert set(a_container[a_key].columns) - excluded == set(b_container[b_key].columns) - excluded
            return
        for container, key, column in self.columns(target):
            if if_exists and column not in container[key]:
                continue
            series = container[key][column]
            if word == "NUMBER":
                assert pd.api.types.is_numeric_dtype(series), f"{column} is not numeric"
            elif word == "STRING":
                assert series.dtype == object, f"{column} is not text"
            elif word == "BOOLEAN":
                assert pd.api.types.is_bool_dtype(series), f"{column} is not boolean"
            elif word == "NOT_NULL":
                assert not series.isna().any(), f"{column} has missing values"
            elif word == "UNIQUE":
                assert not series.dropna().duplicated().any(), f"{column} has repeated values"
            elif word == "BETWEEN":
                low, high = re.match(r"BETWEEN (\S+) AND (\S+)", rule).groups()
                assert series.dropna().between(float(low), float(high)).all(), f"{column} out of range"
            elif word == "REFERENCES":
                parts = rule.split()[1].split(".")
                ref_container, ref_key = self.frames(".".join(parts[:-1]))[0]
                assert series.dropna().isin(ref_container[ref_key][parts[-1]]).all(), f"{column} has unknown keys"

    def do_resize(self, s):
        name, width, height, keep = re.match(r"RESIZE (\w+) TO (\d+) (\d+)( KEEP_ASPECT)?", s).groups()
        width, height = int(width), int(height)
        for item in self.data[name].items:
            img = item[1]
            if keep:
                h, w = img.shape[:2]
                scale = min(width / w, height / h)
                img = cv2.resize(img, (max(1, round(w * scale)), max(1, round(h * scale))))
                top, left = (height - img.shape[0]) // 2, (width - img.shape[1]) // 2
                img = cv2.copyMakeBorder(img, top, height - img.shape[0] - top, left, width - img.shape[1] - left,
                                         cv2.BORDER_CONSTANT, value=0)
            else:
                img = cv2.resize(img, (width, height), interpolation=cv2.INTER_AREA)
            item[1] = img

    def do_grayscale(self, s):
        for item in self.data[s.split()[1]].items:
            img = item[1]
            if img.ndim == 3:
                item[1] = cv2.cvtColor(img, cv2.COLOR_BGRA2GRAY if img.shape[2] == 4 else cv2.COLOR_BGR2GRAY)

    def do_compress(self, s):
        name, quality = re.match(r"COMPRESS (\w+) QUALITY (\d+)", s).groups()
        self.data[name].quality = int(quality)

    def do_convert(self, s):
        name, fmt = re.match(r"CONVERT (\w+) TO (\w+)", s).groups()
        self.data[name].format = fmt.upper()

    def do_remove_corrupt(self, s):
        images = self.data[s.split()[1]]
        images.items = [item for item in images.items if item[1] is not None]

    def do_save_images(self, s):
        name, folder = re.match(r'SAVE_IMAGES (\w+) TO "([^"]+)"', s).groups()
        images = self.data[name]
        for path, img in images.items:
            target = self.out / folder / path
            if images.format:
                target = target.with_suffix("." + images.format.lower())
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.suffix == ".npy":
                np.save(target, img)
            else:
                params = [cv2.IMWRITE_JPEG_QUALITY, images.quality] if images.quality else []
                cv2.imwrite(str(target), img, params)

    def do_link(self, s):
        images, table, column, name = re.match(r"LINK (\w+) WITH (\w+) ON (\w+) AS (\w+)", s).groups()
        files = {path for path, _ in self.data[images].items}
        self.links[name] = (files, set(self.data[table][column].dropna()))


if __name__ == "__main__":
    program, output = sys.argv[1], sys.argv[2]
    Runner(program, output).run(Path(program).read_text(encoding="utf-8"))
