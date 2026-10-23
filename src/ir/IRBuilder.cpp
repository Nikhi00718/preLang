#include "ir/IRBuilder.h"

#include <filesystem>
#include <iomanip>
#include <map>
#include <utility>

namespace {

std::string temp(int number) { return "t" + std::to_string(number); }

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

// " IF CHANGED students" for a check that runs only when an input changed
std::string changedText(const Instruction& in) {
    if (!in.onlyIfInputChanged) {
        return "";
    }
    std::string text = " IF CHANGED";
    for (const std::string& table : in.provenFromValuesOf) {
        text += " " + table;
    }
    return text;
}

Literal number(const std::string& value) { return {LiteralKind::Number, value}; }

// A file inside a folder path written in the program, e.g. "company/" + "staff.csv".
std::string inFolder(const std::string& folder, const std::string& file) {
    return (std::filesystem::path(folder) / file).generic_string();
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

const std::vector<Target>& IRBuilder::targets() const {
    static const std::vector<Target> none;
    const auto found = analysis_.targets.find(statement_);
    return found == analysis_.targets.end() ? none : found->second;
}

Instruction IRBuilder::make(Opcode op, const Target& target) const {
    Instruction instruction(op);
    instruction.dataset = target.dataset;
    instruction.table = target.table;
    instruction.column = target.column;
    instruction.line = statement_->location.line;
    const auto found = analysis_.noEffect.find({statement_, targetKey(target)});
    if (found != analysis_.noEffect.end()) {
        instruction.noEffect = found->second;
        instruction.proofKey = proofKey(*statement_, targetKey(target));
    }
    const auto values = analysis_.valueProofs.find({statement_, targetKey(target)});
    if (values != analysis_.valueProofs.end()) {
        instruction.provenFromValuesOf = values->second;
    }
    return instruction;
}

void IRBuilder::emit(Instruction instruction) { program_.code.push_back(std::move(instruction)); }

// ---------- Table statements ----------

// A TABLESET becomes one LOAD per CSV file; it reads every column of the file
// until the optimizer prunes the ones that are never used.
void IRBuilder::visit(const LoadStmt& node) {
    const DatasetSymbol& dataset = *symbols_.lookup(node.dataset);
    if (dataset.isImage()) {
        Instruction instruction = make(Opcode::LoadImages, {dataset.name, "", "", {}});
        instruction.text = node.path;
        program_.images[dataset.name] = dataset.imageSchema;
        emit(std::move(instruction));
        return;
    }
    for (const TableState& table : dataset.tables) {
        const bool inSet = dataset.kind == DatasetKind::TableSet;
        Instruction instruction = make(Opcode::Load, {dataset.name, inSet ? table.name : "", "", {}});
        instruction.text =
            inSet ? inFolder(node.path, std::filesystem::path(table.schema.file).filename().u8string()) : node.path;
        for (const ColumnInfo& column : table.schema.columns) {
            instruction.columns.push_back(column.name);
        }
        program_.tables[tableKey(instruction.dataset, instruction.table)] = table.schema;
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const DropNullStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::DropNull, target);
        instruction.columns = node.columns;
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const FillNullStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::FillNull, target);
        if (node.method == FillMethod::Value) {
            instruction.values = {node.value};
        } else {
            instruction.text = fillMethodName(node.method);
        }
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const NormalizeStmt& node) {
    for (const Target& target : targets()) {
        if (node.method == NormalizeMethod::Range) {
            Instruction instruction = make(Opcode::NormalizeImages, target);
            instruction.values = {number(node.low), number(node.high)};
            for (const std::string& arg : target.args) {
                instruction.values.push_back(number(arg));
            }
            emit(std::move(instruction));
        } else {
            Instruction instruction = make(Opcode::Normalize, target);
            instruction.text = node.method == NormalizeMethod::ZScore ? "ZSCORE" : "MINMAX";
            emit(std::move(instruction));
        }
    }
}

// The condition is emitted first as three-address code; the FILTER then uses
// the temporary that holds the final result.
void IRBuilder::visit(const FilterStmt& node) {
    filterTarget_ = targets().front();
    node.condition->accept(*this);
    Instruction instruction = make(Opcode::Filter, filterTarget_);
    instruction.uses = {lastTemp_};
    emit(std::move(instruction));
}

void IRBuilder::visit(const SelectStmt& node) {
    Instruction instruction = make(Opcode::Select, targets().front());
    instruction.columns = node.columns;
    emit(std::move(instruction));
}

// Analysis gives one target per dropped column; columns of the same table
// are grouped back into one DROP.
void IRBuilder::visit(const DropStmt&) {
    const size_t first = program_.code.size();
    for (const Target& target : targets()) {
        Instruction* last = program_.code.size() > first ? &program_.code.back() : nullptr;
        if (last && last->dataset == target.dataset && last->table == target.table) {
            last->columns.push_back(target.column);
            continue;
        }
        Instruction instruction = make(Opcode::Drop, {target.dataset, target.table, "", {}});
        instruction.columns = {target.column};
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const ParseDateStmt&) {
    for (const Target& target : targets()) {
        emit(make(Opcode::ParseDate, target));
    }
}

void IRBuilder::visit(const DeriveStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Derive, target);
        instruction.columns = valueColumns(*node.value);
        instruction.text = valueText(*node.value);
        instruction.value = node.value;
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const JoinStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Join, target);
        instruction.text = node.many ? "MANY" : "";
        instruction.withDataset = target.args[0];
        instruction.withTable = target.args[1];
        instruction.columns.assign(target.args.begin() + 2, target.args.end());
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const AggregateStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Aggregate, target);
        instruction.columns = node.by;
        for (size_t i = 0; i < node.items.size(); ++i) {
            const AggregateItem& item = node.items[i];
            instruction.aggregates.push_back({item.function, item.column, item.name, target.args[i]});
        }
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const PlotStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Plot, target);
        instruction.column = node.kind;
        instruction.columns = target.args;
        instruction.numbers = {node.bins};
        instruction.text = node.path;
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const ReportStmt& node) {
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Report, target);
        instruction.columns = target.args;
        instruction.text = node.path;
        emit(std::move(instruction));
    }
}

void IRBuilder::visit(const SaveStmt& node) {
    for (const Target& target : targets()) {
        if (node.kind == DataKind::Image || node.kind == DataKind::ImageSet) {
            Instruction instruction = make(Opcode::SaveImages, target);
            instruction.text = node.path;
            instruction.numbers = {node.kind == DataKind::Image ? 1 : 0};
            emit(std::move(instruction));
        } else {
            Instruction instruction = make(Opcode::Save, target);
            instruction.text = node.kind == DataKind::TableSet ? inFolder(node.path, target.table + ".csv") : node.path;
            emit(std::move(instruction));
        }
    }
}

// Only contracts about values can need a run-time CHECK. Types, schemas and
// image shapes are always decided at compile time.
void IRBuilder::visit(const ExpectStmt& node) {
    static const std::map<ConstraintKind, std::string> runtimeChecks = {
        {ConstraintKind::NotNull, "NOT_NULL"},     {ConstraintKind::Between, "BETWEEN"},
        {ConstraintKind::Unique, "UNIQUE"},        {ConstraintKind::References, "REFERENCES"},
        {ConstraintKind::MatchRate, "MATCH_RATE"},
    };
    const auto check = runtimeChecks.find(node.constraint);
    if (check == runtimeChecks.end()) {
        return;
    }
    for (const Target& target : targets()) {
        Instruction instruction = make(Opcode::Check, target);
        instruction.text = check->second;
        instruction.columns = target.args;
        if (node.constraint == ConstraintKind::Between) {
            instruction.values = {number(node.low), number(node.high)};
        } else if (node.constraint == ConstraintKind::MatchRate) {
            instruction.values = {number(node.low)};
        }
        emit(std::move(instruction));
    }
}

// ---------- Image statements ----------

void IRBuilder::visit(const ResizeStmt& node) {
    Instruction instruction = make(Opcode::Resize, targets().front());
    instruction.numbers = {node.width, node.height, node.keepAspect ? 1 : 0};
    emit(std::move(instruction));
}

void IRBuilder::visit(const GrayscaleStmt&) { emit(make(Opcode::Grayscale, targets().front())); }

void IRBuilder::visit(const CompressStmt& node) {
    Instruction instruction = make(Opcode::Compress, targets().front());
    instruction.numbers = {node.quality};
    emit(std::move(instruction));
}

void IRBuilder::visit(const ConvertStmt& node) {
    Instruction instruction = make(Opcode::Convert, targets().front());
    instruction.text = node.format;
    emit(std::move(instruction));
}

void IRBuilder::visit(const RemoveCorruptStmt&) { emit(make(Opcode::RemoveCorrupt, targets().front())); }

// LINK is fully decided at compile time; only a deferred MATCH_RATE contract
// needs run-time work, and it has its own CHECK.
void IRBuilder::visit(const LinkStmt&) {}

// ---------- Conditions ----------

void IRBuilder::visit(const ComparisonExpr& node) {
    Instruction instruction(Opcode::Compare);
    instruction.dataset = filterTarget_.dataset;
    instruction.table = filterTarget_.table;
    instruction.column = node.column;
    instruction.text = node.op;
    instruction.values = {node.value};
    instruction.result = nextTemp_++;
    instruction.line = statement_->location.line;
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
    instruction.line = statement_->location.line;
    lastTemp_ = instruction.result;
    emit(std::move(instruction));
}

void IRBuilder::visit(const NotExpr& node) {
    node.operand->accept(*this);
    Instruction instruction(Opcode::Not);
    instruction.uses = {lastTemp_};
    instruction.result = nextTemp_++;
    instruction.line = statement_->location.line;
    lastTemp_ = instruction.result;
    emit(std::move(instruction));
}

// ---------- Printing ----------

std::string instructionText(const Instruction& in) {
    const std::string table = tableKey(in.dataset, in.table);
    const std::string target = table + "." + in.column;
    switch (in.op) {
        case Opcode::Load: return "LOAD " + table + " <- \"" + in.text + "\" " + listText(in.columns);
        case Opcode::Check:
            if (in.text == "MATCH_RATE") {
                return "CHECK " + in.dataset + " MATCH_RATE " + in.values[0].value + "%" + changedText(in);
            }
            if (in.text == "REFERENCES") {
                return "CHECK " + target + " REFERENCES " + tableKey(in.columns[0], in.columns[1]) + "." +
                       in.columns[2] + changedText(in);
            }
            return "CHECK " + target + " " + in.text +
                   (in.values.empty() ? "" : " " + in.values[0].value + " AND " + in.values[1].value) + changedText(in);
        case Opcode::FillNull:
            return "FILL_NULL " + target + " WITH " + (in.values.empty() ? in.text : literalText(in.values[0]));
        case Opcode::DropNull:  return "DROP_NULL " + table + (in.columns.empty() ? "" : " " + listText(in.columns));
        case Opcode::Normalize: return "NORMALIZE " + target + " " + in.text;
        case Opcode::Compare:   return temp(in.result) + " = " + target + " " + in.text + " " + literalText(in.values[0]);
        case Opcode::And:       return temp(in.result) + " = " + temp(in.uses[0]) + " AND " + temp(in.uses[1]);
        case Opcode::Or:        return temp(in.result) + " = " + temp(in.uses[0]) + " OR " + temp(in.uses[1]);
        case Opcode::Not:       return temp(in.result) + " = NOT " + temp(in.uses[0]);
        case Opcode::Filter:    return "FILTER " + table + " IF " + temp(in.uses[0]);
        case Opcode::Select:    return "SELECT " + table + " " + listText(in.columns);
        case Opcode::Drop:      return "DROP " + table + " " + listText(in.columns);
        case Opcode::Save:      return "SAVE " + table + " -> \"" + in.text + "\"";
        case Opcode::Plot:
            return "PLOT " + table + " " + in.column + " " + listText(in.columns) +
                   (in.numbers.empty() || in.numbers[0] == 0 ? "" : " BINS " + std::to_string(in.numbers[0])) +
                   " -> \"" + in.text + "\"";
        case Opcode::Report:    return "REPORT " + table + " -> \"" + in.text + "\"";
        case Opcode::ParseDate: return "PARSE_DATE " + target;
        case Opcode::Derive:    return "DERIVE " + target + " = " + in.text;
        case Opcode::Join:
            return "JOIN " + table + " WITH " + tableKey(in.withDataset, in.withTable) + " ON " + in.column +
                   (in.text == "MANY" ? " MANY" : "") + " ADD " + listText(in.columns);
        case Opcode::Aggregate: {
            std::string text = "AGGREGATE " + table + " BY " + listText(in.columns);
            for (size_t i = 0; i < in.aggregates.size(); ++i) {
                const AggregateResult& item = in.aggregates[i];
                text += (i > 0 ? ", " : " ") + item.function + (item.column.empty() ? "" : " " + item.column) + " AS " +
                        item.name;
            }
            return text;
        }
        case Opcode::LoadImages: return "LOAD_IMAGES " + in.dataset + " <- \"" + in.text + "\"";
        case Opcode::Resize:
            return "RESIZE " + in.dataset + " TO " + std::to_string(in.numbers[0]) + " " +
                   std::to_string(in.numbers[1]) + (in.numbers[2] ? " KEEP_ASPECT" : "");
        case Opcode::Grayscale: return "GRAYSCALE " + in.dataset;
        case Opcode::NormalizeImages:
            return "NORMALIZE " + in.dataset + " RANGE " + in.values[0].value + " " + in.values[1].value + " (from " +
                   in.values[2].value + ".." + in.values[3].value + ")";
        case Opcode::Compress:      return "COMPRESS " + in.dataset + " QUALITY " + std::to_string(in.numbers[0]);
        case Opcode::Convert:       return "CONVERT " + in.dataset + " TO " + in.text;
        case Opcode::RemoveCorrupt: return "REMOVE_CORRUPT " + in.dataset;
        case Opcode::SaveImages:
            return (in.numbers[0] ? "SAVE_IMAGE " : "SAVE_IMAGES ") + in.dataset + " -> \"" + in.text + "\"";
    }
    return "";
}

void printIR(std::ostream& out, const IRProgram& program) {
    for (size_t i = 0; i < program.code.size(); ++i) {
        const Instruction& instruction = program.code[i];
        out << std::right << std::setw(3) << i + 1 << "  " << std::left << std::setw(62) << instructionText(instruction)
            << "  ; line " << instruction.line
            << (instruction.speculative ? "  (speculative: runs only if deoptimized)" : "") << '\n';
    }
}
