#include "CodeGenerator.h"

#include <sstream>

namespace prep {
namespace {

std::string quote(const std::string& value) {
    return "\"" + value + "\"";
}

std::string valueText(const Statement& statement) {
    if (!statement.hasValue) return "";
    return statement.value.kind == LiteralKind::String ? quote(statement.value.value) : statement.value.value;
}

std::string columnsText(const Statement& statement) {
    std::string result = "[";
    for (std::size_t i = 0; i < statement.columns.size(); ++i) {
        if (i > 0) result += ", ";
        result += quote(statement.columns[i]);
    }
    return result + "]";
}

}  // namespace

std::string CodeGenerator::generate(const IntermediateProgram& program) const {
    std::ostringstream out;
    out << "// Generated C++-style execution plan\n";
    for (const Instruction& instruction : program.instructions) {
        const Statement& statement = instruction.source;
        switch (statement.kind) {
            case StatementKind::Load:
                out << "auto " << statement.dataset << " = loadCsv(" << quote(statement.file) << ");\n";
                break;
            case StatementKind::Expect:
                out << "expect(" << statement.dataset << ", " << quote(statement.column)
                    << ", " << quote(statement.contract) << ");\n";
                break;
            case StatementKind::DropNull:
                out << "dropNull(" << statement.dataset << ");\n";
                break;
            case StatementKind::FillNull:
                out << "fillNull(" << statement.dataset << ", " << quote(statement.column)
                    << ", " << valueText(statement) << ");\n";
                break;
            case StatementKind::Normalize:
                out << "normalize(" << statement.dataset << ", " << quote(statement.column) << ");\n";
                break;
            case StatementKind::Filter:
                out << "filter(" << statement.dataset << ", " << quote(statement.column)
                    << ", " << quote(statement.operation) << ", " << valueText(statement) << ");\n";
                break;
            case StatementKind::Select:
                out << "select(" << statement.dataset << ", " << columnsText(statement) << ");\n";
                break;
            case StatementKind::Drop:
                out << "drop(" << statement.dataset << ", " << columnsText(statement) << ");\n";
                break;
            case StatementKind::Save:
                out << "saveCsv(" << statement.dataset << ", " << quote(statement.file) << ");\n";
                break;
        }
    }
    return out.str();
}

}  // namespace prep
