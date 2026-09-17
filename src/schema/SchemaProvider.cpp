#include "schema/SchemaProvider.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <vector>

namespace {

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// Splits one CSV line into trimmed fields. A field inside double quotes may
// contain commas, and "" inside quotes stands for a single quote character.
std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                inQuotes = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trim(field));
    return fields;
}

// The most specific type for one non-empty cell. Numbers use the same rule
// as PrepLang number literals: optional '-', digits, optional '.' and digits.
ColumnType typeOfCell(const std::string& cell) {
    const std::string lower = toLower(cell);
    if (lower == "true" || lower == "false") {
        return ColumnType::Boolean;
    }

    size_t i = (cell[0] == '-') ? 1 : 0;
    const size_t digitsStart = i;
    while (i < cell.size() && isDigit(cell[i])) {
        ++i;
    }
    if (i == digitsStart) {
        return ColumnType::String;
    }
    if (i == cell.size()) {
        return ColumnType::Integer;
    }
    if (cell[i] != '.') {
        return ColumnType::String;
    }
    const size_t fractionStart = ++i;
    while (i < cell.size() && isDigit(cell[i])) {
        ++i;
    }
    return (i > fractionStart && i == cell.size()) ? ColumnType::Float : ColumnType::String;
}

// Combines the type seen so far in a column with the type of a new cell.
// INTEGER and FLOAT give FLOAT; any other mix can only be STRING.
ColumnType combine(ColumnType current, ColumnType next) {
    if (current == next) {
        return current;
    }
    const bool bothNumeric = (current == ColumnType::Integer || current == ColumnType::Float) &&
                             (next == ColumnType::Integer || next == ColumnType::Float);
    return bothNumeric ? ColumnType::Float : ColumnType::String;
}

// FNV-1a hash of the file's bytes. The generated Python computes the same
// hash, so it can refuse to run if the CSV changed after compilation.
std::uint64_t fingerprintOf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
        hash ^= static_cast<unsigned char>(*it);
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

}  // namespace

SchemaProvider::SchemaProvider(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

std::optional<Schema> SchemaProvider::infer(const std::string& csvPath, SourceLocation loadLocation) {
    std::ifstream file(csvPath);
    if (!file) {
        diagnostics_.error(Phase::Schema, loadLocation, "cannot open CSV file '" + csvPath + "'",
                           "the path is relative to the folder of the .prep file");
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(file, line)) {
        diagnostics_.error(Phase::Schema, loadLocation, "CSV file '" + csvPath + "' is empty",
                           "the first line must list the column names");
        return std::nullopt;
    }
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {  // Excel may add a UTF-8 byte-order mark
        line.erase(0, 3);
    }

    Schema schema;
    schema.file = csvPath;
    schema.fingerprint = fingerprintOf(csvPath);
    for (const std::string& name : splitCsvLine(line)) {
        ColumnInfo column;
        column.name = name;
        schema.columns.push_back(column);
    }

    // Type of each column so far. Stays empty until the column's first value.
    std::vector<std::optional<ColumnType>> inferred(schema.columns.size());
    // Whether the column has a value that is not plain text (a number or boolean).
    std::vector<bool> hasNonText(schema.columns.size(), false);
    int lineNumber = 1;

    while (std::getline(file, line)) {
        ++lineNumber;
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> cells = splitCsvLine(line);
        if (cells.size() != schema.columns.size()) {
            diagnostics_.error(Phase::Schema, loadLocation,
                               "line " + std::to_string(lineNumber) + " of '" + csvPath + "' has " +
                                   std::to_string(cells.size()) + " values but the header has " +
                                   std::to_string(schema.columns.size()) + " columns");
            return std::nullopt;
        }
        ++schema.rowCount;

        for (size_t i = 0; i < cells.size(); ++i) {
            ColumnInfo& column = schema.columns[i];
            if (cells[i].empty()) {
                ++column.missingCount;
                continue;
            }
            const ColumnType type = typeOfCell(cells[i]);
            inferred[i] = inferred[i] ? combine(*inferred[i], type) : type;
            if (type != ColumnType::String) {
                hasNonText[i] = true;
            }
            if (*inferred[i] == ColumnType::String && column.stringReason.empty()) {
                column.stringReason = "line " + std::to_string(lineNumber) + " has \"" + cells[i] + "\"";
            }

            if (type == ColumnType::Integer || type == ColumnType::Float) {
                const double value = std::stod(cells[i]);
                column.min = column.hasRange ? std::min(column.min, value) : value;
                column.max = column.hasRange ? std::max(column.max, value) : value;
                column.hasRange = true;
            }
        }
    }

    for (size_t i = 0; i < schema.columns.size(); ++i) {
        ColumnInfo& column = schema.columns[i];
        column.type = inferred[i].value_or(ColumnType::String);  // no values at all: STRING
        if (!hasNonText[i]) {
            column.stringReason.clear();  // an ordinary text column needs no explanation
        }
    }
    return schema;
}

void printSchema(std::ostream& out, const std::string& dataset, const Schema& schema) {
    out << "Dataset '" << dataset << "' from " << schema.file << " (" << schema.rowCount << " rows, "
        << schema.columns.size() << " columns, fingerprint " << std::hex << std::showbase << schema.fingerprint
        << std::dec << std::noshowbase << ")\n";

    size_t nameWidth = 8;
    for (const ColumnInfo& column : schema.columns) {
        nameWidth = std::max(nameWidth, column.name.size() + 2);
    }
    const int width = static_cast<int>(nameWidth);

    out << std::left << std::setw(width) << "COLUMN" << std::setw(10) << "TYPE" << std::setw(10) << "NULLABLE"
        << std::setw(9) << "MISSING" << "RANGE\n";
    for (const ColumnInfo& column : schema.columns) {
        const bool numeric = column.type == ColumnType::Integer || column.type == ColumnType::Float;
        const std::string range =
            numeric && column.hasRange ? "[" + formatNumber(column.min) + ", " + formatNumber(column.max) + "]" : "-";
        out << std::setw(width) << column.name << std::setw(10) << columnTypeName(column.type) << std::setw(10)
            << (column.missingCount > 0 ? "yes" : "no") << std::setw(9) << column.missingCount << range << '\n';
    }
}
