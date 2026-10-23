#include "backend/PandasCodegen.h"

#include "common/Dates.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string pythonValue(const Literal& literal) {
    switch (literal.kind) {
        case LiteralKind::Number:  return literal.value;
        case LiteralKind::String:  return pythonString(literal.value);
        case LiteralKind::Boolean: return literal.value == "TRUE" ? "True" : "False";
    }
    return "";
}

std::string identifierPart(const std::string& name) {
    std::string text;
    for (char c : name) {
        text += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    return text;
}

const char* pandasDtype(ColumnType type) {
    switch (type) {
        case ColumnType::Boolean:  return "boolean";
        case ColumnType::Integer:  return "Int64";
        case ColumnType::Float:    return "Float64";
        case ColumnType::String:   return "string";
        case ColumnType::DateTime: return "";  // kept as pandas reads it (datetime64)
    }
    return "";
}

bool isParquetPath(const std::string& path) {
    const std::string suffix = ".parquet";
    return path.size() >= suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), path.rbegin(),
                      [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
}

std::string hex(std::uint64_t value) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return text.str();
}

// A DERIVE value as a pandas expression over the columns of `table`.
std::string pandasValue(const ValueExpr& expr, const std::string& table) {
    switch (expr.kind) {
        case ValueKind::Number: return expr.text;
        case ValueKind::Column: return table + "[" + pythonString(expr.text) + "]";
        case ValueKind::Binary:
            return "(" + pandasValue(*expr.left, table) + " " + expr.text + " " + pandasValue(*expr.right, table) + ")";
        case ValueKind::DatePart: {
            std::string part;
            for (char c : expr.text) {
                part += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            // INTEGER, with missing values where the date is missing
            return table + "[" + pythonString(expr.left->text) + "].dt." + part + ".astype(\"Int64\")";
        }
    }
    return "";
}

}  // namespace

std::string pythonString(const std::string& text) {
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "\"";
}

std::string pythonList(const std::vector<std::string>& items) {
    std::string text = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + pythonString(items[i]);
    }
    return text + "]";
}

// Path of an input file relative to the output folder, e.g. ../examples/students.csv
std::string pathFrom(const std::filesystem::path& outputFolder, const std::string& inputPath) {
    const std::filesystem::path file = std::filesystem::absolute(inputPath).lexically_normal();
    const std::filesystem::path folder = std::filesystem::absolute(outputFolder).lexically_normal();
    const std::filesystem::path relative = file.lexically_relative(folder);
    return (relative.empty() ? file : relative).generic_u8string();
}

std::string exactNumber(double value) {
    char text[32];
    const auto end = std::to_chars(text, text + sizeof text, value).ptr;  // shortest text that reads back exactly
    return std::string(text, end);
}

PandasCodegen::PandasCodegen(std::filesystem::path outputFolder, const Certificate& certificate)
    : outputFolder_(std::move(outputFolder)), certificate_(certificate) {}

std::string PandasCodegen::certificateDict(const std::string& key) const {
    const TableCertificate* table = certificate_.table(key);
    if (certificate_.exactOnly || !table) {
        return "None";  // only the compiled file is accepted
    }
    return columnsDict(*table);
}

std::string PandasCodegen::columnsDict(const TableCertificate& certificate) {
    const TableCertificate* table = &certificate;
    const auto join = [](const std::vector<std::string>& items) {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : "; ") + item;
        }
        return pythonString(text);
    };
    std::string code = "{";
    for (const ColumnFacts& column : table->columns) {
        code += std::string(code.size() > 1 ? ",\n     " : "") + pythonString(column.name) +
                ": {\"type\": " + (column.typeFixed ? "\"" + requiredTypeName(column) + "\"" : std::string("None")) +
                (column.required ? "" : ", \"optional\": True");
        if (column.notNull) {
            code += ", \"not_null\": " + join(column.notNullFor);
        }
        if (!std::isinf(column.min)) {
            code += ", \"min\": " + exactNumber(column.min) + (column.minStrict ? ", \"min_strict\": True" : "") +
                    ", \"min_for\": " + join(column.minFor);
        }
        if (!std::isinf(column.max)) {
            code += ", \"max\": " + exactNumber(column.max) + (column.maxStrict ? ", \"max_strict\": True" : "") +
                    ", \"max_for\": " + join(column.maxFor);
        }
        if (column.dates) {
            code += ", \"dates\": " + join(column.datesFor);
        }
        if (!std::isinf(column.dateMin)) {
            code += ", \"date_min\": " + pythonString(dateText(column.dateMin)) +
                    ", \"date_min_for\": " + join(column.dateMinFor);
        }
        if (!std::isinf(column.dateMax)) {
            code += ", \"date_max\": " + pythonString(dateText(column.dateMax)) +
                    ", \"date_max_for\": " + join(column.dateMaxFor);
        }
        code += "}";
    }
    return code + "}";
}

std::string PandasCodegen::inputChanged(const Instruction& in) {
    std::string same;
    for (const std::string& table : in.provenFromValuesOf) {
        same += (same.empty() ? "" : " and ") + std::string("same_input[") + pythonString(table) + "]";
    }
    return "not (" + same + ")";
}

std::string PandasCodegen::tableVariable(const std::string& dataset, const std::string& table) {
    return table.empty() ? "df_" + identifierPart(dataset)
                         : "tables_" + identifierPart(dataset) + "[" + pythonString(table) + "]";
}

std::string PandasCodegen::chartHelpers() {
    return R"py(import base64
import html

import matplotlib

matplotlib.use("Agg")  # no window: the charts go to files
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator


def draw(axes, kind, frame, columns, bins=0, bounds=()):
    """One chart of PLOT on axes. Missing values are left out, except in BAR, which counts them."""
    if kind == "HISTOGRAM":
        values = frame[columns[0]].dropna().astype("float64")
        axes.hist(values, bins=bins or 10, color="#4c72b0")
        for low, high, contract in bounds:
            for x in (low, high):
                axes.axvline(x, color="#c44e52", linestyle="--", linewidth=1)
            axes.text(high, axes.get_ylim()[1], contract + " ", color="#c44e52", fontsize=7, ha="right", va="top")
        axes.set_xlabel(columns[0])
        axes.set_ylabel("rows")
        axes.yaxis.set_major_locator(MaxNLocator(integer=True))
    elif kind == "BAR":
        counts = frame[columns[0]].astype("string").fillna("(missing)").value_counts().sort_index()
        axes.bar(list(counts.index), counts.to_numpy(), color="#4c72b0")
        axes.set_xlabel(columns[0])
        axes.set_ylabel("rows")
        axes.tick_params(axis="x", labelrotation=45)
    else:
        data = frame[columns].dropna()
        if kind == "LINE":
            data = data.sort_values(columns[0])
            axes.plot(data[columns[0]].to_numpy(), data[columns[1]].astype("float64").to_numpy(), color="#4c72b0")
        else:
            axes.scatter(data[columns[0]].astype("float64"), data[columns[1]].astype("float64"), s=8,
                         color="#4c72b0")
        axes.set_xlabel(columns[0])
        axes.set_ylabel(columns[1])


def plot(table, kind, frame, columns, path, bins=0, bounds=()):
    """PLOT: one chart, as an image file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    figure, axes = plt.subplots(figsize=(6.4, 4.0))
    draw(axes, kind, frame, columns, bins, bounds)
    axes.set_title(f"{table}: {kind.lower()} of {' and '.join(columns)} ({len(frame)} rows)", fontsize=9)
    figure.tight_layout()
    figure.savefig(path)
    plt.close(figure)
    outputs.append({"file": path.name, "rows": len(frame)})
    print("Plotted", kind.lower(), "of", ", ".join(columns), "to", path)


def chart_html(kind, frame, column):
    """A small chart of one column, as an <img> with the PNG inline."""
    figure, axes = plt.subplots(figsize=(3.2, 1.8))
    draw(axes, kind, frame, [column])
    axes.set_xlabel("")
    figure.tight_layout()
    buffer = io.BytesIO()
    figure.savefig(buffer, format="png")
    plt.close(figure)
    return '<img alt="" src="data:image/png;base64,' + base64.b64encode(buffer.getvalue()).decode() + '">'


def write_report(table, frame, path, facts, contracts):
    """REPORT: every column as this run has it, next to what the compiler knew about it here."""
    path.parent.mkdir(parents=True, exist_ok=True)
    known = dict(facts)
    rows = []
    for column in frame.columns:
        values = frame[column]
        present = values.dropna()
        numeric = pd.api.types.is_numeric_dtype(values.dtype) and not pd.api.types.is_bool_dtype(values.dtype)
        dated = pd.api.types.is_datetime64_any_dtype(values.dtype)
        spread = f"{present.min()} to {present.max()}" if (numeric or dated) and len(present) else ""
        if numeric and len(present):
            chart = chart_html("HISTOGRAM", frame, column)
        elif values.nunique(dropna=False) <= 30:
            chart = chart_html("BAR", frame, column)
        else:
            chart = ""
        rows.append("<tr>" + "".join(f"<td>{html.escape(str(cell))}</td>" for cell in (
            column, known.get(column, ""), values.dtype, int(values.isna().sum()), values.nunique(), spread)) +
            f"<td>{chart}</td></tr>")
    checks = "".join(f"<li>{html.escape(text)}: {html.escape(verdict)}</li>" for text, verdict in contracts)
    rerun = "".join(f"<li>{html.escape(item)}</li>" for item in deoptimized)
    inputs = "".join(f"<li>{html.escape(name)}: " + ("the file it was compiled with" if same else
                     "another file, admitted by its data certificate") + "</li>" for name, same in same_input.items())
    page = f"""<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(table)}</title>
<style>body{{font-family:sans-serif;margin:24px;color:#222}}table{{border-collapse:collapse}}
td,th{{border:1px solid #ccc;padding:4px 8px;text-align:left;vertical-align:top}}th{{background:#f2f2f2}}</style>
</head><body><h1>{html.escape(table)}: {len(frame)} rows, {len(frame.columns)} columns</h1>
<h2>Inputs</h2><ul>{inputs}</ul>
<h2>Columns</h2><table><tr><th>column</th><th>known at compile time</th><th>dtype</th><th>missing</th>
<th>distinct</th><th>range</th><th>values</th></tr>{"".join(rows)}</table>
<h2>Contracts</h2><ul>{checks or "<li>none</li>"}</ul>
<h2>Removed checks that ran again on this input</h2><ul>{rerun or "<li>none</li>"}</ul>
</body></html>"""
    path.write_text(page, encoding="utf-8")
    outputs.append({"file": path.name, "rows": len(frame)})
    print("Wrote the report of", table, "to", path)


)py";
}

std::string PandasCodegen::helpers(bool csv, bool parquet) {
    std::string code = R"(KINDS = {
    "INTEGER": pd.api.types.is_integer_dtype,
    "FLOAT": pd.api.types.is_float_dtype,
    "BOOLEAN": pd.api.types.is_bool_dtype,
    "STRING": lambda dtype: True,
    "DATETIME": pd.api.types.is_datetime64_any_dtype,
    "NUMBER": lambda dtype: pd.api.types.is_numeric_dtype(dtype) and not pd.api.types.is_bool_dtype(dtype),
}


def resolved(frame, dtypes):
    """The dtypes to apply: a "number" column (INTEGER or FLOAT in the certificate) keeps
    the kind the input has, as a compilation of that input would read it."""
    return {column: ("Int64" if pd.api.types.is_integer_dtype(frame[column].dtype) else "Float64")
            if kind == "number" else kind for column, kind in dtypes.items() if column in frame.columns}


def first_row(mask):
    """1-based number of the first data row where mask is true."""
    return int(mask.fillna(False).to_numpy().argmax()) + 1


DATE_LAYOUTS = {10: "%Y-%m-%d", 16: "%Y-%m-%d %H:%M", 19: "%Y-%m-%d %H:%M:%S"}


def as_dates(values):
    """The dates in a column of text, as PARSE_DATE reads them: every present value in
    the ISO layout of the first one. None if a value is not such a date."""
    present = values.dropna().astype(str)
    if present.empty:
        return pd.Series(pd.NaT, index=values.index, dtype="datetime64[ns]")
    first = present.iloc[0]
    layout = DATE_LAYOUTS.get(len(first), "")
    if len(first) == 19 and first[10] == "T":
        layout = "%Y-%m-%dT%H:%M:%S"
    dates = pd.to_datetime(values, format=layout or None, errors="coerce") if layout else None
    if dates is None or (present.str.len() != len(first)).any() or dates[values.notna()].isna().any():
        return None
    return dates


def admit(table, path, frame, certificate, empty_is_text):
    """A file other than the compiled one may be used only if it has every fact of
    the program's data certificate: the facts the compiler's proofs and removed
    operations rely on. Contracts proven from the values are checked again later.
    Types must be the ones the compiler infers; in a CSV file a column without any
    value is STRING (empty_is_text)."""
    if certificate is None:
        refuse(path, "the compiled program's proofs depend on the exact values of its file", "")
    for column, fact in certificate.items():
        if column not in frame.columns:
            if fact.get("optional"):
                continue  # only carried to the output, which then lacks it
            refuse(path, f"it has no column {column!r}", "")
        values = frame[column]
        empty = empty_is_text and values.isna().all()
        if fact["type"] and not (fact["type"] == "STRING" if empty else KINDS[fact["type"]](values.dtype)):
            kind = "a column without values, so STRING" if empty else values.dtype
            refuse(path, f"its column {column!r} is {kind}, not {fact['type']}", "")
        if "not_null" in fact and values.isna().any():
            refuse(path, f"{column} is missing in data row {first_row(values.isna())}", fact["not_null"])
        if "min" in fact:
            low = values <= fact["min"] if fact.get("min_strict") else values < fact["min"]
            if low.any():
                row = first_row(low)
                limit = ("at or below " if fact.get("min_strict") else "below ") + str(fact["min"])
                refuse(path, f"{column} is {values.iloc[row - 1]} in data row {row}, {limit}", fact["min_for"])
        if "max" in fact:
            high = values >= fact["max"] if fact.get("max_strict") else values > fact["max"]
            if high.any():
                row = first_row(high)
                limit = ("at or above " if fact.get("max_strict") else "above ") + str(fact["max"])
                refuse(path, f"{column} is {values.iloc[row - 1]} in data row {row}, {limit}", fact["max_for"])
        if "dates" in fact or "date_min" in fact or "date_max" in fact:
            dates = as_dates(values)
            if dates is None:
                refuse(path, f"{column} has values that are not dates in one ISO layout", fact.get("dates", ""))
            if "date_min" in fact and (dates < pd.Timestamp(fact["date_min"])).any():
                refuse(path, f"{column} has dates before {fact['date_min']}", fact["date_min_for"])
            if "date_max" in fact and (dates > pd.Timestamp(fact["date_max"])).any():
                refuse(path, f"{column} has dates after {fact['date_max']}", fact["date_max_for"])
    print(f"Admitted {path} for {table}: it satisfies the data certificate")


def column_facts(frame, columns):
    """Missing values and range of the columns some item certificate is about."""
    facts = {}
    for column in columns:
        if column in frame.columns:
            values = frame[column]
            present = values.dropna()
            numeric = pd.api.types.is_numeric_dtype(values.dtype) and not pd.api.types.is_bool_dtype(values.dtype)
            facts[column] = {"missing": int(values.isna().sum()),
                             "min": present.min() if numeric and len(present) else None,
                             "max": present.max() if numeric and len(present) else None}
            dates = None if numeric else as_dates(values)
            if dates is not None and dates.notna().any():
                facts[column].update(date_min=dates.min(), date_max=dates.max())
    return facts


def table_problem(facts, certificate):
    """Why a new input breaks the item certificate of a removed check or operation; "" if it does not."""
    for column, fact in certificate.items():
        found = facts.get(column)
        if found is None:
            continue
        if "not_null" in fact and found["missing"]:
            return f"{column} has {found['missing']} missing values"
        low, high = found["min"], found["max"]
        if "min" in fact and low is not None and (low <= fact["min"] if fact.get("min_strict") else low < fact["min"]):
            return f"{column} goes down to {low}, " + ("at or below " if fact.get("min_strict") else "below ") + str(fact["min"])
        if "max" in fact and high is not None and (high >= fact["max"] if fact.get("max_strict") else high > fact["max"]):
            return f"{column} goes up to {high}, " + ("at or above " if fact.get("max_strict") else "above ") + str(fact["max"])
        if "date_min" in fact and "date_min" in found and found["date_min"] < pd.Timestamp(fact["date_min"]):
            return f"{column} goes back to {found['date_min']}, before {fact['date_min']}"
        if "date_max" in fact and "date_max" in found and found["date_max"] > pd.Timestamp(fact["date_max"]):
            return f"{column} goes up to {found['date_max']}, after {fact['date_max']}"
    return ""


)";
    if (csv) {
        code += R"(def read_csv_checked(table, path, fingerprint, columns, dtypes, certificate):
    """Reads a CSV file. The compiler's proofs were made for the file with this
    CRC-32 checksum; any other file is admitted only if it satisfies the data
    certificate. After a private compilation there is no checksum (None): the
    compiler never saw the file, so every input is checked against the
    certificate. The nullable dtypes are applied after parsing: that gives the
    same values, and passing them to read_csv directly is several times slower."""
    path = Path(INPUTS[table]).resolve() if table in INPUTS else path
    data = path.read_bytes()
    # Without a checksum (a private compilation) nothing is trusted, not even for experiments.
    same_input[table] = fingerprint is not None and (zlib.crc32(data) == fingerprint or TRUST_INPUTS)
    if same_input[table]:
        frame = pd.read_csv(io.BytesIO(data), usecols=columns, keep_default_na=False, na_values=[""],
                            dtype_backend="numpy_nullable")
        return frame.astype(resolved(frame, dtypes))
    # Also the columns the program does not use but its proofs depend on.
    text = {column: "string" for column, kind in dtypes.items() if kind == "string"}
    frame = pd.read_csv(io.BytesIO(data), usecols=lambda column: column in columns or column in (certificate or {}),
                        keep_default_na=False, na_values=[""], dtype=text, dtype_backend="numpy_nullable")
    admit(table, path, frame, certificate, True)
    input_facts[table] = column_facts(frame, ITEM_COLUMNS.get(table, ()))
    present = [column for column in columns if column in frame.columns]
    return frame[present].astype(resolved(frame[present], dtypes))


)";
    }
    if (parquet) {
        code += R"(def read_parquet_checked(table, path, fingerprint, columns, dtypes, certificate):
    """Reads a Parquet file. The compiler's proofs were made from the statistics in
    the footer with this CRC-32 checksum; any other file is admitted only if it
    satisfies the data certificate."""
    path = Path(INPUTS[table]).resolve() if table in INPUTS else path
    with open(path, "rb") as file:
        file.seek(-8, 2)
        length = int.from_bytes(file.read(4), "little")
        file.seek(-8 - length, 2)
        footer = file.read()
    same_input[table] = fingerprint is not None and (zlib.crc32(footer) == fingerprint or TRUST_INPUTS)
    if same_input[table]:
        frame = pd.read_parquet(path, columns=columns, dtype_backend="numpy_nullable")
        return frame.astype(resolved(frame, dtypes))
    import pyarrow.parquet
    wanted = columns + [column for column in certificate or {} if column not in columns]
    present = [column for column in wanted if column in pyarrow.parquet.read_schema(path).names]
    frame = pd.read_parquet(path, columns=present, dtype_backend="numpy_nullable")
    admit(table, path, frame, certificate, False)
    input_facts[table] = column_facts(frame, ITEM_COLUMNS.get(table, ()))
    present = [column for column in columns if column in frame.columns]
    return frame[present].astype(resolved(frame[present], dtypes))


)";
    }
    return code;
}

std::string PandasCodegen::code(const Instruction& in, const IRProgram& program) {
    const std::string table = tableVariable(in.dataset, in.table);
    const std::string column = table + "[" + pythonString(in.column) + "]";
    const std::string temp = "t" + std::to_string(in.result);
    const std::string where =
        "line " + std::to_string(in.line) + ": " + tableKey(in.dataset, in.table) + "." + in.column;
    const auto fail = [&](const std::string& message) {
        return "    raise ContractError(" + pythonString(where + " " + message) + ")\n";
    };

    switch (in.op) {
        case Opcode::Load: {
            std::string code;
            if (!in.table.empty() && declaredSets_.insert(in.dataset).second) {
                code = "tables_" + identifierPart(in.dataset) + " = {}\n";
            }
            const TableSchema& schema = program.tables.at(tableKey(in.dataset, in.table));
            const TableCertificate* certified =
                certificate_.exactOnly ? nullptr : certificate_.table(tableKey(in.dataset, in.table));
            // A numberKind column is read as INTEGER or FLOAT, whichever the input has ("number").
            const auto numberKind = [&](const std::string& name) {
                return certified && std::any_of(certified->columns.begin(), certified->columns.end(),
                                                [&](const ColumnFacts& c) { return c.name == name && c.numberKind; });
            };
            std::string dtypes = "{";
            for (const ColumnInfo& info : schema.columns) {
                for (const std::string& name : in.columns) {
                    if (name == info.name && info.type != ColumnType::DateTime) {
                        dtypes += (dtypes.size() > 1 ? ", " : "") + pythonString(name) + ": " +
                                  pythonString(numberKind(name) ? "number" : pandasDtype(info.type));
                    }
                }
            }
            const char* reader = schema.format == TableFormat::Parquet ? "read_parquet_checked" : "read_csv_checked";
            const std::string key = tableKey(in.dataset, in.table);
            return code + table + " = " + reader + "(\n    " + pythonString(key) + ",\n    HERE / " +
                   pythonString(pathFrom(outputFolder_, schema.file)) + ",\n    " +
                   (schema.released ? std::string("None") : hex(schema.fingerprint)) + ",\n    " +
                   pythonList(in.columns) + ",\n    " + dtypes + "},\n    " + certificateDict(key) + ",\n)\n";
        }
        case Opcode::Check: {
            // A check proven from the values runs only when an input changed.
            const std::string when = in.onlyIfInputChanged ? "if " + inputChanged(in) + " and " : "if ";
            if (in.text == "NOT_NULL") {
                return when + column + ".isna().any():\n" + fail("has missing values");
            }
            if (in.text == "BETWEEN") {
                return when + "not " + column + ".dropna().between(" + in.values[0].value + ", " + in.values[1].value +
                       ").all():\n" + fail("has values outside " + in.values[0].value + " to " + in.values[1].value);
            }
            if (in.text == "UNIQUE") {
                return when + column + ".dropna().duplicated().any():\n" + fail("has repeated values");
            }
            // REFERENCES
            return when + "not " + column + ".dropna().isin(" + tableVariable(in.columns[0], in.columns[1]) + "[" +
                   pythonString(in.columns[2]) + "].dropna()).all():\n" +
                   fail("has values missing from " + tableKey(in.columns[0], in.columns[1]) + "." + in.columns[2]);
        }
        case Opcode::FillNull: {
            if (!in.values.empty()) {
                return column + " = " + column + ".fillna(" + pythonValue(in.values[0]) + ")\n";
            }
            // The statistic is computed from the data; MEAN and MEDIAN make the column FLOAT.
            // With missing values but no value to compute it from, the fill would do nothing.
            const std::string statistic = in.text == "MEAN" ? "column.mean()"
                                          : in.text == "MEDIAN"
                                              ? "column.median()"
                                              : "column.mode().iloc[0] if column.notna().any() else pd.NA";
            return "column = " + column + (in.text == "MODE" ? "" : ".astype(\"Float64\")") + "\nvalue = " + statistic +
                   "\nif column.isna().any() and pd.isna(value):\n" +
                   fail("has no values to compute the " + in.text + " from") + column + " = column.fillna(value)\n";
        }
        case Opcode::DropNull:
            return table + " = " + table + ".dropna(" +
                   (in.columns.empty() ? std::string() : "subset=" + pythonList(in.columns)) + ")\n";
        case Opcode::Normalize: {
            // A constant column (spread 0) or one with too few values becomes 0.0
            // instead of dividing by zero; missing values stay missing.
            const bool zscore = in.text == "ZSCORE";
            return "column = " + column + "\nspread = " + (zscore ? "column.std()" : "column.max() - column.min()") +
                   "\n" + column + " = (column - " + (zscore ? "column.mean()" : "column.min()") +
                   ") / spread if pd.notna(spread) and spread > 0 else column * 0.0\n";
        }
        case Opcode::Compare:
            // pandas 3 compares a string column into bool[pyarrow], which & and | with a
            // boolean mask holding NA cannot combine; as boolean it is like every other mask.
            if (in.values[0].kind == LiteralKind::String) {
                return temp + " = (" + column + " " + in.text + " " + pythonValue(in.values[0]) +
                       ").astype(\"boolean\")\n";
            }
            return temp + " = " + column + " " + in.text + " " + pythonValue(in.values[0]) + "\n";
        case Opcode::And:
            return temp + " = t" + std::to_string(in.uses[0]) + " & t" + std::to_string(in.uses[1]) + "\n";
        case Opcode::Or:  return temp + " = t" + std::to_string(in.uses[0]) + " | t" + std::to_string(in.uses[1]) + "\n";
        case Opcode::Not: return temp + " = ~t" + std::to_string(in.uses[0]) + "\n";
        case Opcode::Filter:  // rows where the condition is missing (NA) are dropped, like in SQL
            return table + " = " + table + "[t" + std::to_string(in.uses[0]) + "]\n";
        case Opcode::Select: return table + " = " + table + "[" + pythonList(in.columns) + "]\n";
        case Opcode::ParseDate:  // the compiler checked that every value is a date in one ISO layout
            return column + " = pd.to_datetime(" + column + ")\n";
        case Opcode::Derive: return column + " = " + pandasValue(*in.value, table) + "\n";
        case Opcode::Drop:   return table + " = " + table + ".drop(columns=" + pythonList(in.columns) + ")\n";
        case Opcode::Join:   {  // a left join on a key the lookup table has once; a missing key matches nothing
            std::vector<std::string> read = {in.column};
            read.insert(read.end(), in.columns.begin(), in.columns.end());
            const std::string key = pythonString(in.column);
            if (in.text == "MANY") {  // every matching row, in the order of the other table
                return "right = " + tableVariable(in.withDataset, in.withTable) + "[" + pythonList(read) +
                       "].dropna(subset=[" + key + "])\n" + table + " = " + table + ".merge(right, on=" + key +
                       ", how=\"left\")\n";
            }
            const std::string lookup = tableKey(in.withDataset, in.withTable) + "." + in.column;
            return "right = " + tableVariable(in.withDataset, in.withTable) + "[" + pythonList(read) +
                   "].dropna(subset=[" + key + "])\nif right[" + key + "].duplicated().any():\n" +
                   "    raise ContractError(" +
                   pythonString("line " + std::to_string(in.line) + ": JOIN needs each " + lookup +
                                " once, but it repeats ") +
                   " + repr(str(right.loc[right[" + key + "].duplicated(), " + key + "].iloc[0])))\n" + table + " = " +
                   table + ".merge(right, on=" + key + ", how=\"left\")\n";
        }
        case Opcode::Aggregate: {  // one row per group, sorted by the BY values; a missing BY value is in no group
            std::string code = "grouped = " + table + ".groupby(" + pythonList(in.columns) +
                               ", dropna=True, sort=True)\n" + table + " = pd.DataFrame({";
            std::string types;
            for (size_t i = 0; i < in.aggregates.size(); ++i) {
                const AggregateResult& item = in.aggregates[i];
                const std::string statistic = item.function == "COUNT"
                                                  ? "grouped.size()"
                                                  : "grouped[" + pythonString(item.column) + "]." +
                                                        (item.function == "SUM"              ? "sum()"
                                                         : item.function == "MEAN"           ? "mean()"
                                                         : item.function == "MEDIAN"         ? "median()"
                                                         : item.function == "STD"            ? "std()"
                                                         : item.function == "COUNT_DISTINCT" ? "nunique()"
                                                         : item.function == "MIN"            ? "min()"
                                                                                             : "max()");
                code += (i > 0 ? ", " : "") + pythonString(item.name) + ": " + statistic;
                types += (i > 0 ? ", " : "") + pythonString(item.name) + ": " +
                         pythonString(item.type == "FLOAT" ? "Float64" : "Int64");
            }
            return code + "}).reset_index().astype({" + types + "})\n";
        }
        case Opcode::Plot: {
            // The bounds of the BETWEEN contracts on the column so far, drawn on a histogram.
            std::string bounds = "[";
            if (in.column == "HISTOGRAM") {
                for (const Instruction& check : program.code) {
                    if (check.op == Opcode::Check && check.text == "BETWEEN" && check.line < in.line &&
                        check.dataset == in.dataset && check.table == in.table && check.column == in.columns[0]) {
                        bounds += std::string(bounds.size() > 1 ? ", " : "") + "(" + check.values[0].value + ", " +
                                  check.values[1].value + ", " +
                                  pythonString("line " + std::to_string(check.line) + ": BETWEEN " +
                                               check.values[0].value + " AND " + check.values[1].value) +
                                  ")";
                    }
                }
            }
            return "plot(" + pythonString(tableKey(in.dataset, in.table)) + ", " + pythonString(in.column) + ", " +
                   table + ", " + pythonList(in.columns) + ", HERE / " + pythonString(in.text) + ", " +
                   std::to_string(in.numbers.empty() ? 0 : in.numbers[0]) + ", " + bounds + "])\n";
        }
        case Opcode::Report: {
            std::string facts = "[";
            std::string contracts = "[";
            for (const std::string& item : in.columns) {
                const size_t first = item.find('\t');
                const size_t second = item.find('\t', first + 1);
                const std::string kind = item.substr(0, first);
                const std::string pair = "(" + pythonString(item.substr(first + 1, second - first - 1)) + ", " +
                                         pythonString(item.substr(second + 1)) + ")";
                std::string& list = kind == "column" ? facts : contracts;
                list += std::string(list.size() > 1 ? ", " : "") + pair;
            }
            return "write_report(" + pythonString(tableKey(in.dataset, in.table)) + ", " + table + ", HERE / " +
                   pythonString(in.text) + ",\n       " + facts + "],\n       " + contracts + "])\n";
        }
        case Opcode::Save:
            return "path = HERE / " + pythonString(in.text) + "\npath.parent.mkdir(parents=True, exist_ok=True)\n" +
                   table + (isParquetPath(in.text) ? ".to_parquet(path, index=False)" : ".to_csv(path, index=False)") +
                   "\noutputs.append({\"file\": " + pythonString(in.text) + ", \"rows\": len(" + table +
                   ")})\nprint(\"Saved\", len(" + table + "), \"rows to\", path)\n";
        default: return "";  // image instructions belong to the OpenCV backend
    }
}
