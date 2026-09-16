#include "IR.h"

#include <sstream>
#include <utility>

namespace prep {
namespace {

std::string reference(const Statement& statement) {
    if (statement.column.empty()) return statement.dataset;
    return statement.dataset + "." + statement.column;
}

std::string literalText(const Literal& literal) {
    if (literal.kind == LiteralKind::String) return "\"" + literal.value + "\"";
    return literal.value;
}

}  // namespace

IntermediateProgram IRGenerator::generate(const Program& program) const {
    IntermediateProgram result;
    for (const Statement& statement : program.statements) {
        Instruction instruction;
        instruction.line = statement.line;
        instruction.source = statement;
        instruction.operation = statementKindName(statement.kind);
        instruction.result = reference(statement);

        switch (statement.kind) {
            case StatementKind::Load:
                instruction.argument = statement.file;
                break;
            case StatementKind::Expect:
                instruction.argument = statement.contract;
                if (!statement.expectedType.empty()) instruction.argument += " " + statement.expectedType;
                if (!statement.low.empty()) instruction.argument += " " + statement.low + ".." + statement.high;
                break;
            case StatementKind::FillNull:
            case StatementKind::Filter:
                instruction.argument = statement.hasValue ? literalText(statement.value) : "";
                if (statement.kind == StatementKind::Filter) instruction.argument += " " + statement.operation;
                break;
            case StatementKind::Select:
            case StatementKind::Drop:
                for (const std::string& column : statement.columns) {
                    if (!instruction.argument.empty()) instruction.argument += ",";
                    instruction.argument += column;
                }
                break;
            case StatementKind::DropNull:
            case StatementKind::Normalize:
            case StatementKind::Save:
                instruction.argument = statement.file;
                break;
        }
        result.instructions.push_back(std::move(instruction));
    }
    return result;
}

void printIR(const IntermediateProgram& program, std::ostream& out) {
    out << "Three-address intermediate code\n";
    for (std::size_t i = 0; i < program.instructions.size(); ++i) {
        const Instruction& instruction = program.instructions[i];
        out << "  " << i + 1 << ". " << instruction.operation << " " << instruction.result;
        if (!instruction.argument.empty()) out << " <- " << instruction.argument;
        out << "  (line " << instruction.line << ")\n";
    }
}

}  // namespace prep
