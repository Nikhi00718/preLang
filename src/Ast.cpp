#include "Ast.h"

#include <iomanip>

namespace prep {

std::string literalKindName(LiteralKind kind) {
    switch (kind) {
        case LiteralKind::Number: return "NUMBER";
        case LiteralKind::String: return "STRING";
        case LiteralKind::Boolean: return "BOOLEAN";
    }
    return "UNKNOWN";
}

std::string statementKindName(StatementKind kind) {
    switch (kind) {
        case StatementKind::Load: return "LOAD";
        case StatementKind::Expect: return "EXPECT";
        case StatementKind::DropNull: return "DROP_NULL";
        case StatementKind::FillNull: return "FILL_NULL";
        case StatementKind::Normalize: return "NORMALIZE";
        case StatementKind::Filter: return "FILTER";
        case StatementKind::Select: return "SELECT";
        case StatementKind::Drop: return "DROP";
        case StatementKind::Save: return "SAVE";
    }
    return "UNKNOWN";
}

void printAst(const Program& program, std::ostream& out) {
    out << "Program\n";
    for (std::size_t i = 0; i < program.statements.size(); ++i) {
        const Statement& statement = program.statements[i];
        out << "  " << i + 1 << ". " << statementKindName(statement.kind)
            << " (line " << statement.line << ")\n";
        if (!statement.file.empty()) out << "     file: " << statement.file << '\n';
        if (!statement.dataset.empty()) out << "     dataset: " << statement.dataset << '\n';
        if (!statement.column.empty()) out << "     column: " << statement.column << '\n';
        if (!statement.columns.empty()) {
            out << "     columns: ";
            for (std::size_t j = 0; j < statement.columns.size(); ++j) {
                if (j > 0) out << ", ";
                out << statement.columns[j];
            }
            out << '\n';
        }
        if (!statement.contract.empty()) out << "     contract: " << statement.contract << '\n';
        if (!statement.expectedType.empty()) out << "     type: " << statement.expectedType << '\n';
        if (!statement.low.empty()) out << "     low: " << statement.low << '\n';
        if (!statement.high.empty()) out << "     high: " << statement.high << '\n';
        if (!statement.operation.empty()) out << "     operator: " << statement.operation << '\n';
        if (statement.hasValue) {
            out << "     value: " << statement.value.value
                << " (" << literalKindName(statement.value.kind) << ")\n";
        }
    }
}

}  // namespace prep
