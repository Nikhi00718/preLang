#include "backend/PandasCodegen.h"

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
        case ColumnType::Boolean: return "boolean";
        case ColumnType::Integer: return "Int64";
        case ColumnType::Float:   return "Float64";
        case ColumnType::String:  return "string";
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

// {"marks": {"type": "INTEGER", "max": 100, "max_for": "line 5: ..."}, ...}
std::string PandasCodegen::certificateDict(const std::string& key) const {
    const TableCertificate* table = certificate_.table(key);
    if (certificate_.exactOnly || !table) {
        return "None";  // only the compiled file is accepted
    }
    const auto join = [](const std::vector<std::string>& items) {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : "; ") + item;
        }
        return pythonString(text);
    };
    std::string code = "{";
    for (const ColumnFacts& column : table->columns) {
        code += std::string(code.size() > 1 ? ",\n     " : "") + pythonString(column.name) + ": {\"type\": \"" +
                columnTypeName(column.type) + "\"";
        if (column.notNull) {
            code += ", \"not_null\": " + join(column.notNullFor);
        }
        if (!std::isinf(column.min)) {
            code += ", \"min\": " + exactNumber(column.min) + ", \"min_for\": " + join(column.minFor);
        }
        if (!std::isinf(column.max)) {
            code += ", \"max\": " + exactNumber(column.max) + ", \"max_for\": " + join(column.maxFor);
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

std::string PandasCodegen::helpers(bool csv, bool parquet) {
    std::string code = R"(# A table can be given another input file when the program is run:
#   py -3 generated.py students=new_students.csv
INPUTS = dict(arg.split("=", 1) for arg in sys.argv[1:] if "=" in arg)
same_input = {}  # table -> True if its file is the one the compiler analysed

KINDS = {
    "INTEGER": pd.api.types.is_integer_dtype,
    "FLOAT": lambda dtype: pd.api.types.is_numeric_dtype(dtype) and not pd.api.types.is_bool_dtype(dtype),
    "BOOLEAN": pd.api.types.is_bool_dtype,
    "STRING": lambda dtype: True,
}


def refuse(path, problem, needed_by):
    raise ContractError(f"{path} is not the file the program was compiled with, and {problem}"
                        + (f"; the compiled program relies on this for {needed_by}" if needed_by else "")
                        + ". Compile the .prep file again for this input.")


def first_row(mask):
    """1-based number of the first data row where mask is true."""
    return int(mask.fillna(False).to_numpy().argmax()) + 1


def admit(table, path, frame, certificate):
    """A file other than the compiled one may be used only if it has every fact of
    the program's data certificate: the facts the compiler's proofs and removed
    operations rely on. Contracts proven from the values are checked again later."""
    if certificate is None:
        refuse(path, "the compiled program's proofs depend on the exact values of that file", "")
    for column, fact in certificate.items():
        if column not in frame.columns:
            refuse(path, f"it has no column {column!r}", "")
        values = frame[column]
        if not values.isna().all() and not KINDS[fact["type"]](values.dtype):  # no values fit any type
            refuse(path, f"its column {column!r} is read as {values.dtype}, not {fact['type']}", "")
        if "not_null" in fact and values.isna().any():
            refuse(path, f"{column} is missing in data row {first_row(values.isna())}", fact["not_null"])
        if "min" in fact and (values < fact["min"]).any():
            row = first_row(values < fact["min"])
            refuse(path, f"{column} is {values.iloc[row - 1]} in data row {row}, below {fact['min']}", fact["min_for"])
        if "max" in fact and (values > fact["max"]).any():
            row = first_row(values > fact["max"])
            refuse(path, f"{column} is {values.iloc[row - 1]} in data row {row}, above {fact['max']}", fact["max_for"])
    print(f"Admitted {path} for {table}: it satisfies the data certificate")


)";
    if (csv) {
        code += R"(def read_csv_checked(table, path, fingerprint, columns, dtypes, certificate):
    """Reads a CSV file. The compiler's proofs were made for the file with this
    CRC-32 checksum; any other file is admitted only if it satisfies the data
    certificate. The nullable dtypes are applied after parsing: that gives the
    same values, and passing them to read_csv directly is several times slower."""
    path = Path(INPUTS[table]).resolve() if table in INPUTS else path
    data = path.read_bytes()
    same_input[table] = zlib.crc32(data) == fingerprint
    if same_input[table]:
        frame = pd.read_csv(io.BytesIO(data), usecols=columns, keep_default_na=False, na_values=[""],
                            dtype_backend="numpy_nullable")
        return frame.astype(dtypes)
    text = {column: "string" for column, kind in dtypes.items() if kind == "string"}
    frame = pd.read_csv(io.BytesIO(data), usecols=lambda column: column in columns, keep_default_na=False,
                        na_values=[""], dtype=text, dtype_backend="numpy_nullable")
    admit(table, path, frame, certificate)
    return frame[columns].astype(dtypes)


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
    same_input[table] = zlib.crc32(footer) == fingerprint
    if same_input[table]:
        return pd.read_parquet(path, columns=columns, dtype_backend="numpy_nullable").astype(dtypes)
    import pyarrow.parquet
    present = [column for column in columns if column in pyarrow.parquet.read_schema(path).names]
    frame = pd.read_parquet(path, columns=present, dtype_backend="numpy_nullable")
    admit(table, path, frame, certificate)
    return frame[columns].astype(dtypes)


)";
    }
    return code;
}

std::string PandasCodegen::code(const Instruction& in, const IRProgram& program) {
    const std::string table = tableVariable(in.dataset, in.table);
    const std::string column = table + "[" + pythonString(in.column) + "]";
    const std::string temp = "t" + std::to_string(in.result);
    const std::string where = "line " + std::to_string(in.line) + ": " + tableKey(in.dataset, in.table) + "." + in.column;
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
            std::string dtypes = "{";
            for (const ColumnInfo& info : schema.columns) {
                for (const std::string& name : in.columns) {
                    if (name == info.name) {
                        dtypes += (dtypes.size() > 1 ? ", " : "") + pythonString(name) + ": " +
                                  pythonString(pandasDtype(info.type));
                    }
                }
            }
            const char* reader = schema.format == TableFormat::Parquet ? "read_parquet_checked" : "read_csv_checked";
            const std::string key = tableKey(in.dataset, in.table);
            return code + table + " = " + reader + "(\n    " + pythonString(key) + ",\n    HERE / " +
                   pythonString(pathFrom(outputFolder_, schema.file)) + ",\n    " + hex(schema.fingerprint) +
                   ",\n    " + pythonList(in.columns) + ",\n    " + dtypes + "},\n    " + certificateDict(key) +
                   ",\n)\n";
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
        case Opcode::FillNull:
            return column + " = " + column + ".fillna(" + pythonValue(in.values[0]) + ")\n";
        case Opcode::DropNull:
            return table + " = " + table + ".dropna()\n";
        case Opcode::Normalize: {
            // A constant column (spread 0) or one with too few values becomes 0.0
            // instead of dividing by zero; missing values stay missing.
            const bool zscore = in.text == "ZSCORE";
            return "column = " + column + "\nspread = " + (zscore ? "column.std()" : "column.max() - column.min()") +
                   "\n" + column + " = (column - " + (zscore ? "column.mean()" : "column.min()") +
                   ") / spread if pd.notna(spread) and spread > 0 else column * 0.0\n";
        }
        case Opcode::Compare:
            return temp + " = " + column + " " + in.text + " " + pythonValue(in.values[0]) + "\n";
        case Opcode::And:
            return temp + " = t" + std::to_string(in.uses[0]) + " & t" + std::to_string(in.uses[1]) + "\n";
        case Opcode::Or:
            return temp + " = t" + std::to_string(in.uses[0]) + " | t" + std::to_string(in.uses[1]) + "\n";
        case Opcode::Not:
            return temp + " = ~t" + std::to_string(in.uses[0]) + "\n";
        case Opcode::Filter:  // rows where the condition is missing (NA) are dropped, like in SQL
            return table + " = " + table + "[t" + std::to_string(in.uses[0]) + "]\n";
        case Opcode::Select:
            return table + " = " + table + "[" + pythonList(in.columns) + "]\n";
        case Opcode::Drop:
            return table + " = " + table + ".drop(columns=" + pythonList(in.columns) + ")\n";
        case Opcode::Save:
            return "path = HERE / " + pythonString(in.text) +
                   "\npath.parent.mkdir(parents=True, exist_ok=True)\n" + table +
                   (isParquetPath(in.text) ? ".to_parquet(path, index=False)" : ".to_csv(path, index=False)") +
                   "\noutputs.append({\"file\": " + pythonString(in.text) +
                   ", \"rows\": len(" + table + ")})\nprint(\"Saved\", len(" + table + "), \"rows to\", path)\n";
        default:
            return "";  // image instructions belong to the OpenCV backend
    }
}
