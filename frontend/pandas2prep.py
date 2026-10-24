"""A second front end: lifts the data-cleaning part of a pandas script into PrepLang.

    py -3 frontend/pandas2prep.py script.py      (prints the .prep program; notes go to stderr)
    py -3 frontend/pandas2prep.py script.py -o lifted.prep      (writes it to a file instead)

The script is parsed with Python's own parser (the ast module), not run. Every
DataFrame read with pd.read_csv("...") is followed through the statements of the
script, in order. A statement that changes a followed DataFrame in a way PrepLang
expresses is translated:

    df = pd.read_csv("train.csv")                       LOAD "train.csv" AS df;
    df["Age"] = df["Age"].fillna(df["Age"].median())    FILL_NULL df.Age WITH MEDIAN;
    df.fillna({"Embarked": "S"}, inplace=True)          FILL_NULL df.Embarked WITH "S";
    df = df.dropna(subset=["Fare"])                     DROP_NULL df [Fare];
    df.drop(["Cabin", "Ticket"], axis=1, inplace=True)  DROP df [Cabin, Ticket];
    df = df[["Pclass", "Age"]]                          SELECT df [Pclass, Age];
    df = df[(df.Age > 1) & (df["Fare"] < 500)]          FILTER df WHERE Age > 1 AND Fare < 500;
    df["Fare"] = (df["Fare"] - df["Fare"].mean()) / df["Fare"].std()
                                                        NORMALIZE df.Fare USING ZSCORE;
    assert df["Age"].between(0, 100).all()              EXPECT df.Age BETWEEN 0 AND 100;
    df.to_csv("clean.csv", index=False)                 SAVE df AS "clean.csv";

and, unless derived=False (the version that lifted the corpora of RQ5):

    df["when"] = pd.to_datetime(df["when"])             PARSE_DATE df.when;
    df["hour"] = df["when"].dt.hour                     DERIVE df.hour = HOUR(when);
    df["temp_c"] = df["temp"] - 273.15                  DERIVE df.temp_c = temp - 273.15;

That version also finds reads that are not written as one literal call:

    path = DATA + "day.csv"; pd.read_csv(path)          a path built from strings (+, f-strings,
                                                        os.path.join, pathlib's /)
    pd.read_csv(path, parse_dates=["dteday"])           LOAD, then PARSE_DATE df.dteday;
                                                        sep=",", header=0, encoding="utf-8",
                                                        low_memory and engine change nothing
    pd.read_csv(path, index_col="id")                   LOAD, then DROP df [id] (the index is not a column)
    def load(p): return pd.read_csv(p)                  a function that only reads a file
    def load(p): df = pd.read_csv(p); ...; return df    its body, with p bound, as if written here
    def main(): ...; main()                             the body of a function called once
    try: / with ...:                                    the statements inside

Statements that only read a DataFrame (print, describe, plots, models) do not
change it and are skipped. Lifting a DataFrame stops at the first statement that
changes it in a way PrepLang cannot express (a new column, map, apply, ...): the
lifted program is the longest prefix of the script's cleaning that PrepLang
expresses exactly, and every followed DataFrame is saved where its prefix ends.
"""
import argparse
import ast
import sys
from dataclasses import dataclass, field

COMPARISONS = {ast.Gt: ">", ast.GtE: ">=", ast.Lt: "<", ast.LtE: "<=", ast.Eq: "==", ast.NotEq: "!="}
ARITHMETIC = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/"}
DATE_PARTS = {"year": "YEAR", "month": "MONTH", "day": "DAY", "hour": "HOUR", "weekday": "WEEKDAY",
              "dayofweek": "WEEKDAY", "day_of_week": "WEEKDAY"}


class Unsupported(Exception):
    """The statement changes a followed DataFrame in a way PrepLang cannot express."""


@dataclass
class Frame:
    name: str            # PrepLang dataset name
    path: str            # the CSV file it was read from
    open: bool = True    # still being lifted
    stopped_at: str = ""  # why lifting stopped (empty while open)
    saved: bool = False  # its final SAVE <name>_lifted.csv, where lifting ended, is written


@dataclass
class Lifted:
    prep: list = field(default_factory=list)           # PrepLang statements
    source_lines: list = field(default_factory=list)   # the lifted Python statements, in order
    frames: dict = field(default_factory=dict)         # Python variable -> Frame
    changes: int = 0          # statements that change a followed DataFrame
    lifted: int = 0           # of these, the ones translated

    def program(self):
        return "\n".join(self.prep) + "\n"


def literal(node):
    """A PrepLang literal for a Python constant, or Unsupported."""
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub) and isinstance(node.operand, ast.Constant):
        node = ast.Constant(-node.operand.value)
    if isinstance(node, ast.Constant):
        value = node.value
        if isinstance(value, bool):
            return "TRUE" if value else "FALSE"
        if isinstance(value, (int, float)):
            return repr(value)
        if isinstance(value, str) and '"' not in value:
            return '"' + value + '"'
    raise Unsupported("a value that is not a number, text or boolean constant")


def strings(node):
    """['a', 'b'] or 'a' -> ["a", "b"]"""
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return [node.value]
    if isinstance(node, (ast.List, ast.Tuple)) and all(isinstance(e, ast.Constant) and isinstance(e.value, str)
                                                         for e in node.elts):
        return [e.value for e in node.elts]
    raise Unsupported("a column list that is not written out")


def name_text(column):
    return column if column.isidentifier() else "`" + column + "`"


# read_csv options that do not change the table read from a comma-separated UTF-8 file
NEUTRAL_OPTIONS = {"sep": {","}, "delimiter": {","}, "header": {0, "infer"},
                   "encoding": {"utf-8", "utf8", "UTF-8", "UTF8"}, "low_memory": None, "engine": None}


class Lifter:
    def __init__(self, derived=True):
        self.derived = derived  # translate PARSE_DATE and DERIVE, and find more reads (see the module text)
        self.out = Lifted()
        self.texts = {}  # module-level names bound to a string, e.g. path = "data/train.csv"
        self.functions = {}  # module-level functions, by name
        self.inlined = set()  # functions whose body was lifted in place of their call

    # ---------- recognising DataFrame expressions ----------

    def frame_of(self, node):
        """The followed variable a Name node is, or None."""
        if isinstance(node, ast.Name) and node.id in self.out.frames:
            return node.id
        return None

    def column_of(self, node, frame):
        """'Age' for df["Age"], df.Age or df.loc[:, "Age"] on the given frame, else None."""
        if isinstance(node, ast.Subscript) and self.frame_of(node.value) == frame:
            key = node.slice
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                return key.value
        if isinstance(node, ast.Attribute) and self.frame_of(node.value) == frame and node.attr not in dir_methods:
            return node.attr
        return None

    def mentions(self, node):
        return {n.id for n in ast.walk(node) if isinstance(n, ast.Name) and n.id in self.out.frames}

    # ---------- statements ----------

    def lift(self, tree):
        for statement in tree.body:
            self.statement(statement)
        for variable, frame in self.out.frames.items():
            if not frame.saved:
                self.out.prep.append(f'SAVE {frame.name} AS "{frame.name}_lifted.csv";')
        return self.out

    def statement(self, node):
        if isinstance(node, ast.FunctionDef):
            self.functions[node.name] = node
            return
        if isinstance(node, (ast.Import, ast.ImportFrom, ast.ClassDef)):
            return
        if self.derived and isinstance(node, (ast.Try, ast.With)):
            for inner in node.body:  # the statements run in order when nothing fails
                self.statement(inner)
            return
        if self.derived and isinstance(node, ast.Expr) and isinstance(node.value, ast.Call) and \
                isinstance(node.value.func, ast.Name) and not node.value.args and not node.value.keywords:
            function = self.functions.get(node.value.func.id)
            if function and not function.args.args and function.name not in self.inlined:
                self.inlined.add(function.name)  # main(): its body, once, as if written here
                for inner in function.body:
                    if not isinstance(inner, ast.Return):
                        self.statement(inner)
                return
        if isinstance(node, ast.If) and isinstance(node.test, ast.Compare) and \
                isinstance(node.test.left, ast.Name) and node.test.left.id == "__name__":
            for inner in node.body:
                self.statement(inner)
            return
        if self.load(node) or self.inline_call(node):
            return
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            value = node.value
            text = self.static_text(value, self.texts) if self.derived else \
                value.value if isinstance(value, ast.Constant) and isinstance(value.value, str) else None
            if text is not None:
                self.texts[node.targets[0].id] = text
            else:
                self.texts.pop(node.targets[0].id, None)
        touched = self.changed_frames(node)
        if not touched:
            return  # reads a DataFrame at most
        open_frames = [v for v in touched if self.out.frames[v].open]
        if not open_frames:
            return
        self.out.changes += 1
        try:
            prep = self.translate(node, open_frames)
        except Unsupported as reason:
            for variable in open_frames:
                frame = self.out.frames[variable]
                frame.open = False
                frame.stopped_at = f"line {node.lineno}: {reason}"
                if not frame.saved:
                    self.out.prep.append(f'SAVE {frame.name} AS "{frame.name}_lifted.csv";')
                    frame.saved = True
            return
        self.out.lifted += 1
        self.out.prep.extend(prep)
        self.out.source_lines.append(ast.unparse(node))

    def static_text(self, node, names):
        """The string an expression always has, from string constants and `names` (+, f-strings,
        os.path.join, str(), pathlib's Path and /), or None."""
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.Name):
            return names.get(node.id)
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Div)):
            left, right = self.static_text(node.left, names), self.static_text(node.right, names)
            if left is None or right is None:
                return None
            return left + right if isinstance(node.op, ast.Add) else left.rstrip("/\\") + "/" + right
        if isinstance(node, ast.JoinedStr):
            parts = []
            for value in node.values:
                if isinstance(value, ast.FormattedValue):
                    part = self.static_text(value.value, names) \
                        if value.conversion == -1 and value.format_spec is None else None
                else:
                    part = self.static_text(value, names)
                if part is None:
                    return None
                parts.append(part)
            return "".join(parts)
        if isinstance(node, ast.Call) and not node.keywords and node.args:
            name = ast.unparse(node.func)
            parts = [self.static_text(arg, names) for arg in node.args]
            if None in parts:
                return None
            if name in ("os.path.join", "path.join"):
                return "/".join(part.rstrip("/\\") for part in parts[:-1]) + ("/" if len(parts) > 1 else "") + parts[-1]
            if name in ("str", "Path", "pathlib.Path") and len(parts) == 1:
                return parts[0]
        return None

    def read_call(self, call, names):
        """(path, columns to parse as dates, the call as it can be run) for pd.read_csv(...) of a known
        path with no option that changes the table but parse_dates; None otherwise."""
        if not (isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute) and call.func.attr == "read_csv"
                and len(call.args) == 1):
            return None
        argument = call.args[0]
        if self.derived:
            path = self.static_text(argument, names)
        elif isinstance(argument, ast.Constant) and isinstance(argument.value, str):
            path = argument.value
        else:
            path = names.get(argument.id) if isinstance(argument, ast.Name) else None
        if path is None:
            return None
        dates, options, index = [], [], []
        for keyword in call.keywords:
            if not self.derived:
                return None  # read options PrepLang does not have (sep, usecols, ...): not followed
            if keyword.arg == "parse_dates":
                try:
                    dates = strings(keyword.value)
                except Unsupported:
                    return None
            elif keyword.arg == "index_col" and isinstance(keyword.value, ast.Constant) and \
                    isinstance(keyword.value.value, str):
                index = [keyword.value.value]
            elif not (keyword.arg in NEUTRAL_OPTIONS and isinstance(keyword.value, ast.Constant) and
                      (NEUTRAL_OPTIONS[keyword.arg] is None or keyword.value.value in NEUTRAL_OPTIONS[keyword.arg])):
                return None
            options.append(f", {keyword.arg}={ast.unparse(keyword.value)}")
        if set(index) & set(dates):
            return None
        return path, dates, index, f"pd.read_csv({path!r}{''.join(options)})"

    def function_read(self, call):
        """For df = load("x.csv"), where load only returns pd.read_csv(...): that read."""
        if not (self.derived and isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
                and call.func.id in self.functions and not call.keywords):
            return None
        function = self.functions[call.func.id]
        if len(function.body) != 1 or not isinstance(function.body[0], ast.Return) or \
                len(call.args) != len(function.args.args):
            return None
        names = dict(self.texts)
        for parameter, argument in zip(function.args.args, call.args):
            text = self.static_text(argument, self.texts)
            if text is None:
                return None
            names[parameter.arg] = text
        return self.read_call(function.body[0].value, names)

    def inline_call(self, node):
        """x = f(args), where f reads a file, may change the DataFrame, and returns it last: f's body
        is lifted as if written here, with its parameters bound to the arguments, and x is the
        DataFrame it returns. Only for module-level functions whose arguments are known strings."""
        if not (self.derived and isinstance(node, ast.Assign) and len(node.targets) == 1 and
                isinstance(node.targets[0], ast.Name) and isinstance(node.value, ast.Call) and
                isinstance(node.value.func, ast.Name) and not node.value.keywords):
            return False
        function = self.functions.get(node.value.func.id)
        if function is None or function.name in self.inlined or len(node.value.args) != len(function.args.args) or \
                not function.body or not isinstance(function.body[-1], ast.Return) or \
                not isinstance(function.body[-1].value, ast.Name) or \
                any(isinstance(n, (ast.Return, ast.Yield, ast.Global, ast.Nonlocal))
                    for statement in function.body[:-1] for n in ast.walk(statement)):
            return False
        bound = {}
        for parameter, argument in zip(function.args.args, node.value.args):
            text = self.static_text(argument, self.texts)
            if text is None:
                return False
            bound[parameter.arg] = text
        reads = [n for statement in function.body for n in ast.walk(statement)
                 if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and n.func.attr == "read_csv"]
        if len(reads) != 1:
            return False
        self.inlined.add(function.name)
        saved = dict(self.texts)
        self.texts.update(bound)
        for parameter, text in bound.items():
            self.out.source_lines.append(f"{parameter} = {text!r}")
        for inner in function.body[:-1]:
            self.statement(inner)
        self.texts = saved
        returned = function.body[-1].value.id
        target = node.targets[0].id
        if returned not in self.out.frames or target in self.out.frames:
            return returned in self.out.frames
        self.out.frames[target] = self.out.frames.pop(returned)
        self.out.source_lines.append(f"{target} = {returned}")
        return True

    def load(self, node):
        """df = pd.read_csv("file.csv") starts following df."""
        if not (isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)):
            return False
        read = self.read_call(node.value, self.texts) or self.function_read(node.value)
        if read is None:
            return False
        path, dates, index, call = read
        variable = node.targets[0].id
        if variable in self.out.frames:
            return False
        frame = Frame(name=variable if variable.isidentifier() else "data", path=path)
        self.out.frames[variable] = frame
        self.out.prep.append(f'LOAD "{frame.path}" AS {frame.name};')
        self.out.prep.extend(f"PARSE_DATE {frame.name}.{name_text(column)};" for column in dates)
        if index:
            self.out.prep.append(f"DROP {frame.name} [{name_text(index[0])}];")
        # recorded with the path itself, so the lifted statements run on their own
        self.out.source_lines.append(f"{variable} = {call}")
        return True

    def changed_frames(self, node):
        """The followed variables this statement may change."""
        changed = set()
        targets = []
        if isinstance(node, ast.Assign):
            targets = node.targets
        elif isinstance(node, (ast.AugAssign, ast.AnnAssign)):
            targets = [node.target]
        elif isinstance(node, ast.Delete):
            targets = node.targets
        for target in targets:
            for element in (target.elts if isinstance(target, ast.Tuple) else [target]):
                base = element
                while isinstance(base, (ast.Subscript, ast.Attribute)):
                    base = base.value
                if isinstance(base, ast.Name) and base.id in self.out.frames:
                    changed.add(base.id)
        for call in [n for n in ast.walk(node) if isinstance(n, ast.Call)]:
            inplace = any(k.arg == "inplace" and isinstance(k.value, ast.Constant) and k.value.value is True
                          for k in call.keywords)
            if inplace and isinstance(call.func, ast.Attribute):
                changed |= self.mentions(call.func.value)
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call) and \
                isinstance(node.value.func, ast.Attribute) and node.value.func.attr == "to_csv":
            changed |= self.mentions(node.value.func.value)  # a save is translated too
        if isinstance(node, ast.Assert):
            changed |= self.mentions(node.test)
        return sorted(changed)

    # ---------- translation ----------

    def translate(self, node, open_frames):
        if len(open_frames) != 1:
            raise Unsupported("a statement that changes two DataFrames")
        variable = open_frames[0]
        name = self.out.frames[variable].name
        if isinstance(node, ast.Assert):
            return [self.expect(node.test, variable, name)]
        if isinstance(node, ast.Delete):
            columns = [self.column_of(t, variable) for t in node.targets]
            if None in columns:
                raise Unsupported("del of something that is not a column")
            return [f"DROP {name} [{', '.join(map(name_text, columns))}];"]
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
            return self.method_statement(node.value, variable, name, inplace=True)
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target, value = node.targets[0], node.value
            if self.frame_of(target) == variable:  # df = <expression on df>
                return self.frame_expression(value, variable, name)
            column = self.column_of(target, variable)
            if column is not None:  # df["c"] = <expression on df["c"]>
                return self.column_assignment(column, value, variable, name)
        raise Unsupported(f"{type(node).__name__} that PrepLang cannot express")

    def frame_expression(self, value, variable, name):
        # df[["a", "b"]], df[condition], df.loc[condition], df.loc[:, ["a", "b"]]
        if isinstance(value, ast.Subscript) and self.frame_of(value.value) == variable:
            if isinstance(value.slice, (ast.List, ast.Tuple)):
                return [f"SELECT {name} [{', '.join(map(name_text, strings(value.slice)))}];"]
            return [f"FILTER {name} WHERE {self.condition(value.slice, variable)};"]
        if isinstance(value, ast.Subscript) and isinstance(value.value, ast.Attribute) and \
                value.value.attr == "loc" and self.frame_of(value.value.value) == variable:
            key = value.slice
            if isinstance(key, ast.Tuple) and len(key.elts) == 2 and isinstance(key.elts[0], ast.Slice):
                return [f"SELECT {name} [{', '.join(map(name_text, strings(key.elts[1])))}];"]
            return [f"FILTER {name} WHERE {self.condition(key, variable)};"]
        if isinstance(value, ast.Call):
            return self.method_statement(value, variable, name, inplace=False)
        raise Unsupported("an assignment PrepLang cannot express")

    def method_statement(self, call, variable, name, inplace):
        """df.dropna(...), df.drop(...), df.fillna(...), df.to_csv(...), df["c"].fillna(..., inplace=True)"""
        if not isinstance(call.func, ast.Attribute):
            raise Unsupported("a call PrepLang cannot express")
        method = call.func.attr
        owner = call.func.value
        keywords = {k.arg: k.value for k in call.keywords}
        has_inplace = "inplace" in keywords
        keywords.pop("inplace", None)
        if has_inplace != inplace and method != "to_csv":
            raise Unsupported("a method result that is not assigned back")
        column = self.column_of(owner, variable)
        if column is not None and method == "fillna":
            if len(call.args) != 1 or keywords:
                raise Unsupported("fillna with options")
            return [f"FILL_NULL {name}.{name_text(column)} WITH {self.fill_value(call.args[0], column, variable)};"]
        if self.frame_of(owner) != variable:
            raise Unsupported(f"{method} on something that is not the DataFrame or one of its columns")
        if method == "dropna":
            subset = keywords.pop("subset", None)
            if call.args or keywords:
                raise Unsupported("dropna with options other than subset")
            columns = "" if subset is None else " [" + ", ".join(map(name_text, strings(subset))) + "]"
            return [f"DROP_NULL {name}{columns};"]
        if method == "drop":
            columns = keywords.pop("columns", None)
            axis = keywords.pop("axis", None)
            if columns is not None and axis is not None:
                raise Unsupported("drop with both columns= and axis=, which pandas rejects")
            if columns is None and call.args and axis is not None and \
                    isinstance(axis, ast.Constant) and axis.value in (1, "columns"):
                columns = call.args[0]
            elif columns is None or call.args:
                raise Unsupported("drop of rows, or with unclear axis")
            if keywords:
                raise Unsupported("drop with options")
            return [f"DROP {name} [{', '.join(map(name_text, strings(columns)))}];"]
        if method == "fillna":
            if len(call.args) != 1 or keywords or not isinstance(call.args[0], ast.Dict):
                raise Unsupported("fillna of every column with one value")
            out = []
            for key, value in zip(call.args[0].keys, call.args[0].values):
                column = strings(key)[0]
                out.append(f"FILL_NULL {name}.{name_text(column)} WITH {self.fill_value(value, column, variable)};")
            return out
        if method == "copy" and not call.args and not keywords:
            return []  # df = df.copy() changes nothing
        if method == "to_csv":
            index = keywords.pop("index", None)
            if not (call.args and isinstance(call.args[0], ast.Constant)) or keywords or \
                    not (isinstance(index, ast.Constant) and index.value is False):
                raise Unsupported("to_csv with the index or other options")
            return [f'SAVE {name} AS "{call.args[0].value}";']
        raise Unsupported(f".{method}() PrepLang cannot express")

    def fill_value(self, node, column, variable):
        """A constant, or df["c"].mean() / .median() / .mode()[0] of the same column."""
        statistic = node
        if isinstance(node, ast.Subscript) and isinstance(node.slice, ast.Constant) and node.slice.value == 0:
            statistic = node.value  # df["c"].mode()[0]
        if isinstance(statistic, ast.Call) and isinstance(statistic.func, ast.Attribute) and not statistic.args \
                and statistic.func.attr in ("mean", "median", "mode") \
                and self.column_of(statistic.func.value, variable) == column:
            if (statistic.func.attr == "mode") != (statistic is not node):
                raise Unsupported("mode() without [0]")
            return statistic.func.attr.upper()
        return literal(node)

    def column_assignment(self, column, value, variable, name):
        if isinstance(value, ast.Call) and isinstance(value.func, ast.Attribute) and value.func.attr == "fillna" \
                and self.column_of(value.func.value, variable) == column and len(value.args) == 1 \
                and not value.keywords:
            return [f"FILL_NULL {name}.{name_text(column)} WITH {self.fill_value(value.args[0], column, variable)};"]
        method = self.normalization(value, column, variable)
        if method:
            return [f"NORMALIZE {name}.{name_text(column)} USING {method};"]
        if self.derived:
            # errors="coerce" turns text that is not a date into a missing value; PrepLang refuses such
            # text instead, so the two agree on every input PrepLang runs.
            coerce = [k for k in value.keywords if k.arg == "errors" and isinstance(k.value, ast.Constant)
                      and k.value.value == "coerce"] if isinstance(value, ast.Call) else []
            if isinstance(value, ast.Call) and len(value.keywords) == len(coerce) and len(value.args) == 1 and (
                    (isinstance(value.func, ast.Attribute) and value.func.attr == "to_datetime" and
                     isinstance(value.func.value, ast.Name) and value.func.value.id in ("pd", "pandas")) or
                    (isinstance(value.func, ast.Name) and value.func.id == "to_datetime")) and \
                    self.column_of(value.args[0], variable) == column:
                return [f"PARSE_DATE {name}.{name_text(column)};"]
            try:
                return [f"DERIVE {name}.{name_text(column)} = {self.arithmetic(value, variable)};"]
            except Unsupported:
                pass
        raise Unsupported(f"a new value for column {column!r} that PrepLang cannot express")

    def arithmetic(self, node, variable):
        """A DERIVE value: + - * / over columns of the DataFrame and numbers, or a part of a date column."""
        column = self.column_of(node, variable)
        if column is not None:
            return name_text(column)
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(node.value, bool):
            return repr(node.value)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return f"-{self.arithmetic(node.operand, variable)}"
        if isinstance(node, ast.BinOp) and type(node.op) in ARITHMETIC:
            return (f"({self.arithmetic(node.left, variable)} {ARITHMETIC[type(node.op)]} "
                    f"{self.arithmetic(node.right, variable)})")
        if isinstance(node, ast.Attribute) and node.attr in DATE_PARTS and isinstance(node.value, ast.Attribute) and \
                node.value.attr == "dt" and self.column_of(node.value.value, variable) is not None:
            return f"{DATE_PARTS[node.attr]}({name_text(self.column_of(node.value.value, variable))})"
        raise Unsupported("a value that is not arithmetic over columns and numbers")

    def normalization(self, value, column, variable):
        """(c - c.min()) / (c.max() - c.min()) -> MINMAX;  (c - c.mean()) / c.std() -> ZSCORE"""
        def stat(node, name):
            return isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == name \
                and not node.args and not node.keywords and self.column_of(node.func.value, variable) == column

        def minus(node, name):
            return isinstance(node, ast.BinOp) and isinstance(node.op, ast.Sub) and \
                self.column_of(node.left, variable) == column and stat(node.right, name)

        if not (isinstance(value, ast.BinOp) and isinstance(value.op, ast.Div)):
            return None
        if minus(value.left, "min") and isinstance(value.right, ast.BinOp) and isinstance(value.right.op, ast.Sub) \
                and stat(value.right.left, "max") and stat(value.right.right, "min"):
            return "MINMAX"
        if minus(value.left, "mean") and stat(value.right, "std"):
            return "ZSCORE"
        return None

    def condition(self, node, variable):
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.BitAnd, ast.BitOr)):
            word = "AND" if isinstance(node.op, ast.BitAnd) else "OR"
            return f"({self.condition(node.left, variable)} {word} {self.condition(node.right, variable)})"
        # pandas keeps a row with a missing value for c != v and for ~(c == v) (NaN != v is True),
        # PrepLang drops it (the comparison is unknown); the two agree for every other comparison,
        # AND and OR. Whether a column has missing values is not known here, so these are refused.
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            raise Unsupported("~ of a row condition, which keeps rows with missing values in pandas")
        if isinstance(node, ast.Compare) and len(node.ops) == 1 and isinstance(node.ops[0], ast.NotEq):
            raise Unsupported("!= in a row condition, which keeps rows with missing values in pandas")
        if isinstance(node, ast.Compare) and len(node.ops) == 1 and type(node.ops[0]) in COMPARISONS:
            column = self.column_of(node.left, variable)
            if column is None:
                raise Unsupported("a comparison that is not column OP constant")
            return f"{name_text(column)} {COMPARISONS[type(node.ops[0])]} {literal(node.comparators[0])}"
        raise Unsupported("a row condition PrepLang cannot express")

    def expect(self, test, variable, name):
        # assert df["c"].between(a, b).all() / df["c"].notna().all() / df["c"].is_unique
        if isinstance(test, ast.Attribute) and test.attr == "is_unique":
            column = self.column_of(test.value, variable)
            if column is not None:
                return f"EXPECT {name}.{name_text(column)} UNIQUE;"
        if isinstance(test, ast.Call) and isinstance(test.func, ast.Attribute) and test.func.attr == "all" \
                and isinstance(test.func.value, ast.Call) and isinstance(test.func.value.func, ast.Attribute):
            inner = test.func.value
            column = self.column_of(inner.func.value, variable)
            if column is not None and inner.func.attr == "between" and len(inner.args) == 2:
                return f"EXPECT {name}.{name_text(column)} BETWEEN {literal(inner.args[0])} AND {literal(inner.args[1])};"
            if column is not None and inner.func.attr in ("notna", "notnull") and not inner.args:
                return f"EXPECT {name}.{name_text(column)} NOT_NULL;"
        raise Unsupported("an assert PrepLang cannot express")


# Attribute names that are DataFrame methods or properties, not columns (df.shape is not a column).
dir_methods = {"loc", "iloc", "shape", "columns", "index", "values", "dtypes", "T", "head", "tail", "describe",
               "info", "copy", "fillna", "dropna", "drop", "to_csv", "mean", "median", "mode", "std", "min", "max",
               "sum", "count", "isna", "isnull", "notna", "notnull", "apply", "map", "astype", "groupby", "merge",
               "join", "query", "sort_values", "reset_index", "set_index", "plot", "hist", "corr", "between",
               "is_unique", "unique", "nunique", "value_counts", "replace", "rename", "size", "empty"}


def lift(source, derived=True):
    """Lifts a pandas script; returns a Lifted, or raises SyntaxError for code Python 3 cannot parse.
    derived=False lifts as the version that produced the corpus results of RQ5 (no PARSE_DATE, no DERIVE)."""
    return Lifter(derived).lift(ast.parse(source))


def main():
    """pandas2prep.py script.py [-o out.prep]: the lifted program, and how far each DataFrame was lifted."""
    parser = argparse.ArgumentParser(prog="preplang-lift",
                                     description="Lift the cleaning part of a pandas script into PrepLang.")
    parser.add_argument("script", help="the pandas script (.py)")
    parser.add_argument("-o", dest="out", help="write the program here instead of printing it")
    args = parser.parse_args()
    with open(args.script, encoding="utf-8", errors="replace") as source:
        result = lift(source.read())
    if args.out:
        with open(args.out, "w", encoding="utf-8") as out:
            out.write(result.program())
        print(f"Wrote {args.out}", file=sys.stderr)
    else:
        print(result.program(), end="")
    for variable, frame in result.frames.items():
        print(f"# {variable}: " + (f"lifted until {frame.stopped_at}" if frame.stopped_at else "lifted completely"),
              file=sys.stderr)


if __name__ == "__main__":
    main()
