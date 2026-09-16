#include "Executor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace prep {
namespace {

struct Table {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char character = line[i];
        if (character == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else {
            field += character;
        }
    }
    fields.push_back(field);
    return fields;
}

std::string csvField(const std::string& field) {
    if (field.find_first_of(",\"\n") == std::string::npos) return field;
    std::string escaped = "\"";
    for (char character : field) {
        if (character == '"') escaped += "\"\"";
        else escaped += character;
    }
    return escaped + "\"";
}

bool readCsv(const std::string& path, Table& table, std::string& problem) {
    std::ifstream input(path);
    if (!input) {
        problem = "Could not open CSV file " + quoteText(path) + ".";
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        problem = "CSV file " + quoteText(path) + " is empty.";
        return false;
    }
    table.columns = splitCsv(line);
    while (std::getline(input, line)) {
        std::vector<std::string> row = splitCsv(line);
        row.resize(table.columns.size());
        table.rows.push_back(std::move(row));
    }
    return true;
}

bool writeCsv(const std::string& path, const Table& table, std::string& problem) {
    std::ofstream output(path);
    if (!output) {
        problem = "Could not write CSV file " + quoteText(path) + ".";
        return false;
    }
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        if (i > 0) output << ',';
        output << csvField(table.columns[i]);
    }
    output << '\n';
    for (const auto& row : table.rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) output << ',';
            output << csvField(row[i]);
        }
        output << '\n';
    }
    return true;
}

int columnIndex(const Table& table, const std::string& column) {
    auto found = std::find(table.columns.begin(), table.columns.end(), column);
    if (found == table.columns.end()) return -1;
    return static_cast<int>(found - table.columns.begin());
}

bool number(const std::string& text, double& result) {
    if (text.empty()) return false;
    try {
        std::size_t used = 0;
        result = std::stod(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

std::string numberText(double value) {
    std::ostringstream out;
    out << std::setprecision(6) << value;
    return out.str();
}

bool compareValues(const std::string& left, const std::string& operation,
                   const Literal& right) {
    if (right.kind == LiteralKind::Number) {
        double leftNumber = 0.0;
        double rightNumber = 0.0;
        if (number(left, leftNumber) && number(right.value, rightNumber)) {
            if (operation == ">") return leftNumber > rightNumber;
            if (operation == "<") return leftNumber < rightNumber;
            if (operation == ">=") return leftNumber >= rightNumber;
            if (operation == "<=") return leftNumber <= rightNumber;
            if (operation == "==") return leftNumber == rightNumber;
            if (operation == "!=") return leftNumber != rightNumber;
        }
        return false;
    }

    const std::string& rightText = right.value;
    if (operation == "==") return left == rightText;
    if (operation == "!=") return left != rightText;
    if (operation == ">") return left > rightText;
    if (operation == "<") return left < rightText;
    if (operation == ">=") return left >= rightText;
    if (operation == "<=") return left <= rightText;
    return false;
}

void runtimeError(ErrorReporter& reporter, const Statement& statement,
                  const std::string& problem) {
    reporter.report({"RUNTIME ERROR", problem, statement.line, 1, "", "", ""});
}

bool checkExpectation(const Statement& statement, const Table& table, ErrorReporter& reporter) {
    int index = columnIndex(table, statement.column);
    if (index < 0) {
        runtimeError(reporter, statement, "Column " + statement.dataset + "." + statement.column + " is missing from the CSV.");
        return false;
    }

    for (const auto& row : table.rows) {
        const std::string& value = row[static_cast<std::size_t>(index)];
        if (statement.contract == "NOT_NULL" && value.empty()) {
            runtimeError(reporter, statement, "Column " + statement.column + " contains a null value.");
            return false;
        }
        if (value.empty()) continue;

        if (statement.contract == "TYPE" && statement.expectedType == "NUMBER") {
            double ignored = 0.0;
            if (!number(value, ignored)) {
                runtimeError(reporter, statement, "Column " + statement.column + " contains a non-number value.");
                return false;
            }
        } else if (statement.contract == "TYPE" && statement.expectedType == "BOOLEAN") {
            std::string normalized = upper(value);
            if (normalized != "TRUE" && normalized != "FALSE") {
                runtimeError(reporter, statement, "Column " + statement.column + " contains a non-boolean value.");
                return false;
            }
        } else if (statement.contract == "RANGE") {
            double actual = 0.0;
            double low = 0.0;
            double high = 0.0;
            if (!number(value, actual) || !number(statement.low, low) || !number(statement.high, high) ||
                actual < low || actual > high) {
                runtimeError(reporter, statement, "Column " + statement.column + " has a value outside the expected range.");
                return false;
            }
        }
    }
    return true;
}

}  // namespace

bool Executor::run(const IntermediateProgram& program, ErrorReporter& reporter,
                   std::ostream& log) const {
    std::unordered_map<std::string, Table> tables;

    for (const Instruction& instruction : program.instructions) {
        const Statement& statement = instruction.source;
        if (statement.kind == StatementKind::Load) {
            Table table;
            std::string problem;
            if (!readCsv(statement.file, table, problem)) {
                runtimeError(reporter, statement, problem);
                return false;
            }
            tables[statement.dataset] = std::move(table);
            log << "LOAD       " << statement.dataset << " ("
                << tables[statement.dataset].rows.size() << " rows)\n";
            continue;
        }

        if (statement.kind == StatementKind::Save) {
            auto found = tables.find(statement.dataset);
            if (found == tables.end()) {
                runtimeError(reporter, statement, "Dataset " + quoteText(statement.dataset) + " is not loaded.");
                return false;
            }
            std::string problem;
            if (!writeCsv(statement.file, found->second, problem)) {
                runtimeError(reporter, statement, problem);
                return false;
            }
            log << "SAVE       " << statement.file << " ("
                << found->second.rows.size() << " rows)\n";
            continue;
        }

        auto found = tables.find(statement.dataset);
        if (found == tables.end()) {
            runtimeError(reporter, statement, "Dataset " + quoteText(statement.dataset) + " is not loaded.");
            return false;
        }
        Table& table = found->second;

        if (statement.kind == StatementKind::Expect) {
            log << "EXPECT     " << statement.dataset << "." << statement.column << '\n';
            if (!checkExpectation(statement, table, reporter)) return false;
        } else if (statement.kind == StatementKind::DropNull) {
            table.rows.erase(std::remove_if(table.rows.begin(), table.rows.end(), [](const auto& row) {
                return std::any_of(row.begin(), row.end(), [](const std::string& value) { return value.empty(); });
            }), table.rows.end());
            log << "DROP_NULL  " << statement.dataset << '\n';
        } else if (statement.kind == StatementKind::FillNull) {
            int index = columnIndex(table, statement.column);
            if (index < 0) {
                runtimeError(reporter, statement, "Column " + quoteText(statement.column) + " is missing from the CSV.");
                return false;
            }
            for (auto& row : table.rows) {
                if (row[static_cast<std::size_t>(index)].empty()) row[static_cast<std::size_t>(index)] = statement.value.value;
            }
            log << "FILL_NULL  " << statement.dataset << "." << statement.column << '\n';
        } else if (statement.kind == StatementKind::Normalize) {
            int index = columnIndex(table, statement.column);
            if (index < 0) {
                runtimeError(reporter, statement, "Column " + quoteText(statement.column) + " is missing from the CSV.");
                return false;
            }
            std::vector<double> values;
            for (const auto& row : table.rows) {
                double value = 0.0;
                if (!number(row[static_cast<std::size_t>(index)], value)) {
                    runtimeError(reporter, statement, "NORMALIZE found a non-number value.");
                    return false;
                }
                values.push_back(value);
            }
            double mean = 0.0;
            for (double value : values) mean += value;
            if (!values.empty()) mean /= static_cast<double>(values.size());
            double variance = 0.0;
            for (double value : values) variance += (value - mean) * (value - mean);
            double deviation = values.empty() ? 1.0 : std::sqrt(variance / static_cast<double>(values.size()));
            if (deviation == 0.0) deviation = 1.0;
            for (auto& row : table.rows) {
                double value = 0.0;
                number(row[static_cast<std::size_t>(index)], value);
                row[static_cast<std::size_t>(index)] = numberText((value - mean) / deviation);
            }
            log << "NORMALIZE  " << statement.dataset << "." << statement.column << '\n';
        } else if (statement.kind == StatementKind::Filter) {
            int index = columnIndex(table, statement.column);
            if (index < 0) {
                runtimeError(reporter, statement, "Column " + quoteText(statement.column) + " is missing from the CSV.");
                return false;
            }
            table.rows.erase(std::remove_if(table.rows.begin(), table.rows.end(), [&](const auto& row) {
                return !compareValues(row[static_cast<std::size_t>(index)], statement.operation, statement.value);
            }), table.rows.end());
            log << "FILTER     " << statement.dataset << "." << statement.column << '\n';
        } else if (statement.kind == StatementKind::Select) {
            std::vector<int> indexes;
            for (const std::string& column : statement.columns) {
                int index = columnIndex(table, column);
                if (index < 0) {
                    runtimeError(reporter, statement, "Column " + quoteText(column) + " is missing from the CSV.");
                    return false;
                }
                indexes.push_back(index);
            }
            Table selected;
            selected.columns = statement.columns;
            for (const auto& row : table.rows) {
                std::vector<std::string> selectedRow;
                for (int index : indexes) selectedRow.push_back(row[static_cast<std::size_t>(index)]);
                selected.rows.push_back(std::move(selectedRow));
            }
            table = std::move(selected);
            log << "SELECT     " << statement.dataset << '\n';
        } else if (statement.kind == StatementKind::Drop) {
            std::vector<bool> remove(table.columns.size(), false);
            for (const std::string& column : statement.columns) {
                int index = columnIndex(table, column);
                if (index < 0) {
                    runtimeError(reporter, statement, "Column " + quoteText(column) + " is missing from the CSV.");
                    return false;
                }
                remove[static_cast<std::size_t>(index)] = true;
            }
            Table kept;
            for (std::size_t i = 0; i < table.columns.size(); ++i) {
                if (!remove[i]) kept.columns.push_back(table.columns[i]);
            }
            for (const auto& row : table.rows) {
                std::vector<std::string> keptRow;
                for (std::size_t i = 0; i < row.size(); ++i) {
                    if (!remove[i]) keptRow.push_back(row[i]);
                }
                kept.rows.push_back(std::move(keptRow));
            }
            table = std::move(kept);
            log << "DROP       " << statement.dataset << '\n';
        }
    }
    return reporter.ok();
}

}  // namespace prep
