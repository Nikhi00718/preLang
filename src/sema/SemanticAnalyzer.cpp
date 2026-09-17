#include "sema/SemanticAnalyzer.h"

#include <algorithm>
#include <utility>

namespace {

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + items[i];
    }
    return text;
}

bool contains(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

// Whether a literal is the same kind of value as the column (for comparisons).
bool sameKind(ColumnType type, const Literal& value) {
    switch (type) {
        case ColumnType::Integer:
        case ColumnType::Float:   return value.kind == LiteralKind::Number;
        case ColumnType::String:  return value.kind == LiteralKind::String;
        case ColumnType::Boolean: return value.kind == LiteralKind::Boolean;
    }
    return false;
}

// Whether a literal can be stored in the column (for FILL_NULL).
// An INTEGER column cannot hold a decimal such as 2.5.
bool canStore(ColumnType type, const Literal& value) {
    const bool decimal = value.value.find('.') != std::string::npos;
    return sameKind(type, value) && !(type == ColumnType::Integer && decimal);
}

std::string describe(const Literal& value) {
    switch (value.kind) {
        case LiteralKind::Number:  return "number " + value.value;
        case LiteralKind::String:  return "text \"" + value.value + "\"";
        case LiteralKind::Boolean: return "boolean " + value.value;
    }
    return "";
}

std::string valueHint(ColumnType type) {
    switch (type) {
        case ColumnType::Integer: return "use a whole number, e.g. 0";
        case ColumnType::Float:   return "use a number, e.g. 0.5";
        case ColumnType::String:  return "put the text in double quotes";
        case ColumnType::Boolean: return "use TRUE or FALSE";
    }
    return "";
}

// Explains why a column is not numeric, or lists the columns that are.
std::string typeHint(const DatasetSymbol& dataset, const ColumnState& column) {
    if (!column.stringReason.empty()) {
        return "'" + column.name + "' is STRING because " + column.stringReason + " in " + dataset.schema.file;
    }
    std::vector<std::string> numeric;
    for (const ColumnState& other : dataset.columns) {
        if (isNumeric(other.type)) {
            numeric.push_back(other.name);
        }
    }
    return numeric.empty() ? "" : "numeric columns: " + join(numeric);
}

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(SymbolTable& symbols, SchemaProvider& schemas, std::filesystem::path baseFolder,
                                   Diagnostics& diagnostics)
    : symbols_(symbols), schemas_(schemas), baseFolder_(std::move(baseFolder)), diagnostics_(diagnostics) {}

AnalysisResult SemanticAnalyzer::analyze(const Program& program) {
    for (const auto& statement : program.statements) {
        statement->accept(*this);
    }
    for (const DatasetSymbol& dataset : symbols_.datasets()) {
        if (!dataset.saved) {
            diagnostics_.warning(Phase::Semantic, dataset.definedAt,
                                 "dataset '" + dataset.name + "' is loaded but never saved",
                                 "add SAVE " + dataset.name + " AS \"file.csv\";");
        }
    }
    return std::move(result_);
}

// ---------- Names ----------

DatasetSymbol* SemanticAnalyzer::findDataset(const std::string& name, SourceLocation location) {
    if (DatasetSymbol* dataset = symbols_.lookup(name)) {
        return dataset;
    }
    if (unreadable_.count(name) == 0) {  // a failed LOAD was already reported
        std::string hint = suggestClosest(name, symbols_.names());
        if (hint.empty()) {
            hint = "create it first with LOAD \"file.csv\" AS " + name + ";";
        }
        diagnostics_.error(Phase::Semantic, location, "dataset '" + name + "' is not defined", hint);
    }
    return nullptr;
}

ColumnState* SemanticAnalyzer::findColumn(DatasetSymbol& dataset, const std::string& name, SourceLocation location) {
    if (ColumnState* column = dataset.column(name)) {
        return column;
    }
    const auto removed = dataset.removedAt.find(name);
    if (removed != dataset.removedAt.end()) {
        diagnostics_.error(Phase::Semantic, location,
                           "column '" + name + "' was removed from '" + dataset.name + "' at line " +
                               std::to_string(removed->second),
                           "use the column before that line, or keep it in the SELECT/DROP");
        return nullptr;
    }
    std::string hint = suggestClosest(name, dataset.columnNames());
    if (hint.empty()) {
        hint = "columns of '" + dataset.name + "': " + join(dataset.columnNames());
    }
    diagnostics_.error(Phase::Semantic, location, "column '" + name + "' does not exist in '" + dataset.name + "'",
                       hint);
    return nullptr;
}

// ---------- Statements ----------

void SemanticAnalyzer::visit(const LoadStmt& node) {
    if (const DatasetSymbol* existing = symbols_.lookup(node.dataset)) {
        diagnostics_.error(Phase::Semantic, node.location,
                           "dataset '" + node.dataset + "' is already defined at line " +
                               std::to_string(existing->definedAt.line),
                           "choose another name after AS");
        return;
    }
    std::optional<Schema> schema = schemas_.infer((baseFolder_ / node.path).string(), node.location);
    if (!schema) {
        unreadable_.insert(node.dataset);
        return;
    }

    DatasetSymbol dataset;
    dataset.name = node.dataset;
    dataset.definedAt = node.location;
    for (const ColumnInfo& info : schema->columns) {
        ColumnState column{info.name, info.type, info.missingCount > 0, Range{}, true, info.stringReason};
        if (isNumeric(info.type) && info.hasRange) {
            column.range = {info.min, info.max};
        }
        dataset.columns.push_back(column);
    }
    dataset.schema = std::move(*schema);
    log(node, "LOAD " + node.dataset + ": " + std::to_string(dataset.schema.rowCount) + " rows, columns " +
                  join(dataset.columnNames()));
    symbols_.insert(std::move(dataset));
}

void SemanticAnalyzer::visit(const DropNullStmt& node) {
    DatasetSymbol* dataset = findDataset(node.dataset, node.location);
    if (!dataset) {
        return;
    }
    std::vector<std::string> nullable;
    for (const ColumnState& column : dataset->columns) {
        if (column.nullable) {
            nullable.push_back(column.name);
        }
    }
    if (nullable.empty()) {
        const std::string reason = "no column of '" + node.dataset + "' has missing values here";
        diagnostics_.warning(Phase::Semantic, node.location, "DROP_NULL has no effect: " + reason);
        result_.noEffect[&node] = reason;
        log(node, "DROP_NULL " + node.dataset + ": no effect");
        return;
    }
    for (ColumnState& column : dataset->columns) {
        column.nullable = false;
        column.exact = false;
    }
    log(node, "DROP_NULL " + node.dataset + ": removes rows with missing " + join(nullable) +
                  "; now no column has missing values");
}

void SemanticAnalyzer::visit(const FillNullStmt& node) {
    DatasetSymbol* dataset = findDataset(node.target.dataset, node.target.location);
    ColumnState* column = dataset ? findColumn(*dataset, node.target.column, node.target.location) : nullptr;
    if (!column) {
        return;
    }
    const std::string target = node.target.dataset + "." + node.target.column;
    if (!canStore(column->type, node.value)) {
        diagnostics_.error(Phase::Type, node.target.location,
                           std::string("cannot fill ") + columnTypeName(column->type) + " column " + target +
                               " with " + describe(node.value),
                           valueHint(column->type));
        return;
    }
    if (!column->nullable) {
        const std::string reason = target + " has no missing values here";
        diagnostics_.warning(Phase::Semantic, node.location, "FILL_NULL has no effect: " + reason);
        result_.noEffect[&node] = reason;
        log(node, "FILL_NULL " + target + ": no effect");
        return;
    }
    column->nullable = false;
    if (node.value.kind == LiteralKind::Number) {
        const double value = std::stod(node.value.value);
        column->range.min = std::min(column->range.min, value);
        column->range.max = std::max(column->range.max, value);
    }
    log(node, "FILL_NULL " + target + ": no missing values now");
}

void SemanticAnalyzer::visit(const NormalizeStmt& node) {
    DatasetSymbol* dataset = findDataset(node.target.dataset, node.target.location);
    ColumnState* column = dataset ? findColumn(*dataset, node.target.column, node.target.location) : nullptr;
    if (!column) {
        return;
    }
    const std::string target = node.target.dataset + "." + node.target.column;
    if (!isNumeric(column->type)) {
        diagnostics_.error(Phase::Type, node.target.location,
                           std::string("NORMALIZE needs a numeric column, but ") + target + " is " +
                               columnTypeName(column->type),
                           typeHint(*dataset, *column));
        return;
    }
    if (column->nullable) {
        diagnostics_.warning(Phase::Semantic, node.target.location,
                             "missing values in " + target + " stay missing after NORMALIZE",
                             "use FILL_NULL or DROP_NULL first if they should not stay missing");
    }
    const bool minMax = node.method == NormalizeMethod::MinMax;
    column->type = ColumnType::Float;
    column->range = minMax ? Range{0, 1} : Range{};
    column->exact = false;
    log(node, "NORMALIZE " + target + (minMax ? ": FLOAT in [0, 1]" : ": FLOAT, range unknown (z-score)"));
}

void SemanticAnalyzer::visit(const FilterStmt& node) {
    DatasetSymbol* dataset = findDataset(node.dataset, node.location);
    if (!dataset) {
        return;
    }
    const int errorsBefore = diagnostics_.errorCount();
    filterDataset_ = dataset;
    neverTrueReason_.clear();
    node.condition->accept(*this);
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    if (truth_ == Truth::AlwaysTrue) {
        const std::string reason = "the condition is true for every row";
        diagnostics_.warning(Phase::Semantic, node.location, "FILTER has no effect: " + reason,
                             "the compared columns have no missing values and their ranges always pass");
        result_.noEffect[&node] = reason;
        log(node, "FILTER " + node.dataset + ": no effect");
        return;
    }
    if (truth_ == Truth::NeverTrue) {
        diagnostics_.warning(Phase::Semantic, node.location, "FILTER removes every row: the condition is never true",
                             neverTrueReason_);
    }

    std::vector<std::string> learned;
    refine(*node.condition, *dataset, learned);
    for (ColumnState& column : dataset->columns) {
        column.exact = false;
    }
    log(node, "FILTER " + node.dataset + ": " + (learned.empty() ? "nothing new is known" : join(learned)));
}

void SemanticAnalyzer::visit(const SelectStmt& node) {
    DatasetSymbol* dataset = findDataset(node.dataset, node.location);
    if (!dataset) {
        return;
    }
    const int errorsBefore = diagnostics_.errorCount();
    std::vector<ColumnState> kept;
    std::vector<std::string> keptNames;
    for (const std::string& name : node.columns) {
        if (contains(keptNames, name)) {
            diagnostics_.error(Phase::Semantic, node.location, "column '" + name + "' is listed twice");
        } else if (ColumnState* column = findColumn(*dataset, name, node.location)) {
            kept.push_back(*column);
            keptNames.push_back(name);
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    std::vector<std::string> removed;
    for (const ColumnState& column : dataset->columns) {
        if (!contains(keptNames, column.name)) {
            removed.push_back(column.name);
            dataset->removedAt[column.name] = node.location.line;
        }
    }
    dataset->columns = kept;
    log(node, "SELECT " + node.dataset + ": keeps " + join(keptNames) +
                  (removed.empty() ? "" : "; removes " + join(removed)));
}

void SemanticAnalyzer::visit(const DropStmt& node) {
    DatasetSymbol* dataset = findDataset(node.dataset, node.location);
    if (!dataset) {
        return;
    }
    const int errorsBefore = diagnostics_.errorCount();
    std::vector<std::string> dropped;
    for (const std::string& name : node.columns) {
        if (contains(dropped, name)) {
            diagnostics_.error(Phase::Semantic, node.location, "column '" + name + "' is listed twice");
        } else if (findColumn(*dataset, name, node.location)) {
            dropped.push_back(name);
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    auto& columns = dataset->columns;
    columns.erase(std::remove_if(columns.begin(), columns.end(),
                                 [&](const ColumnState& column) { return contains(dropped, column.name); }),
                  columns.end());
    for (const std::string& name : dropped) {
        dataset->removedAt[name] = node.location.line;
    }
    log(node, "DROP " + node.dataset + ": removes " + join(dropped));
}

void SemanticAnalyzer::visit(const SaveStmt& node) {
    DatasetSymbol* dataset = findDataset(node.dataset, node.location);
    if (!dataset) {
        return;
    }
    dataset->saved = true;
    log(node, "SAVE " + node.dataset + " to \"" + node.path + "\": columns " + join(dataset->columnNames()));
}

// ---------- Data contracts ----------

void SemanticAnalyzer::visit(const ExpectStmt& node) {
    DatasetSymbol* dataset = findDataset(node.target.dataset, node.target.location);
    ColumnState* column = dataset ? findColumn(*dataset, node.target.column, node.target.location) : nullptr;
    if (!column) {
        return;
    }
    switch (node.constraint) {
        case ConstraintKind::IsNumber:  expectType(node, *dataset, *column, isNumeric(column->type)); return;
        case ConstraintKind::IsString:  expectType(node, *dataset, *column, column->type == ColumnType::String); return;
        case ConstraintKind::IsBoolean: expectType(node, *dataset, *column, column->type == ColumnType::Boolean); return;
        case ConstraintKind::NotNull:   expectNotNull(node, *dataset, *column); return;
        case ConstraintKind::Between:   expectBetween(node, *dataset, *column); return;
    }
}

// Column types come from the CSV and pandas enforces them when it reads the
// file, so a type contract is always decided at compile time.
void SemanticAnalyzer::expectType(const ExpectStmt& node, const DatasetSymbol& dataset, const ColumnState& column,
                                  bool holds) {
    const std::string target = dataset.name + "." + column.name;
    const std::string expected = node.constraint == ConstraintKind::IsNumber   ? "NUMBER"
                                 : node.constraint == ConstraintKind::IsString ? "STRING"
                                                                               : "BOOLEAN";
    if (!holds) {
        diagnostics_.error(Phase::Contract, node.target.location,
                           "contract violated: " + target + " should be " + expected + " but it is " +
                               columnTypeName(column.type),
                           typeHint(dataset, column));
        return;
    }
    log(node, "EXPECT " + target + " " + expected + ": PROVEN, the column is " + columnTypeName(column.type));
}

void SemanticAnalyzer::expectNotNull(const ExpectStmt& node, const DatasetSymbol& dataset, ColumnState& column) {
    const std::string target = dataset.name + "." + column.name;
    if (!column.nullable) {
        result_.noEffect[&node] = "contract proven at compile time: " + target + " has no missing values";
        log(node, "EXPECT " + target + " NOT_NULL: PROVEN, no missing values");
    } else if (column.exact) {
        diagnostics_.error(Phase::Contract, node.target.location,
                           "contract violated: " + target + " has missing values in " + dataset.schema.file,
                           "use FILL_NULL or DROP_NULL before this EXPECT");
    } else {
        column.nullable = false;  // rows only get past the run-time check if nothing is missing
        log(node, "EXPECT " + target + " NOT_NULL: checked at run time");
    }
}

void SemanticAnalyzer::expectBetween(const ExpectStmt& node, const DatasetSymbol& dataset, ColumnState& column) {
    const std::string target = dataset.name + "." + column.name;
    const std::string bounds = node.low + " AND " + node.high;
    if (!isNumeric(column.type)) {
        diagnostics_.error(Phase::Type, node.target.location,
                           std::string("BETWEEN needs a numeric column, but ") + target + " is " +
                               columnTypeName(column.type),
                           typeHint(dataset, column));
        return;
    }
    const double low = std::stod(node.low);
    const double high = std::stod(node.high);
    if (low > high) {
        diagnostics_.error(Phase::Semantic, node.target.location, "BETWEEN " + bounds + " is an empty range",
                           "write the smaller number first");
        return;
    }

    if (column.range.min >= low && column.range.max <= high) {
        result_.noEffect[&node] = "contract proven at compile time: " + target + " is in " + column.range.text();
        log(node, "EXPECT " + target + " BETWEEN " + bounds + ": PROVEN, values are in " + column.range.text());
    } else if (column.exact && column.range.isKnown()) {
        diagnostics_.error(Phase::Contract, node.target.location,
                           "contract violated: " + target + " has values from " + formatNumber(column.range.min) +
                               " to " + formatNumber(column.range.max),
                           "every value must be between " + node.low + " and " + node.high);
    } else {
        column.range.min = std::max(column.range.min, low);  // true after the run-time check passes
        column.range.max = std::min(column.range.max, high);
        log(node, "EXPECT " + target + " BETWEEN " + bounds + ": checked at run time");
    }
}

// ---------- FILTER conditions ----------

void SemanticAnalyzer::visit(const ComparisonExpr& node) {
    truth_ = Truth::Unknown;
    const ColumnState* column = findColumn(*filterDataset_, node.column, node.location);
    if (!column) {
        return;
    }
    const std::string type = columnTypeName(column->type);
    if (!sameKind(column->type, node.value)) {
        diagnostics_.error(Phase::Type, node.location,
                           "cannot compare " + type + " column '" + node.column + "' with " + describe(node.value),
                           valueHint(column->type));
        return;
    }
    if (!isNumeric(column->type)) {
        if (node.op != "==" && node.op != "!=") {
            diagnostics_.error(Phase::Type, node.location,
                               "operator '" + node.op + "' needs a numeric column, but '" + node.column + "' is " +
                                   type,
                               "use == or != for " + type + " columns");
        }
        return;
    }

    // Decide the comparison for every value in the column's known range.
    const double value = std::stod(node.value.value);
    const double low = column->range.min;
    const double high = column->range.max;
    bool always = false;
    bool never = false;
    if (node.op == ">") {
        always = low > value;
        never = high <= value;
    } else if (node.op == ">=") {
        always = low >= value;
        never = high < value;
    } else if (node.op == "<") {
        always = high < value;
        never = low >= value;
    } else if (node.op == "<=") {
        always = high <= value;
        never = low > value;
    } else if (node.op == "==") {
        always = low == value && high == value;
        never = value < low || value > high;
    } else {  // !=
        always = value < low || value > high;
        never = low == value && high == value;
    }

    if (never) {
        truth_ = Truth::NeverTrue;
        if (neverTrueReason_.empty()) {
            neverTrueReason_ = "'" + node.column + "' is in " + column->range.text() + ", so " + node.column + " " +
                               node.op + " " + node.value.value + " is never true";
        }
    } else if (always && !column->nullable) {
        truth_ = Truth::AlwaysTrue;  // only without missing values: they never pass a comparison
    }
}

void SemanticAnalyzer::visit(const LogicalExpr& node) {
    node.left->accept(*this);
    const Truth left = truth_;
    node.right->accept(*this);
    const Truth right = truth_;

    if (node.op == LogicalOp::And) {
        truth_ = (left == Truth::NeverTrue || right == Truth::NeverTrue)       ? Truth::NeverTrue
                 : (left == Truth::AlwaysTrue && right == Truth::AlwaysTrue) ? Truth::AlwaysTrue
                                                                             : Truth::Unknown;
    } else {
        truth_ = (left == Truth::AlwaysTrue || right == Truth::AlwaysTrue)   ? Truth::AlwaysTrue
                 : (left == Truth::NeverTrue && right == Truth::NeverTrue) ? Truth::NeverTrue
                                                                           : Truth::Unknown;
    }
}

// NOT of a comparison on a missing value is still unknown, so nothing is
// concluded; the operand is only visited for its name and type checks.
void SemanticAnalyzer::visit(const NotExpr& node) {
    const std::string savedReason = neverTrueReason_;
    node.operand->accept(*this);
    neverTrueReason_ = savedReason;
    truth_ = Truth::Unknown;
}

// Rows that pass a FILTER satisfy every comparison joined by AND, so each of
// those comparisons narrows what is known about its column.
void SemanticAnalyzer::refine(const Expr& condition, DatasetSymbol& dataset, std::vector<std::string>& learned) {
    if (const auto* logical = dynamic_cast<const LogicalExpr*>(&condition)) {
        if (logical->op == LogicalOp::And) {
            refine(*logical->left, dataset, learned);
            refine(*logical->right, dataset, learned);
        }
        return;
    }
    const auto* comparison = dynamic_cast<const ComparisonExpr*>(&condition);
    if (!comparison) {
        return;  // NOT: nothing is certain
    }
    ColumnState* column = dataset.column(comparison->column);
    column->nullable = false;  // a missing value never passes a comparison
    std::string fact = comparison->column + " has no missing values";
    if (isNumeric(column->type)) {
        const double value = std::stod(comparison->value.value);
        const std::string& op = comparison->op;
        if (op == ">" || op == ">=" || op == "==") {
            column->range.min = std::max(column->range.min, value);
        }
        if (op == "<" || op == "<=" || op == "==") {
            column->range.max = std::min(column->range.max, value);
        }
        fact += " and is in " + column->range.text();
    }
    learned.push_back(fact);
}

void SemanticAnalyzer::log(const Stmt& node, const std::string& text) {
    result_.log.push_back("line " + std::to_string(node.location.line) + ": " + text);
}
