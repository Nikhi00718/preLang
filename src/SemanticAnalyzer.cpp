#include "SemanticAnalyzer.h"

namespace prep {

SemanticAnalyzer::SemanticAnalyzer(ErrorReporter& reporter) : reporter_(reporter) {}

void SemanticAnalyzer::error(const Statement& statement, const std::string& problem,
                             const std::string& hint) {
    reporter_.report({"SEMANTIC ERROR", problem, statement.line, 1, "", "", hint});
}

bool SemanticAnalyzer::loaded(const Statement& statement, const SymbolTable& symbols) const {
    return symbols.findDataset(statement.dataset) != nullptr;
}

const ColumnSymbol* SemanticAnalyzer::requireColumn(const Statement& statement,
                                                    const SymbolTable& symbols,
                                                    const std::string& use) {
    if (!loaded(statement, symbols)) {
        error(statement,
              "Dataset " + quoteText(statement.dataset) + " is used before LOAD.",
              "Load the dataset before " + use + ".");
        return nullptr;
    }

    const ColumnSymbol* column = symbols.findColumn(statement.dataset, statement.column);
    if (column == nullptr) {
        error(statement,
              "Column " + statement.dataset + "." + statement.column + " is not declared.",
              "Add EXPECT " + statement.dataset + "." + statement.column + " <type>; first.");
    }
    return column;
}

bool SemanticAnalyzer::analyze(const Program& program, SymbolTable& symbols) {
    for (const Statement& statement : program.statements) {
        switch (statement.kind) {
            case StatementKind::Load:
                if (!symbols.addDataset(statement.dataset, statement.file)) {
                    error(statement, "Dataset " + quoteText(statement.dataset) + " is loaded twice.");
                }
                break;

            case StatementKind::Expect: {
                if (!loaded(statement, symbols)) {
                    error(statement, "Dataset " + quoteText(statement.dataset) + " is used before LOAD.");
                    break;
                }
                ValueType type = ValueType::Unknown;
                bool notNull = false;
                if (statement.contract == "TYPE") type = valueTypeFromName(statement.expectedType);
                if (statement.contract == "RANGE") type = ValueType::Number;
                if (statement.contract == "NOT_NULL") notNull = true;
                std::string problem;
                if (!symbols.declareColumn(statement.dataset, statement.column, type, notNull, problem)) {
                    error(statement, problem);
                }
                break;
            }

            case StatementKind::DropNull:
            case StatementKind::Save:
                if (!loaded(statement, symbols)) {
                    error(statement, "Dataset " + quoteText(statement.dataset) + " is used before LOAD.");
                }
                break;

            case StatementKind::FillNull: {
                const ColumnSymbol* column = requireColumn(statement, symbols, "FILL_NULL");
                if (column != nullptr && column->type != ValueType::Unknown &&
                    column->type != valueTypeFromLiteral(statement.value)) {
                    error(statement, "The FILL_NULL value does not match the column type.",
                          "Use a " + valueTypeName(column->type) + " value.");
                }
                break;
            }

            case StatementKind::Normalize: {
                const ColumnSymbol* column = requireColumn(statement, symbols, "NORMALIZE");
                if (column != nullptr && column->type != ValueType::Number) {
                    error(statement, "NORMALIZE requires a NUMBER column.",
                          "Use NORMALIZE only with a numeric column.");
                }
                break;
            }

            case StatementKind::Filter: {
                const ColumnSymbol* column = requireColumn(statement, symbols, "FILTER");
                if (column != nullptr && column->type != ValueType::Unknown &&
                    column->type != valueTypeFromLiteral(statement.value)) {
                    error(statement, "The FILTER value does not match the column type.");
                }
                break;
            }

            case StatementKind::Select:
            case StatementKind::Drop:
                if (!loaded(statement, symbols)) {
                    error(statement, "Dataset " + quoteText(statement.dataset) + " is used before LOAD.");
                    break;
                }
                for (const std::string& columnName : statement.columns) {
                    if (symbols.findColumn(statement.dataset, columnName) == nullptr) {
                        error(statement,
                              "Column " + statement.dataset + "." + columnName + " is not declared.",
                              "Declare the column with EXPECT before using it.");
                    }
                }
                break;
        }
    }
    return reporter_.ok();
}

}  // namespace prep
