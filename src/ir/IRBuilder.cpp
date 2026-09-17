#include "ir/IRBuilder.h"

#include <iomanip>
#include <utility>

namespace {

std::string temp(int number) {
    return "t" + std::to_string(number);
}

std::string literalText(const Literal& literal) {
    return literal.kind == LiteralKind::String ? "\"" + literal.value + "\"" : literal.value;
}

std::string listText(const std::vector<std::string>& names) {
    std::string text = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        text += (i > 0 ? ", " : "") + names[i];
    }
    return text + "]";
}

}  // namespace

IRBuilder::IRBuilder(const SymbolTable& symbols, const AnalysisResult& analysis)
    : symbols_(symbols), analysis_(analysis) {}

IRProgram IRBuilder::build(const Program& program) {
    for (const auto& statement : program.statements) {
        statement_ = statement.get();
        statement->accept(*this);
    }
    return std::move(program_);
}

// Adds an instruction, tagged with its source line and with the analysis
// result if its statement was proven to have no effect.
void IRBuilder::emit(Instruction instruction) {
    instruction.line = statement_->location.line;
    const auto found = analysis_.noEffect.find(statement_);
    if (found != analysis_.noEffect.end() && instruction.result == 0) {
        instruction.noEffect = found->second;
    }
    program_.code.push_back(std::move(instruction));
}

// ---------- Statements ----------

void IRBuilder::visit(const LoadStmt& node) {
    const Schema& schema = symbols_.lookup(node.dataset)->schema;
    Instruction instruction(Opcode::Load);
    instruction.dataset = node.dataset;
    instruction.text = node.path;
    for (const ColumnInfo& column : schema.columns) {
        instruction.columns.push_back(column.name);
    }
    program_.inputs[node.dataset] = schema;
    emit(std::move(instruction));
}

void IRBuilder::visit(const DropNullStmt& node) {
    Instruction instruction(Opcode::DropNull);
    instruction.dataset = node.dataset;
    emit(std::move(instruction));
}

void IRBuilder::visit(const FillNullStmt& node) {
    Instruction instruction(Opcode::FillNull);
    instruction.dataset = node.target.dataset;
    instruction.column = node.target.column;
    instruction.values = {node.value};
    emit(std::move(instruction));
}

void IRBuilder::visit(const NormalizeStmt& node) {
    Instruction instruction(Opcode::Normalize);
    instruction.dataset = node.target.dataset;
    instruction.column = node.target.column;
    instruction.text = node.method == NormalizeMethod::ZScore ? "ZSCORE" : "MINMAX";
    emit(std::move(instruction));
}

// The condition is emitted first as three-address code; the FILTER then
// uses the temporary holding the final result.
void IRBuilder::visit(const FilterStmt& node) {
    filterDataset_ = node.dataset;
    node.condition->accept(*this);

    Instruction instruction(Opcode::Filter);
    instruction.dataset = node.dataset;
    instruction.uses = {lastTemp_};
    emit(std::move(instruction));
}

void IRBuilder::visit(const SelectStmt& node) {
    Instruction instruction(Opcode::Select);
    instruction.dataset = node.dataset;
    instruction.columns = node.columns;
    emit(std::move(instruction));
}

void IRBuilder::visit(const DropStmt& node) {
    Instruction instruction(Opcode::Drop);
    instruction.dataset = node.dataset;
    instruction.columns = node.columns;
    emit(std::move(instruction));
}

void IRBuilder::visit(const SaveStmt& node) {
    Instruction instruction(Opcode::Save);
    instruction.dataset = node.dataset;
    instruction.text = node.path;
    emit(std::move(instruction));
}

// Type contracts (NUMBER, STRING, BOOLEAN) were already decided by the type
// checker, so only data contracts become run-time CHECK instructions.
void IRBuilder::visit(const ExpectStmt& node) {
    if (node.constraint != ConstraintKind::NotNull && node.constraint != ConstraintKind::Between) {
        return;
    }
    Instruction instruction(Opcode::Check);
    instruction.dataset = node.target.dataset;
    instruction.column = node.target.column;
    if (node.constraint == ConstraintKind::NotNull) {
        instruction.text = "NOT_NULL";
    } else {
        instruction.text = "BETWEEN";
        instruction.values = {{LiteralKind::Number, node.low}, {LiteralKind::Number, node.high}};
    }
    emit(std::move(instruction));
}

// ---------- Conditions ----------

void IRBuilder::visit(const ComparisonExpr& node) {
    Instruction instruction(Opcode::Compare);
    instruction.dataset = filterDataset_;
    instruction.column = node.column;
    instruction.text = node.op;
    instruction.values = {node.value};
    instruction.result = nextTemp_++;
    lastTemp_ = instruction.result;
    emit(std::move(instruction));
}

void IRBuilder::visit(const LogicalExpr& node) {
    node.left->accept(*this);
    const int left = lastTemp_;
    node.right->accept(*this);
    const int right = lastTemp_;

    Instruction instruction(node.op == LogicalOp::And ? Opcode::And : Opcode::Or);
    instruction.uses = {left, right};
    instruction.result = nextTemp_++;
    lastTemp_ = instruction.result;
    emit(std::move(instruction));
}

void IRBuilder::visit(const NotExpr& node) {
    node.operand->accept(*this);
    Instruction instruction(Opcode::Not);
    instruction.uses = {lastTemp_};
    instruction.result = nextTemp_++;
    lastTemp_ = instruction.result;
    emit(std::move(instruction));
}

// ---------- Printing ----------

std::string instructionText(const Instruction& in) {
    const std::string target = in.dataset + "." + in.column;
    switch (in.op) {
        case Opcode::Load:
            return "LOAD " + in.dataset + " <- \"" + in.text + "\" " + listText(in.columns);
        case Opcode::Check:
            return "CHECK " + target + " " + in.text +
                   (in.values.empty() ? "" : " " + in.values[0].value + " AND " + in.values[1].value);
        case Opcode::FillNull:
            return "FILL_NULL " + target + " WITH " + literalText(in.values[0]);
        case Opcode::DropNull:
            return "DROP_NULL " + in.dataset;
        case Opcode::Normalize:
            return "NORMALIZE " + target + " " + in.text;
        case Opcode::Compare:
            return temp(in.result) + " = " + target + " " + in.text + " " + literalText(in.values[0]);
        case Opcode::And:
            return temp(in.result) + " = " + temp(in.uses[0]) + " AND " + temp(in.uses[1]);
        case Opcode::Or:
            return temp(in.result) + " = " + temp(in.uses[0]) + " OR " + temp(in.uses[1]);
        case Opcode::Not:
            return temp(in.result) + " = NOT " + temp(in.uses[0]);
        case Opcode::Filter:
            return "FILTER " + in.dataset + " IF " + temp(in.uses[0]);
        case Opcode::Select:
            return "SELECT " + in.dataset + " " + listText(in.columns);
        case Opcode::Drop:
            return "DROP " + in.dataset + " " + listText(in.columns);
        case Opcode::Save:
            return "SAVE " + in.dataset + " -> \"" + in.text + "\"";
    }
    return "";
}

void printIR(std::ostream& out, const IRProgram& program) {
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& instruction = program.code[i];
        out << std::right << std::setw(3) << i + 1 << "  " << std::left << std::setw(62)
            << instructionText(instruction) << "  ; line " << instruction.line << '\n';
    }
}
