#include "schema/ReleasedFacts.h"

#include "schema/SchemaProvider.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Both files are text with one record per line and tab-separated fields, so that
// column names with spaces or commas need no quoting.
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t')) {
        out.push_back(field);
    }
    return out;
}

std::string exactText(double value) {
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    char text[32];
    const auto end = std::to_chars(text, text + sizeof text, value).ptr;  // shortest text that reads back exactly
    return std::string(text, end);
}

bool parseNumber(const std::string& text, double& value) {
    if (text == "inf" || text == "-inf") {
        value = (text[0] == '-' ? -1 : 1) * std::numeric_limits<double>::infinity();
        return true;
    }
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && *end == '\0';
}

bool parseType(const std::string& text, ColumnType& type) {
    for (ColumnType candidate : {ColumnType::Boolean, ColumnType::Integer, ColumnType::Float, ColumnType::String,
                                 ColumnType::DateTime}) {
        if (text == columnTypeName(candidate)) {
            type = candidate;
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<ReleasedFacts> readReleasedFacts(const fs::path& file, const fs::path& programFolder,
                                               std::string& problem) {
    std::ifstream in(file);
    if (!in) {
        problem = "cannot open the facts file '" + file.generic_string() + "'";
        return std::nullopt;
    }
    ReleasedFacts facts;
    TableSchema* table = nullptr;
    std::string line;
    for (int number = 1; std::getline(in, line); ++number) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> f = fields(line);
        const std::string where = "line " + std::to_string(number) + " of '" + file.generic_string() + "'";
        if (number == 1) {
            if (f.size() != 2 || f[0] != "preplang-facts" || f[1] != "1") {
                problem = where + " is not the header of a facts file written by tools/release_facts.py";
                return std::nullopt;
            }
        } else if (f.empty()) {
            continue;
        } else if (f[0] == "mechanism" && f.size() == 2) {
            facts.mechanism = f[1];
        } else if (f[0] == "table" && f.size() == 4) {
            TableSchema schema;
            schema.name = f[2];
            schema.file = (programFolder / f[1]).lexically_normal().generic_string();  // as LOAD resolves it
            schema.format = f[3] == "parquet" ? TableFormat::Parquet : TableFormat::Csv;
            schema.valuesKnown = false;  // the answers describe the data only approximately
            schema.released = true;
            table = &(facts.tables[SchemaProvider::overrideKey(schema.file, schema.name)] = schema);
        } else if (f[0] == "column" && f.size() == 12 && table) {
            // column, name, type, may have missing values, lower bound, strict, upper bound, strict,
            // and where each bound's question sequence stopped: k and "<" or "<=" (values below k
            // were seen), k and ">" or ">=" (values above k), or "-" and "-" if it never stopped
            ColumnInfo column;
            column.name = f[1];
            double low = 0, high = 0;
            if (!parseType(f[2], column.type) || (f[3] != "0" && f[3] != "1" && f[3] != "?") ||
                !parseNumber(f[4], low) || !parseNumber(f[6], high)) {
                problem = where + " is not a column of a facts file";
                return std::nullopt;
            }
            // Missing values: none (0), seen (1), or unknown (?). Both of the last two are an
            // unknown count to the analysis, as in a weakened certificate.
            column.missingCount = 0;
            column.missingKnown = f[3] == "0";
            column.seenMissing = f[3] == "1";
            if (isNumeric(column.type)) {
                const double infinity = std::numeric_limits<double>::infinity();
                // An open bound (> k) is given to the analysis as the next double past k,
                // as the certifier does.
                column.min = f[5] == "1" ? std::nextafter(low, infinity) : low;
                column.max = f[7] == "1" ? std::nextafter(high, -infinity) : high;
                column.hasRange = !std::isinf(column.min) || !std::isinf(column.max);
                column.boundsExact = false;
                double below = 0, above = 0;
                if (f[9] != "-" && parseNumber(f[8], below)) {
                    column.seenBelow = below;
                    column.seenBelowOrAt = f[9] == "<=";
                }
                if (f[11] != "-" && parseNumber(f[10], above)) {
                    column.seenAbove = above;
                    column.seenAboveOrAt = f[11] == ">=";
                }
            }
            table->columns.push_back(column);
        } else {
            problem = where + " is not a line of a facts file";
            return std::nullopt;
        }
    }
    if (facts.tables.empty()) {
        problem = "the facts file '" + file.generic_string() + "' describes no table";
        return std::nullopt;
    }
    return facts;
}

void writeQueries(std::ostream& out, const std::vector<QueryTable>& tables, const std::vector<double>& constants) {
    out << "preplang-queries\t1\nconstants";
    for (double constant : constants) {
        out << '\t' << exactText(constant);
    }
    out << '\n';
    for (const QueryTable& table : tables) {
        out << "table\t" << table.loadPath << '\t' << table.name << '\t'
            << (table.schema.format == TableFormat::Parquet ? "parquet" : "csv") << '\t'
            << fs::absolute(table.schema.file).generic_string() << '\n';
        for (const ColumnInfo& column : table.schema.columns) {
            out << "column\t" << column.name << '\t' << columnTypeName(column.type) << '\t'
                << (table.ranged.count(column.name) != 0 ? "range" : "missing") << '\n';
        }
    }
}
