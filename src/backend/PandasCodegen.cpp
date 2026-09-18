#include "backend/PandasCodegen.h"

#include <cctype>
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

PandasCodegen::PandasCodegen(std::filesystem::path outputFolder) : outputFolder_(std::move(outputFolder)) {}

std::string PandasCodegen::tableVariable(const std::string& dataset, const std::string& table) {
    return table.empty() ? "df_" + identifierPart(dataset)
                         : "tables_" + identifierPart(dataset) + "[" + pythonString(table) + "]";
}

std::string PandasCodegen::helpers() {
    return R"(def read_csv_checked(path, fingerprint, columns, dtypes):
    """Reads a CSV file, but refuses if the file changed after compilation,
    because the compiler's checks and proofs were made for the file as it was then."""
    if zlib.crc32(path.read_bytes()) != fingerprint:
        raise ContractError(f"{path} changed after compilation; compile the .prep file again")
    return pd.read_csv(path, usecols=columns, dtype=dtypes, keep_default_na=False, na_values=[""])


)";
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
            return code + table + " = read_csv_checked(\n    HERE / " + pythonString(pathFrom(outputFolder_, schema.file)) +
                   ",\n    " + hex(schema.fingerprint) + ",\n    " + pythonList(in.columns) + ",\n    " + dtypes +
                   "},\n)\n";
        }
        case Opcode::Check:
            if (in.text == "NOT_NULL") {
                return "if " + column + ".isna().any():\n" + fail("has missing values");
            }
            if (in.text == "BETWEEN") {
                return "if not " + column + ".dropna().between(" + in.values[0].value + ", " + in.values[1].value +
                       ").all():\n" + fail("has values outside " + in.values[0].value + " to " + in.values[1].value);
            }
            if (in.text == "UNIQUE") {
                return "if " + column + ".dropna().duplicated().any():\n" + fail("has repeated values");
            }
            // REFERENCES
            return "if not " + column + ".dropna().isin(" + tableVariable(in.columns[0], in.columns[1]) + "[" +
                   pythonString(in.columns[2]) + "].dropna()).all():\n" +
                   fail("has values missing from " + tableKey(in.columns[0], in.columns[1]) + "." + in.columns[2]);
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
            return table + " = " + table + "[t" + std::to_string(in.uses[0]) + "].copy()\n";
        case Opcode::Select:
            return table + " = " + table + "[" + pythonList(in.columns) + "].copy()\n";
        case Opcode::Drop:
            return table + " = " + table + ".drop(columns=" + pythonList(in.columns) + ")\n";
        case Opcode::Save:
            return "path = HERE / " + pythonString(in.text) +
                   "\npath.parent.mkdir(parents=True, exist_ok=True)\n" + table +
                   ".to_csv(path, index=False)\noutputs.append({\"file\": " + pythonString(in.text) +
                   ", \"rows\": len(" + table + ")})\nprint(\"Saved\", len(" + table + "), \"rows to\", path)\n";
        default:
            return "";  // image instructions belong to the OpenCV backend
    }
}
