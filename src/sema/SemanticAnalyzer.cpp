#include "sema/SemanticAnalyzer.h"

#include "ast/ASTPrinter.h"
#include "common/Dates.h"
#include "dataset/DatasetLinker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace {

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + items[i];
    }
    return text;
}

// A statement the state shows to be useless. In a private compilation the state comes
// from noisy released facts, so the statement is only probably useless: it stays in the
// program, guarded, and runs if the data needs it. Saying more would mislead the analyst.
std::string noEffectText(bool released, const std::string& statement, const std::string& why) {
    return released ? statement + " probably has no effect: by the released facts, " + why +
                          "; it stays in the program and runs if the data needs it"
                    : statement + " has no effect: " + why;
}

bool contains(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

// Whether a literal is the same kind of value as the column (for comparisons).
bool sameKind(ColumnType type, const Literal& value) {
    switch (type) {
        case ColumnType::Integer:
        case ColumnType::Float:    return value.kind == LiteralKind::Number;
        case ColumnType::String:   return value.kind == LiteralKind::String;
        case ColumnType::Boolean:  return value.kind == LiteralKind::Boolean;
        case ColumnType::DateTime: return false;  // there are no date literals
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
        case ColumnType::Integer:  return "use a whole number, e.g. 0";
        case ColumnType::Float:    return "use a number, e.g. 0.5";
        case ColumnType::String:   return "put the text in double quotes";
        case ColumnType::Boolean:  return "use TRUE or FALSE";
        case ColumnType::DateTime: return "DATETIME columns can be kept, dropped, saved or checked with NOT_NULL";
    }
    return "";
}

// For a value that does not fit a column. A column of numbers that became STRING
// because of one stray cell needs that cell fixed, not quotes around the value.
std::string valueHint(const ColumnState& column) {
    return column.stringReason.empty() ? valueHint(column.type)
                                       : "'" + column.name + "' is STRING because " + column.stringReason;
}

std::string exprText(const Expr& expr) {
    if (const auto* comparison = dynamic_cast<const ComparisonExpr*>(&expr)) {
        return comparison->column + " " + comparison->op + " " + literalText(comparison->value);
    }
    if (const auto* logical = dynamic_cast<const LogicalExpr*>(&expr)) {
        return "(" + exprText(*logical->left) + (logical->op == LogicalOp::And ? " AND " : " OR ") +
               exprText(*logical->right) + ")";
    }
    const auto& negation = dynamic_cast<const NotExpr&>(expr);
    return "NOT " + exprText(*negation.operand);
}

bool endsWithJpg(const std::string& path) {
    std::string lower;
    for (char c : path) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.size() >= 4 && (lower.compare(lower.size() - 4, 4, ".jpg") == 0 ||
                                 (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0));
}

TableState makeTable(TableSchema schema) {
    TableState table;
    table.name = schema.name;
    for (const ColumnInfo& info : schema.columns) {
        // exact promises both: the range is attained and `nullable` is certain. FILL_NULL relies
        // on the second (the fill value really appears), so unknown missing values rule it out.
        const bool nullable = info.missingCount > 0 || !info.missingKnown;
        const bool exact = schema.valuesKnown && (info.boundsExact || !info.hasRange) && info.missingKnown;
        ColumnState column{info.name, info.type, nullable, Range{}, exact, false, info.stringReason};
        if (isNumeric(info.type) && info.hasRange) {
            column.range = {info.min, info.max};
        }
        table.columns.push_back(column);
    }
    table.schema = std::move(schema);
    return table;
}

// What DERIVE knows about a value: its type, whether it may be missing, its range.
struct ValueFacts {
    ColumnType type = ColumnType::Integer;
    bool nullable = false;
    Range range;
};

// Interval arithmetic. An infinite bound times 0, or a finite one over an
// infinite one, is 0: the other end points then give the bound that matters.
double times(double a, double b) { return a == 0 || b == 0 ? 0 : a * b; }
double over(double a, double b) { return std::isinf(b) ? 0 : a / b; }

Range combine(const std::string& op, const Range& a, const Range& b) {
    if (op == "+") {
        return {a.min + b.min, a.max + b.max};
    }
    if (op == "-") {
        return {a.min - b.max, a.max - b.min};
    }
    const auto apply = [&](double x, double y) { return op == "*" ? times(x, y) : over(x, y); };
    const double ends[] = {apply(a.min, b.min), apply(a.min, b.max), apply(a.max, b.min), apply(a.max, b.max)};
    return {*std::min_element(std::begin(ends), std::end(ends)), *std::max_element(std::begin(ends), std::end(ends))};
}

// The range of a part of the dates in [dates.min, dates.max]. A part is narrower
// than its full range only when both dates fall in the same year, month or day
// (or week, for WEEKDAY), so a wider date range never gives a narrower part.
Range datePartRange(const std::string& part, const Range& dates) {
    const bool known = !std::isinf(dates.min) && !std::isinf(dates.max);
    if (part == "YEAR") {
        return {std::isinf(dates.min) ? dates.min : static_cast<double>(civilFromSeconds(dates.min).year),
                std::isinf(dates.max) ? dates.max : static_cast<double>(civilFromSeconds(dates.max).year)};
    }
    const CivilTime a = civilFromSeconds(known ? dates.min : 0);
    const CivilTime b = civilFromSeconds(known ? dates.max : 0);
    const bool sameYear = known && a.year == b.year;
    if (part == "MONTH") {
        return sameYear ? Range{double(a.month), double(b.month)} : Range{1, 12};
    }
    if (part == "DAY") {
        return sameYear && a.month == b.month ? Range{double(a.day), double(b.day)} : Range{1, 31};
    }
    if (part == "HOUR") {
        return known && a.days == b.days ? Range{double(a.hour), double(b.hour)} : Range{0, 23};
    }
    // WEEKDAY
    return known && b.days - a.days < 7 && a.weekday <= b.weekday ? Range{double(a.weekday), double(b.weekday)}
                                                                  : Range{0, 6};
}

ImageState makeImageState(const ImageSetSchema& set) {
    ImageState state;
    bool eightBit = false;
    bool sixteenBit = false;
    for (const ImageInfo& info : set.images) {
        state.shapes.push_back({info.path, info.width, info.height, info.channels, info.corrupt, info.problem});
        if (!info.corrupt) {
            (info.bitDepth > 8 ? sixteenBit : eightBit) = true;
        }
    }
    state.dtype = sixteenBit ? (eightBit ? "MIXED" : "UINT16") : "UINT8";
    state.values = sixteenBit ? Range{0, 65535} : Range{0, 255};
    return state;
}

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(SymbolTable& symbols, SchemaProvider& schemas, std::filesystem::path baseFolder,
                                   Diagnostics& diagnostics)
    : symbols_(symbols),
      schemas_(schemas),
      baseFolder_(std::move(baseFolder)),
      diagnostics_(diagnostics),
      resolver_(symbols, diagnostics, unreadable_),
      contracts_(diagnostics, result_) {}

AnalysisResult SemanticAnalyzer::analyze(const Program& program) {
    for (const auto& statement : program.statements) {
        statement->accept(*this);
    }
    for (const DatasetSymbol& dataset : symbols_.datasets()) {
        if (!dataset.saved && !dataset.joined && dataset.kind != DatasetKind::Dataset) {
            diagnostics_.warning(Phase::Semantic, dataset.definedAt,
                                 "dataset '" + dataset.name + "' is loaded but never saved",
                                 "add a SAVE statement, or remove the LOAD");
        }
    }
    return std::move(result_);
}

// ---------- Helpers ----------

void SemanticAnalyzer::addTarget(const Stmt& node, const Target& target, const std::string& noEffectReason) {
    result_.targets[&node].push_back(target);
    if (!noEffectReason.empty()) {
        result_.noEffect[{&node, targetKey(target)}] = noEffectReason;
    }
}

Target SemanticAnalyzer::tableTarget(const DatasetSymbol& dataset, const TableState& table,
                                     const std::string& column) const {
    return {dataset.name, dataset.kind == DatasetKind::TableSet ? table.name : "", column, {}};
}

void SemanticAnalyzer::recordColumn(const DatasetSymbol& dataset, const TableState& table, const ColumnState& column,
                                    int line, const std::string& operation) {
    result_.lineage.record(columnDisplayName(dataset, table, column.name), line, operation, column.typeText());
}

void SemanticAnalyzer::recordImages(const DatasetSymbol& images, int line, const std::string& operation) {
    result_.lineage.record(images.name, line, operation, images.images.summary());
}

void SemanticAnalyzer::log(const Stmt& node, const std::string& text) {
    result_.log.push_back("line " + std::to_string(node.location.line) + ": " + text);
}

// Checks a type rule on every matched column. One error lists all columns,
// so a wildcard shows which tables break the rule.
bool SemanticAnalyzer::checkTypes(const Selector& selector, const DatasetSymbol& dataset,
                                  const std::vector<ColumnMatch>& matched,
                                  const std::function<bool(const ColumnState&)>& fits, const std::string& rule,
                                  const std::function<std::string(const ColumnState&)>& hintFor) {
    std::vector<std::string> notes;
    const ColumnMatch* firstBad = nullptr;
    int bad = 0;
    for (const ColumnMatch& match : matched) {
        const bool ok = fits(*match.column);
        const std::string name = columnDisplayName(dataset, *match.table, match.column->name);
        notes.push_back(name + " : " + match.column->typeText() + (ok ? "   ok" : "   <-- breaks the rule"));
        if (!ok) {
            ++bad;
            firstBad = firstBad ? firstBad : &match;
        }
    }
    if (bad == 0) {
        return true;
    }
    const std::string badName = columnDisplayName(dataset, *firstBad->table, firstBad->column->name);
    if (matched.size() == 1) {
        diagnostics_.error(Phase::Type, selector.location,
                           rule + ", but " + badName + " is " + columnTypeName(firstBad->column->type),
                           hintFor(*firstBad->column));
    } else {
        Diagnostic& error =
            diagnostics_.error(Phase::Type, selector.location,
                               rule + ": " + std::to_string(bad) + " of " + std::to_string(matched.size()) +
                                   " columns matched by '" + selector.text() + "' break it",
                               "narrow the selector, e.g. WHERE TYPE NUMERIC or EXCEPT [...]");
        error.notes = notes;
    }
    return false;
}

// Pixel operations need every file to decode. Broken files were found while
// reading the headers, so this is reported before anything runs.
bool SemanticAnalyzer::decodable(DatasetSymbol& images, const Stmt& node) {
    const int corrupt = images.images.corruptCount();
    if (corrupt == 0) {
        return true;
    }
    if (corruptReported_.insert(images.name).second) {
        Diagnostic& error = diagnostics_.error(
            Phase::Schema, node.location,
            std::to_string(corrupt) + " of " + std::to_string(images.images.shapes.size()) + " files in " +
                images.imageSchema.root + " cannot be decoded",
            "add REMOVE_CORRUPT " + images.name + "; before line " + std::to_string(node.location.line));
        int listed = 0;
        for (const ImageShape& shape : images.images.shapes) {
            if (shape.corrupt && listed++ < 5) {
                error.notes.push_back(shape.path + ": " + shape.problem);
            }
        }
    }
    return false;
}

// ---------- LOAD ----------

void SemanticAnalyzer::visit(const LoadStmt& node) {
    if (const DatasetSymbol* existing = symbols_.lookup(node.dataset)) {
        diagnostics_.error(
            Phase::Semantic, node.location,
            "dataset '" + node.dataset + "' is already defined at line " + std::to_string(existing->definedAt.line),
            "choose another name after AS");
        return;
    }
    const std::string path = (baseFolder_ / node.path).lexically_normal().generic_string();
    DatasetSymbol dataset;
    dataset.name = node.dataset;
    dataset.definedAt = node.location;

    if (node.kind == DataKind::Table || node.kind == DataKind::TableSet) {
        std::optional<TableSetSchema> tables;
        if (node.kind == DataKind::Table) {
            std::optional<TableSchema> table = schemas_.readTable(path, node.dataset, node.location);
            if (table) {
                tables = TableSetSchema{std::move(*table)};
            }
        } else {
            tables = schemas_.readTableFolder(path, node.location);
        }
        if (!tables) {
            unreadable_.insert(node.dataset);
            return;
        }
        dataset.kind = node.kind == DataKind::Table ? DatasetKind::Table : DatasetKind::TableSet;
        std::vector<std::string> summary;
        for (TableSchema& schema : *tables) {
            summary.push_back(
                schema.name + " (" +
                (schema.released ? std::string("released facts") : std::to_string(schema.rowCount) + " rows") + ", " +
                std::to_string(schema.columns.size()) + " columns)");
            dataset.tables.push_back(makeTable(std::move(schema)));
        }
        for (const TableState& table : dataset.tables) {
            for (const ColumnState& column : table.columns) {
                recordColumn(dataset, table, column, node.location.line, "loaded from " + table.schema.file);
            }
        }
        log(node, dataKindName(node.kind) + " " + node.dataset + ": " + join(summary));
    } else {
        std::optional<ImageSetSchema> set =
            schemas_.readImages(path, node.kind == DataKind::Image, node.recursive, node.location);
        if (set && node.kind == DataKind::Image && set->images.front().corrupt) {
            diagnostics_.error(Phase::Schema, node.location,
                               "image '" + path + "' cannot be decoded: " + set->images.front().problem);
            set.reset();
        }
        if (!set) {
            unreadable_.insert(node.dataset);
            return;
        }
        dataset.kind = node.kind == DataKind::Image ? DatasetKind::Image : DatasetKind::ImageSet;
        dataset.images = makeImageState(*set);
        dataset.imageSchema = std::move(*set);
        recordImages(dataset, node.location.line, "loaded from " + path);
        log(node, dataKindName(node.kind) + " " + node.dataset + ": " + dataset.images.summary());
    }
    symbols_.insert(std::move(dataset));
}

// ---------- Table statements ----------

void SemanticAnalyzer::visit(const DropNullStmt& node) {
    std::optional<TableResolution> resolution = resolver_.tables(node.target, "DROP_NULL");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    // DROP_NULL t [a, b] looks only at these columns; they must exist in every table.
    const int errorsBefore = diagnostics_.errorCount();
    for (TableState* table : resolution->tables) {
        for (const std::string& name : node.columns) {
            resolver_.column(dataset, *table, name, node.location);
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }
    const auto looksAt = [&](const ColumnState& column) {
        return node.columns.empty() || contains(node.columns, column.name);
    };
    std::vector<std::string> changed;
    std::vector<std::string> idle;
    for (TableState* table : resolution->tables) {
        const std::string tableName = columnDisplayName(dataset, *table, "");
        std::vector<std::string> nullable;
        for (const ColumnState& column : table->columns) {
            if (column.nullable && looksAt(column)) {
                nullable.push_back(column.name);
            }
        }
        if (nullable.empty()) {
            addTarget(node, tableTarget(dataset, *table), "no column of " + tableName + " has missing values here");
            idle.push_back(tableName);
            continue;
        }
        for (ColumnState& column : table->columns) {
            column.nullable = column.nullable && !looksAt(column);
            column.exact = false;  // rows are removed from every column
            column.rowsRemoved = true;
            recordColumn(dataset, *table, column, node.location.line, "DROP_NULL");
        }
        addTarget(node, tableTarget(dataset, *table));
        changed.push_back(tableName + " (rows with missing " + join(nullable) + ")");
    }
    if (changed.empty()) {
        const bool released = std::any_of(resolution->tables.begin(), resolution->tables.end(),
                                          [](const TableState* table) { return table->schema.released; });
        diagnostics_.warning(Phase::Semantic, node.location,
                             noEffectText(released, "DROP_NULL", "no column has missing values here"));
    }
    log(node, "DROP_NULL " + (changed.empty() ? "has no effect" : "removes rows of " + join(changed)) +
                  (idle.empty() || changed.empty() ? "" : "; no effect on " + join(idle)));
}

void SemanticAnalyzer::visit(const FillNullStmt& node) {
    const bool literal = node.method == FillMethod::Value;
    const bool average = node.method == FillMethod::Mean || node.method == FillMethod::Median;
    const auto fits = [&](const ColumnState& column) {
        return literal ? canStore(column.type, node.value) : !average || isNumeric(column.type);
    };
    // WHERE NULLABLE means the columns with missing values, which depends on the batch. Filling a
    // column without missing values has no effect, so the statement means the same on every batch
    // if it matches every column the fill fits (those without missing values here become no-ops,
    // removed behind a "no missing values" certificate) and the others only where values are
    // missing (on a batch with missing values there, a fresh compilation reports a type error,
    // which the admission certificate stands in for).
    Selector selector = node.target;
    const bool nullable = selector.filter == SelectorFilter::Nullable;
    if (nullable) {
        selector.filter = SelectorFilter::None;
    }
    std::optional<ColumnResolution> resolution = resolver_.columns(selector, "FILL_NULL");
    if (!resolution) {
        return;
    }
    if (nullable) {
        std::vector<ColumnMatch> kept;
        for (const ColumnMatch& match : resolution->matched) {
            if (match.column->nullable || fits(*match.column)) {
                kept.push_back(match);
            }
        }
        resolution->matched = kept;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    const std::string method = literal ? literalText(node.value) : fillMethodName(node.method);
    const auto hint = [&](const ColumnState& column) {
        return literal
                   ? valueHint(column)
                   : "use MODE (the most frequent value) for a " + std::string(columnTypeName(column.type)) + " column";
    };
    const std::string rule = literal ? "FILL_NULL with " + describe(node.value) + " needs a matching column type"
                                     : "FILL_NULL with " + method + " needs a numeric column";
    if (!checkTypes(node.target, dataset, resolution->matched, fits, rule, hint)) {
        return;
    }

    std::vector<std::string> changed;
    std::vector<std::string> idle;
    for (const ColumnMatch& match : resolution->matched) {
        ColumnState& column = *match.column;
        const std::string name = columnDisplayName(dataset, *match.table, column.name);
        const Target target = tableTarget(dataset, *match.table, column.name);
        // MEAN and MEDIAN make an INTEGER column FLOAT whether or not values are
        // missing, so the type never depends on the data (that keeps the analysis
        // monotone, which data certificates rely on).
        const bool converts = average && column.type == ColumnType::Integer;
        if (!column.nullable && !converts) {
            addTarget(node, target, name + " has no missing values here");
            idle.push_back(name);
            continue;
        }
        column.nullable = false;  // the generated code stops if there is no value to fill with
        column.valuesChanged = true;
        if (converts) {
            column.type = ColumnType::Float;
        }
        if (literal && node.value.kind == LiteralKind::Number) {
            const double value = std::stod(node.value.value);
            column.range.min = std::min(column.range.min, value);
            column.range.max = std::max(column.range.max, value);
        }  // a mean, median or mode lies within the values, so the range does not change
        recordColumn(dataset, *match.table, column, node.location.line, "FILL_NULL " + method);
        addTarget(node, target);
        changed.push_back(name);
    }
    if (changed.empty() && !idle.empty()) {
        const bool released = std::any_of(resolution->matched.begin(), resolution->matched.end(),
                                          [](const ColumnMatch& match) { return match.table->schema.released; });
        diagnostics_.warning(Phase::Semantic, node.location,
                             noEffectText(released, "FILL_NULL",
                                          idle.size() == 1 ? idle.front() + " has no missing values here"
                                                           : "none of the matched columns has missing values here"));
    }
    log(node, "FILL_NULL " + (changed.empty() ? "has no effect" : "fills " + join(changed)) +
                  (idle.empty() || changed.empty() ? "" : "; no effect on " + join(idle)));
}

void SemanticAnalyzer::visit(const NormalizeStmt& node) {
    if (node.method == NormalizeMethod::Range) {
        normalizeImages(node);
        return;
    }
    std::optional<ColumnResolution> resolution = resolver_.columns(node.target, "NORMALIZE");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    const auto numeric = [](const ColumnState& column) { return isNumeric(column.type); };
    const auto hint = [&](const ColumnState& column) {
        return column.stringReason.empty() ? std::string()
                                           : "'" + column.name + "' is STRING because " + column.stringReason;
    };
    if (!checkTypes(node.target, dataset, resolution->matched, numeric, "NORMALIZE needs a numeric column", hint)) {
        return;
    }

    const bool minMax = node.method == NormalizeMethod::MinMax;
    std::vector<std::string> names;
    std::vector<std::string> nullable;
    for (const ColumnMatch& match : resolution->matched) {
        ColumnState& column = *match.column;
        const std::string name = columnDisplayName(dataset, *match.table, column.name);
        if (column.nullable) {
            nullable.push_back(name);
        }
        column.type = ColumnType::Float;
        column.range = minMax ? Range{0, 1} : Range{};
        column.exact = false;
        column.valuesChanged = true;
        recordColumn(dataset, *match.table, column, node.location.line,
                     std::string("NORMALIZE USING ") + (minMax ? "MINMAX" : "ZSCORE"));
        addTarget(node, tableTarget(dataset, *match.table, column.name));
        names.push_back(name);
    }
    if (!nullable.empty()) {
        diagnostics_.warning(Phase::Semantic, node.target.location,
                             "missing values in " + join(nullable) + " stay missing after NORMALIZE",
                             "use FILL_NULL or DROP_NULL first if they should not stay missing");
    }
    if (!names.empty()) {
        log(node, "NORMALIZE " + join(names) + (minMax ? ": FLOAT in [0, 1]" : ": FLOAT, range unknown (z-score)"));
    }
}

void SemanticAnalyzer::visit(const FilterStmt& node) {
    std::optional<TableResolution> resolution = resolver_.singleTable(node.target, "FILTER");
    if (!resolution) {
        return;
    }
    filterDataset_ = resolution->dataset;
    filterTable_ = resolution->tables.front();
    const int errorsBefore = diagnostics_.errorCount();
    neverTrueReason_.clear();
    node.condition->accept(*this);
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    const Target target = tableTarget(*filterDataset_, *filterTable_);
    const std::string tableName = columnDisplayName(*filterDataset_, *filterTable_, "");
    if (truth_ == Truth::AlwaysTrue) {
        diagnostics_.warning(
            Phase::Semantic, node.location,
            noEffectText(filterTable_->schema.released, "FILTER", "the condition is true for every row"),
            "the compared columns have no missing values and their ranges always pass");
        addTarget(node, target, "the condition is true for every row of " + tableName);
        log(node, "FILTER " + tableName + " has no effect");
        return;
    }
    if (truth_ == Truth::NeverTrue) {
        diagnostics_.warning(
            Phase::Semantic, node.location,
            filterTable_->schema.released
                ? "FILTER probably removes every row: by the released facts, the condition is never true"
                : "FILTER removes every row: the condition is never true",
            neverTrueReason_);
    }

    std::vector<std::string> learned;
    refine(*node.condition, *filterTable_, learned);
    for (ColumnState& column : filterTable_->columns) {
        column.exact = false;
        column.rowsRemoved = true;
        recordColumn(*filterDataset_, *filterTable_, column, node.location.line,
                     "FILTER WHERE " + exprText(*node.condition));
    }
    addTarget(node, target);
    log(node, "FILTER " + tableName + ": " + (learned.empty() ? "nothing new is known" : join(learned)));
}

void SemanticAnalyzer::visit(const SelectStmt& node) {
    std::optional<TableResolution> resolution = resolver_.singleTable(node.target, "SELECT");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    TableState& table = *resolution->tables.front();
    const int errorsBefore = diagnostics_.errorCount();
    std::vector<ColumnState> kept;
    std::vector<std::string> keptNames;
    for (const std::string& name : node.columns) {
        if (contains(keptNames, name)) {
            diagnostics_.error(Phase::Semantic, node.location, "column '" + name + "' is listed twice");
        } else if (ColumnState* column = resolver_.column(dataset, table, name, node.location)) {
            kept.push_back(*column);
            keptNames.push_back(name);
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    std::vector<std::string> removed;
    for (const ColumnState& column : table.columns) {
        if (!contains(keptNames, column.name)) {
            removed.push_back(column.name);
            table.removedAt[column.name] = node.location.line;
            result_.lineage.record(columnDisplayName(dataset, table, column.name), node.location.line,
                                   "SELECT (not selected)", "removed");
        }
    }
    table.columns = kept;
    addTarget(node, tableTarget(dataset, table));
    log(node, "SELECT " + columnDisplayName(dataset, table, "") + ": keeps " + join(keptNames) +
                  (removed.empty() ? "" : "; removes " + join(removed)));
}

void SemanticAnalyzer::visit(const DropStmt& node) {
    std::optional<TableResolution> resolution = resolver_.tables(node.target, "DROP");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    const int errorsBefore = diagnostics_.errorCount();

    // Check every table first, so nothing changes if one of them has an error.
    std::vector<std::pair<TableState*, std::vector<std::string>>> plan;
    for (TableState* table : resolution->tables) {
        std::vector<std::string> dropped;
        for (const std::string& name : node.columns) {
            if (contains(dropped, name)) {
                diagnostics_.error(Phase::Semantic, node.location, "column '" + name + "' is listed twice");
            } else if (table->column(name)) {
                dropped.push_back(name);
            } else if (!node.target.ifExists) {
                resolver_.column(dataset, *table, name, node.location);
            }
        }
        plan.emplace_back(table, dropped);
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }

    std::vector<std::string> removed;
    for (auto& [table, dropped] : plan) {
        auto& columns = table->columns;
        columns.erase(std::remove_if(columns.begin(), columns.end(),
                                     [&](const ColumnState& column) { return contains(dropped, column.name); }),
                      columns.end());
        for (const std::string& name : dropped) {
            table->removedAt[name] = node.location.line;
            result_.lineage.record(columnDisplayName(dataset, *table, name), node.location.line, "DROP", "removed");
            addTarget(node, tableTarget(dataset, *table, name));
            removed.push_back(columnDisplayName(dataset, *table, name));
        }
    }
    if (removed.empty()) {
        diagnostics_.warning(Phase::Semantic, node.location, "DROP has no effect: no table has these columns");
    }
    log(node, "DROP " + (removed.empty() ? "has no effect" : "removes " + join(removed)));
}

// PARSE_DATE reads the dates in a STRING column. Whether every value is a date,
// and the first and last date, are facts of the file: they decide the statement
// before any data is processed, and a data certificate can require them.
void SemanticAnalyzer::visit(const ParseDateStmt& node) {
    std::optional<ColumnResolution> resolution = resolver_.columns(node.target, "PARSE_DATE");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    std::vector<std::string> parsed;
    std::vector<std::string> idle;
    for (const ColumnMatch& match : resolution->matched) {
        ColumnState& column = *match.column;
        TableState& table = *match.table;
        const std::string name = columnDisplayName(dataset, table, column.name);
        const Target target = tableTarget(dataset, table, column.name);
        if (column.type == ColumnType::DateTime) {
            addTarget(node, target, name + " is already DATETIME");
            idle.push_back(name);
            continue;
        }
        const auto info = std::find_if(table.schema.columns.begin(), table.schema.columns.end(),
                                       [&](const ColumnInfo& c) { return c.name == column.name; });
        if (column.type != ColumnType::String || column.valuesChanged || info == table.schema.columns.end()) {
            diagnostics_.error(Phase::Type, node.target.location,
                               "PARSE_DATE needs a STRING column of dates as the file has it, but " + name +
                                   (column.valuesChanged ? " was changed after LOAD"
                                                         : " is " + std::string(columnTypeName(column.type))),
                               column.valuesChanged ? "put PARSE_DATE before the statements that change it" : "");
            continue;
        }
        if (!info->datesKnown) {
            if (table.schema.released) {
                diagnostics_.error(Phase::Schema, node.target.location,
                                   "PARSE_DATE needs the values of " + name +
                                       ", but a private compilation sees only the released facts of " +
                                       table.schema.file);
                continue;
            }
            if (table.schema.format != TableFormat::Csv || !table.schema.valuesKnown) {
                diagnostics_.error(Phase::Schema, node.target.location,
                                   "PARSE_DATE needs the values of " + name + ", but only the footer of " +
                                       table.schema.file + " is read at compile time",
                                   "Parquet files keep dates in DATETIME columns, which need no PARSE_DATE");
                continue;
            }
            readDateFacts(table.schema, *info);
        }
        if (!info->allDates) {
            diagnostics_.error(Phase::Contract, node.target.location,
                               name + " has values that are not dates" +
                                   (info->notDateReason.empty() ? std::string() : ": " + info->notDateReason),
                               "PARSE_DATE reads YYYY-MM-DD, YYYY-MM-DD HH:MM or YYYY-MM-DD HH:MM:SS, "
                               "in the same layout in every row");
            continue;
        }
        column.type = ColumnType::DateTime;
        column.range = info->hasDates ? Range{info->dateMin, info->dateMax} : Range{};
        column.valuesChanged = true;  // the values are dates now, not the text of the file
        recordColumn(dataset, table, column, node.location.line, "PARSE_DATE");
        addTarget(node, target);
        parsed.push_back(
            name + (info->hasDates ? " (" + dateText(info->dateMin) + " to " + dateText(info->dateMax) + ")" : ""));
    }
    log(node, "PARSE_DATE " + (parsed.empty() ? "has no effect" : join(parsed)) +
                  (idle.empty() || parsed.empty() ? "" : "; no effect on " + join(idle)));
}

// DERIVE computes a new column (or replaces one) from other columns of the same
// table. Its facts follow from theirs: interval arithmetic for + - * /, and the
// calendar for the parts of a date. A division by a value that can be 0 here is
// an error: pandas would write inf or a missing value without saying so.
void SemanticAnalyzer::visit(const DeriveStmt& node) {
    Selector tableSelector = node.target;
    tableSelector.parts.pop_back();
    std::optional<TableResolution> resolution = resolver_.singleTable(tableSelector, "DERIVE");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    TableState& table = *resolution->tables.front();
    const std::string columnName = node.target.parts.back();
    const int errorsBefore = diagnostics_.errorCount();

    const std::function<ValueFacts(const ValueExpr&)> facts = [&](const ValueExpr& expr) -> ValueFacts {
        ValueFacts result;
        switch (expr.kind) {
            case ValueKind::Number: {
                const double value = std::stod(expr.text);
                result.type = expr.text.find('.') == std::string::npos ? ColumnType::Integer : ColumnType::Float;
                result.range = {value, value};
                return result;
            }
            case ValueKind::Column: {
                const ColumnState* column = resolver_.column(dataset, table, expr.text, expr.location);
                if (column && !isNumeric(column->type)) {
                    diagnostics_.error(Phase::Type, expr.location,
                                       "DERIVE computes with numeric columns, but '" + expr.text + "' is " +
                                           columnTypeName(column->type),
                                       column->type == ColumnType::DateTime
                                           ? "take a part of it: YEAR, MONTH, DAY, HOUR or WEEKDAY(" + expr.text + ")"
                                           : valueHint(*column));
                }
                if (column) {
                    result = {column->type, column->nullable, column->range};
                }
                return result;
            }
            case ValueKind::DatePart: {
                const ColumnState* column = resolver_.column(dataset, table, expr.left->text, expr.left->location);
                if (column && column->type != ColumnType::DateTime) {
                    diagnostics_.error(Phase::Type, expr.left->location,
                                       expr.text + " needs a DATETIME column, but '" + expr.left->text + "' is " +
                                           columnTypeName(column->type),
                                       column->type == ColumnType::String ? "PARSE_DATE it first" : "");
                }
                if (column) {
                    result = {ColumnType::Integer, column->nullable, datePartRange(expr.text, column->range)};
                }
                return result;
            }
            case ValueKind::Binary: {
                const ValueFacts left = facts(*expr.left);
                const ValueFacts right = facts(*expr.right);
                if (expr.text == "/" && right.range.min <= 0 && right.range.max >= 0) {
                    diagnostics_.error(
                        Phase::Contract, expr.right->location,
                        "DERIVE divides by " + valueText(*expr.right) + ", which can be 0 here (" +
                            (right.range.isKnown() ? "it is in " + right.range.text() : "its range is not known") + ")",
                        "FILTER the rows where it is 0 first, or EXPECT it BETWEEN two bounds above 0");
                }
                result.type = expr.text == "/" || left.type == ColumnType::Float || right.type == ColumnType::Float
                                  ? ColumnType::Float
                                  : ColumnType::Integer;
                result.nullable = left.nullable || right.nullable;
                result.range = combine(expr.text, left.range, right.range);
                return result;
            }
        }
        return result;
    };
    const ValueFacts value = facts(*node.value);
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }
    const ColumnState state{columnName, value.type, value.nullable, value.range, false, true, ""};
    ColumnState* existing = table.column(columnName);
    if (existing) {
        *existing = state;
    } else {
        table.columns.push_back(state);
    }
    table.removedAt.erase(columnName);
    recordColumn(dataset, table, state, node.location.line, "DERIVE " + valueText(*node.value));
    addTarget(node, tableTarget(dataset, table, columnName));
    log(node, "DERIVE " + columnDisplayName(dataset, table, columnName) + " = " + valueText(*node.value) + ": " +
                  state.typeText() + (existing ? " (replaces the column)" : ""));
}

// JOIN adds the columns of a lookup table to every row: each row gets the one row of the
// other table with its key, or missing values if there is none (a left join). The key must
// be the same kind of value on both sides, no other column may be in both tables (pandas
// would rename them), and the key may appear only once in the lookup table, so no row is
// repeated. When the lookup file's key values are known, a repeated key is an error here;
// otherwise the generated program checks it before joining.
void SemanticAnalyzer::visit(const JoinStmt& node) {
    std::optional<TableResolution> left = resolver_.singleTable(node.target, "JOIN");
    std::optional<TableResolution> right = resolver_.singleTable(node.other, "JOIN");
    if (!left || !right) {
        return;
    }
    DatasetSymbol& dataset = *left->dataset;
    TableState& table = *left->tables.front();
    DatasetSymbol& otherDataset = *right->dataset;
    TableState& other = *right->tables.front();
    otherDataset.joined = true;  // read as a lookup table, whether or not the JOIN is correct
    if (&table == &other) {
        diagnostics_.error(Phase::Semantic, node.other.location, "JOIN needs two different tables");
        return;
    }
    const int errorsBefore = diagnostics_.errorCount();
    const ColumnState* key = resolver_.column(dataset, table, node.key, node.target.location);
    const ColumnState* otherKey = resolver_.column(otherDataset, other, node.key, node.other.location);
    if (!key || !otherKey) {
        return;
    }
    const std::string name = columnDisplayName(dataset, table, node.key);
    const std::string otherName = columnDisplayName(otherDataset, other, node.key);
    if (key->type != otherKey->type) {
        diagnostics_.error(Phase::Type, node.location,
                           "JOIN compares " + std::string(columnTypeName(key->type)) + " column " + name + " with " +
                               columnTypeName(otherKey->type) + " column " + otherName + ": no row would match",
                           "both keys must hold the same kind of value");
    } else if (key->type == ColumnType::Float || key->type == ColumnType::DateTime) {
        diagnostics_.error(
            Phase::Type, node.location,
            "JOIN needs an INTEGER, STRING or BOOLEAN key, but " + name + " is " + columnTypeName(key->type),
            key->type == ColumnType::Float ? "equal decimals can print differently; DERIVE an integer key"
                                           : "DERIVE a key from its parts, such as YEAR and MONTH");
    }
    std::vector<ColumnState> added;
    std::vector<std::string> addedNames;
    for (const ColumnState& column : other.columns) {
        if (column.name == node.key) {
            continue;
        }
        if (table.column(column.name)) {
            diagnostics_.error(Phase::Semantic, node.location,
                               "column '" + column.name + "' is in both " + columnDisplayName(dataset, table, "") +
                                   " and " + columnDisplayName(otherDataset, other, ""),
                               "DROP it from one of them first; a join would keep both under other names");
            continue;
        }
        ColumnState joined = column;
        joined.nullable = true;  // rows whose key is missing or not in the lookup table
        joined.exact = false;
        joined.valuesChanged = true;
        joined.rowsRemoved = true;
        added.push_back(joined);
        addedNames.push_back(column.name);
    }
    const bool valuesKnown = !other.schema.released && other.schema.format == TableFormat::Csv &&
                             other.schema.valuesKnown && otherKey->exact && !otherKey->valuesChanged;
    if (valuesKnown) {
        std::map<std::string, std::vector<size_t>> rows;
        const std::vector<std::string> values = readColumnValues(other.schema, node.key);
        for (size_t i = 0; i < values.size(); ++i) {
            if (!values[i].empty()) {
                rows[values[i]].push_back(i);
            }
        }
        for (const std::string& value : values) {
            const std::vector<size_t>& found = value.empty() ? std::vector<size_t>{} : rows[value];
            if (found.size() > 1) {
                diagnostics_.error(Phase::Contract, node.other.location,
                                   "JOIN needs each " + otherName + " once, but \"" + value + "\" is in rows " +
                                       std::to_string(found[0] + 1) + " and " + std::to_string(found[1] + 1) + " of " +
                                       other.schema.file,
                                   "each row of " + columnDisplayName(dataset, table, "") +
                                       " would be repeated; remove the repeated rows of the lookup table");
                break;
            }
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }
    for (const ColumnState& column : added) {
        table.columns.push_back(column);
        table.removedAt.erase(column.name);
        recordColumn(dataset, table, column, node.location.line, "JOIN " + columnDisplayName(otherDataset, other, ""));
    }
    Target target = tableTarget(dataset, table, node.key);
    target.args = {otherDataset.name, otherDataset.kind == DatasetKind::TableSet ? other.name : ""};
    target.args.insert(target.args.end(), addedNames.begin(), addedNames.end());
    addTarget(node, target);
    log(node, "JOIN " + columnDisplayName(dataset, table, "") + " WITH " + columnDisplayName(otherDataset, other, "") +
                  " ON " + node.key + ": adds " + (addedNames.empty() ? "no column" : join(addedNames)) +
                  (valuesKnown ? " (every key of the lookup table is different)" : " (keys checked when it runs)"));
}

// AGGREGATE makes one row per group of BY values; rows with a missing BY value belong to
// no group (as in pandas' groupby). What is known about each new column:
//   COUNT  INTEGER, at least 1, never missing
//   SUM    the column's type, never missing (a group without values sums to 0); at least 0
//          if every value is, at most 0 if every value is
//   MEAN   FLOAT; MIN and MAX the column's type: within the column's range, missing if a
//          group has no value of the column
void SemanticAnalyzer::visit(const AggregateStmt& node) {
    std::optional<TableResolution> resolution = resolver_.singleTable(node.target, "AGGREGATE");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    TableState& table = *resolution->tables.front();
    const int errorsBefore = diagnostics_.errorCount();
    std::vector<ColumnState> columns;
    std::vector<std::string> names;
    std::vector<std::string> types;
    const auto addName = [&](const std::string& name, SourceLocation location) {
        if (contains(names, name)) {
            diagnostics_.error(Phase::Semantic, location, "column '" + name + "' is named twice in AGGREGATE");
            return false;
        }
        names.push_back(name);
        return true;
    };
    for (const std::string& name : node.by) {
        ColumnState* column = resolver_.column(dataset, table, name, node.target.location);
        if (column && addName(name, node.target.location)) {
            ColumnState group = *column;
            group.nullable = false;
            group.exact = false;
            group.valuesChanged = true;
            group.rowsRemoved = true;
            columns.push_back(group);
        }
    }
    const double infinity = std::numeric_limits<double>::infinity();
    for (const AggregateItem& item : node.items) {
        ColumnState result{item.name, ColumnType::Integer, false, Range{1, infinity}, false, true, ""};
        result.rowsRemoved = true;
        if (item.function != "COUNT") {
            const ColumnState* column = resolver_.column(dataset, table, item.column, item.location);
            if (!column) {
                continue;
            }
            if (!isNumeric(column->type)) {
                diagnostics_.error(Phase::Type, item.location,
                                   item.function + " needs a numeric column, but '" + item.column + "' is " +
                                       columnTypeName(column->type),
                                   "COUNT AS name counts the rows of each group");
                continue;
            }
            const Range& range = column->range;
            result.type = item.function == "MEAN" ? ColumnType::Float : column->type;
            if (item.function == "SUM") {
                result.range = {range.min >= 0 ? 0 : -infinity, range.max <= 0 ? 0 : infinity};
            } else {
                result.nullable = column->nullable;
                result.range = range;
            }
        }
        if (addName(item.name, item.location)) {
            columns.push_back(result);
            types.push_back(columnTypeName(result.type));
        }
    }
    if (diagnostics_.errorCount() > errorsBefore) {
        return;
    }
    for (const ColumnState& column : table.columns) {
        if (!contains(names, column.name)) {
            table.removedAt[column.name] = node.location.line;
        }
    }
    table.columns = columns;
    for (const ColumnState& column : columns) {
        recordColumn(dataset, table, column, node.location.line, "AGGREGATE BY " + join(node.by));
    }
    Target target = tableTarget(dataset, table);
    target.args = types;
    addTarget(node, target);
    std::vector<std::string> described;
    for (const ColumnState& column : columns) {
        described.push_back(column.name + " " + column.typeText());
    }
    log(node, "AGGREGATE " + columnDisplayName(dataset, table, "") + " BY " + join(node.by) + ": " + join(described));
}

void SemanticAnalyzer::visit(const SaveStmt& node) {
    if (node.kind == DataKind::Image || node.kind == DataKind::ImageSet) {
        saveImages(node);
        return;
    }
    const std::optional<TableResolution> resolution = node.kind == DataKind::Table
                                                          ? resolver_.singleTable(node.target, "SAVE")
                                                          : resolver_.tables(node.target, "SAVE_TABLES");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    dataset.saved = true;  // also when this SAVE has an error: no extra "never saved" warning
    if (node.kind == DataKind::TableSet && dataset.kind != DatasetKind::TableSet) {
        diagnostics_.error(Phase::Type, node.location,
                           "SAVE_TABLES expects a TABLESET, but '" + dataset.name + "' is a TABLE",
                           "use SAVE " + dataset.name + " AS \"file.csv\";");
        return;
    }
    std::vector<std::string> saved;
    for (TableState* table : resolution->tables) {
        SavedTable written{node.kind == DataKind::TableSet ? node.path + "/" + table->name : node.path,
                           dataset.kind == DatasetKind::TableSet ? dataset.name + "." + table->name : dataset.name,
                           {}};
        for (const ColumnState& column : table->columns) {
            recordColumn(dataset, *table, column, node.location.line, "saved to \"" + node.path + "\"");
            written.columns.push_back({column.name, columnTypeName(column.type), column.nullable, column.range.min,
                                       column.range.max, isNumeric(column.type) && column.range.isKnown()});
        }
        result_.saved.push_back(written);
        addTarget(node, tableTarget(dataset, *table));
        saved.push_back(columnDisplayName(dataset, *table, "") + " (" + join(table->columnNames()) + ")");
    }
    log(node, "SAVE to \"" + node.path + "\": " + join(saved));
}

// ---------- Contracts ----------

void SemanticAnalyzer::visit(const ExpectStmt& node) {
    switch (node.constraint) {
        case ConstraintKind::Channels:
        case ConstraintKind::MinWidth:
        case ConstraintKind::MinHeight: {
            DatasetSymbol* images = resolver_.imageDataset(node.target.parts.front(), node.target.location,
                                                           "EXPECT ... " + constraintText(node));
            if (images) {
                contracts_.imageContract(node, *images);
            }
            return;
        }
        case ConstraintKind::MatchRate: {
            DatasetSymbol* linked = resolver_.dataset(node.target.parts.front(), node.target.location);
            if (linked && linked->kind != DatasetKind::Dataset) {
                diagnostics_.error(Phase::Type, node.target.location,
                                   "MATCH_RATE needs a DATASET made by LINK, but '" + linked->name + "' is a " +
                                       datasetKindName(linked->kind));
                return;
            }
            if (linked) {
                // The run-time check must match the same images LINK matched, so it is
                // told whether broken files were removed before the LINK.
                Target target{linked->name, "", "", {linked->link.images, linked->link.table, linked->link.column}};
                if (linked->link.skipsBroken) {
                    target.args.push_back("SKIP_BROKEN");
                }
                contracts_.matchRate(node, *linked, target);
                addTarget(node, target);
            }
            return;
        }
        case ConstraintKind::SchemaSameAs: {
            std::optional<TableResolution> left = resolver_.singleTable(node.target, "SCHEMA_SAME_AS");
            std::optional<TableResolution> right = resolver_.singleTable(node.other, "SCHEMA_SAME_AS");
            if (left && right) {
                contracts_.schemaSameAs(
                    node, columnDisplayName(*left->dataset, *left->tables.front(), ""), *left->tables.front(),
                    columnDisplayName(*right->dataset, *right->tables.front(), ""), *right->tables.front());
            }
            return;
        }
        default: expectColumns(node); return;
    }
}

void SemanticAnalyzer::expectColumns(const ExpectStmt& node) {
    std::optional<ColumnResolution> resolution = resolver_.columns(node.target, "EXPECT");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    std::optional<ColumnResolution> other;
    if (node.constraint == ConstraintKind::References) {
        other = resolver_.singleColumn(node.other, "REFERENCES");
        if (!other) {
            return;
        }
    }
    for (const ColumnMatch& match : resolution->matched) {
        const std::string name = columnDisplayName(dataset, *match.table, match.column->name);
        Target target = tableTarget(dataset, *match.table, match.column->name);
        if (other) {
            const ColumnMatch& key = other->matched.front();
            const DatasetSymbol& owner = *other->dataset;
            target.args = {owner.name, owner.kind == DatasetKind::TableSet ? key.table->name : "", key.column->name};
            contracts_.references(node, name, target, *match.table, *match.column,
                                  columnDisplayName(owner, *key.table, key.column->name), *key.table, *key.column);
        } else {
            contracts_.columnContract(node, name, target, *match.table, *match.column);
        }
        addTarget(node, target);
    }
}

// ---------- Image statements ----------

void SemanticAnalyzer::normalizeImages(const NormalizeStmt& node) {
    DatasetSymbol* images =
        resolver_.imageDataset(node.target.parts.front(), node.target.location, "NORMALIZE ... RANGE");
    if (!images || !decodable(*images, node)) {
        return;
    }
    if (node.target.parts.size() != 1) {
        diagnostics_.error(Phase::Semantic, node.target.location, "NORMALIZE ... RANGE works on a whole image dataset",
                           "write NORMALIZE " + images->name + " RANGE 0 1;");
        return;
    }
    const double low = std::stod(node.low);
    const double high = std::stod(node.high);
    if (low >= high) {
        diagnostics_.error(Phase::Semantic, node.location, "RANGE " + node.low + " " + node.high + " is empty",
                           "write the smaller number first");
        return;
    }
    ImageState& state = images->images;
    Target target{images->name, "", "", {}};
    if (state.dtype == "FLOAT32" && state.values.min == low && state.values.max == high) {
        addTarget(node, target, "pixels are already FLOAT32 in " + state.values.text());
        log(node, "NORMALIZE " + images->name + " has no effect");
        return;
    }
    if (state.dtype == "MIXED") {
        diagnostics_.error(Phase::Type, node.location,
                           images->name + " mixes 8-bit and 16-bit files, so their pixels have no single range",
                           "load 8-bit and 16-bit images as separate datasets");
        return;
    }
    target.args = {formatNumber(state.values.min), formatNumber(state.values.max)};
    state.dtype = "FLOAT32";
    state.values = {low, high};
    state.normalizedLine = node.location.line;
    recordImages(*images, node.location.line, "NORMALIZE RANGE " + node.low + " " + node.high);
    addTarget(node, target);
    log(node, "NORMALIZE " + images->name + ": pixels " + target.args[0] + ".." + target.args[1] + " -> FLOAT32 " +
                  state.values.text());
}

void SemanticAnalyzer::visit(const ResizeStmt& node) {
    DatasetSymbol* images = resolver_.imageDataset(node.dataset, node.location, "RESIZE");
    if (!images || !decodable(*images, node)) {
        return;
    }
    if (node.width <= 0 || node.height <= 0 || node.width > 65535 || node.height > 65535) {
        diagnostics_.error(Phase::Semantic, node.location, "RESIZE needs a width and height from 1 to 65535");
        return;
    }
    const Target target{images->name, "", "", {}};
    const std::string size = std::to_string(node.width) + "x" + std::to_string(node.height);
    std::vector<ImageShape>& shapes = images->images.shapes;
    const bool sized = std::all_of(shapes.begin(), shapes.end(), [&](const ImageShape& shape) {
        return shape.width == node.width && shape.height == node.height;
    });
    if (sized) {
        addTarget(node, target, "every image is already " + size);
        log(node, "RESIZE " + images->name + " has no effect");
        return;
    }
    for (ImageShape& shape : shapes) {
        shape.width = node.width;
        shape.height = node.height;
    }
    recordImages(*images, node.location.line, "RESIZE TO " + size + (node.keepAspect ? " KEEP_ASPECT" : ""));
    addTarget(node, target);
    log(node, "RESIZE " + images->name + ": every image becomes " + size);
}

void SemanticAnalyzer::visit(const GrayscaleStmt& node) {
    DatasetSymbol* images = resolver_.imageDataset(node.dataset, node.location, "GRAYSCALE");
    if (!images || !decodable(*images, node)) {
        return;
    }
    const Target target{images->name, "", "", {}};
    std::vector<ImageShape>& shapes = images->images.shapes;
    if (std::all_of(shapes.begin(), shapes.end(), [](const ImageShape& shape) { return shape.channels == 1; })) {
        addTarget(node, target, "every image already has 1 channel");
        log(node, "GRAYSCALE " + images->name + " has no effect");
        return;
    }
    for (ImageShape& shape : shapes) {
        shape.channels = 1;
    }
    recordImages(*images, node.location.line, "GRAYSCALE");
    addTarget(node, target);
    log(node, "GRAYSCALE " + images->name + ": every image has 1 channel");
}

void SemanticAnalyzer::visit(const CompressStmt& node) {
    DatasetSymbol* images = resolver_.imageDataset(node.dataset, node.location, "COMPRESS");
    if (!images) {
        return;
    }
    if (node.quality < 1 || node.quality > 100) {
        diagnostics_.error(Phase::Semantic, node.location, "QUALITY must be from 1 to 100");
        return;
    }
    const Target target{images->name, "", "", {}};
    if (images->images.quality == node.quality) {
        addTarget(node, target, "quality is already " + std::to_string(node.quality));
        return;
    }
    images->images.quality = node.quality;
    recordImages(*images, node.location.line, "COMPRESS QUALITY " + std::to_string(node.quality));
    addTarget(node, target);
    log(node, "COMPRESS " + images->name + ": JPG files are written with quality " + std::to_string(node.quality));
}

void SemanticAnalyzer::visit(const ConvertStmt& node) {
    DatasetSymbol* images = resolver_.imageDataset(node.dataset, node.location, "CONVERT");
    if (!images) {
        return;
    }
    const Target target{images->name, "", "", {}};
    if (images->images.format == node.format) {
        addTarget(node, target, "the output format is already " + node.format);
        log(node, "CONVERT " + images->name + " has no effect");
        return;
    }
    images->images.format = node.format;
    recordImages(*images, node.location.line, "CONVERT TO " + node.format);
    addTarget(node, target);
    log(node, "CONVERT " + images->name + ": files are written as " + node.format);
}

// Kept even when no broken file was found: a file can have a valid header and
// still fail to decode, and REMOVE_CORRUPT makes the program skip it at run time.
void SemanticAnalyzer::visit(const RemoveCorruptStmt& node) {
    DatasetSymbol* images = resolver_.imageDataset(node.dataset, node.location, "REMOVE_CORRUPT");
    if (!images) {
        return;
    }
    std::vector<ImageShape>& shapes = images->images.shapes;
    const int corrupt = images->images.corruptCount();
    shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [](const ImageShape& shape) { return shape.corrupt; }),
                 shapes.end());
    images->images.brokenRemoved = true;
    recordImages(*images, node.location.line,
                 "REMOVE_CORRUPT (" + std::to_string(corrupt) + " broken file" + (corrupt == 1 ? "" : "s") + ")");
    addTarget(node, {images->name, "", "", {}});
    log(node, "REMOVE_CORRUPT " + images->name + ": " + std::to_string(corrupt) +
                  " file(s) with a broken header removed; files that fail later are skipped at run time");
}

void SemanticAnalyzer::saveImages(const SaveStmt& node) {
    const bool single = node.kind == DataKind::Image;
    const std::string statement = single ? "SAVE_IMAGE" : "SAVE_IMAGES";
    DatasetSymbol* images = resolver_.imageDataset(node.target.parts.front(), node.target.location, statement);
    if (!images) {
        return;
    }
    images->saved = true;  // also when this save has an error: no extra "never saved" warning
    if (single != (images->kind == DatasetKind::Image)) {
        diagnostics_.error(Phase::Type, node.location,
                           statement + " expects " + (single ? "an IMAGE" : "an IMAGESET") + ", but '" + images->name +
                               "' is an " + datasetKindName(images->kind),
                           single ? "use SAVE_IMAGES ... TO \"folder/\";" : "use SAVE_IMAGE ... AS \"file.png\";");
        return;
    }
    if (!decodable(*images, node)) {
        return;
    }

    // Can the chosen file format hold these pixels?
    const ImageState& state = images->images;
    // With SOURCE every file keeps its own format, so only .jpg files become JPG.
    const auto becomesJpg = [&](const ImageShape& shape) {
        return state.format == "JPG" || (state.format == "SOURCE" && endsWithJpg(shape.path));
    };
    const bool writesJpg = std::any_of(state.shapes.begin(), state.shapes.end(), becomesJpg);
    const std::string formatName = state.format == "SOURCE" ? "their original PNG/JPG files" : state.format;
    if (state.format != "NPY" && state.dtype == "FLOAT32") {
        diagnostics_.error(Phase::Type, node.location,
                           "cannot save FLOAT32 pixels as " + formatName + ": NORMALIZE at line " +
                               std::to_string(state.normalizedLine) + " scaled them to " + state.values.text(),
                           "image files store 8-bit pixels, so these would come out almost black; add CONVERT " +
                               images->name + " TO NPY; to keep the float values");
        return;
    }
    const int alpha =
        static_cast<int>(std::count_if(state.shapes.begin(), state.shapes.end(),
                                       [&](const ImageShape& s) { return s.channels == 4 && becomesJpg(s); }));
    if (alpha > 0) {
        diagnostics_.error(Phase::Type, node.location,
                           "JPG cannot store transparency, but " + std::to_string(alpha) + " image(s) have 4 channels",
                           "add CONVERT " + images->name + " TO PNG; or GRAYSCALE " + images->name + ";");
        return;
    }
    if (writesJpg && state.dtype != "UINT8") {
        diagnostics_.error(Phase::Type, node.location, "JPG stores 8-bit pixels, but the images are " + state.dtype,
                           "add CONVERT " + images->name + " TO PNG;");
        return;
    }
    if (state.quality > 0 && !writesJpg) {
        diagnostics_.warning(Phase::Semantic, node.location,
                             "QUALITY only affects JPG files, so COMPRESS changes nothing here");
    }
    recordImages(*images, node.location.line, "saved to \"" + node.path + "\" as " + formatName);
    addTarget(node, {images->name, "", "", {}});
    log(node, statement + " " + images->name + " to \"" + node.path + "\" as " + formatName);
}

void SemanticAnalyzer::visit(const LinkStmt& node) {
    if (const DatasetSymbol* existing = symbols_.lookup(node.dataset)) {
        diagnostics_.error(
            Phase::Semantic, node.location,
            "dataset '" + node.dataset + "' is already defined at line " + std::to_string(existing->definedAt.line));
        return;
    }
    DatasetSymbol* images = resolver_.imageDataset(node.images, node.location, "LINK");
    DatasetSymbol* labels = resolver_.dataset(node.table, node.location);
    if (!images || !labels) {
        return;
    }
    if (labels->kind != DatasetKind::Table) {
        diagnostics_.error(
            Phase::Type, node.location,
            "LINK needs a TABLE of labels, but '" + labels->name + "' is a " + datasetKindName(labels->kind));
        return;
    }
    TableState& table = labels->tables.front();
    ColumnState* column = resolver_.column(*labels, table, node.column, node.location);
    if (!column) {
        return;
    }
    if (column->type != ColumnType::String) {
        diagnostics_.error(
            Phase::Type, node.location,
            "LINK matches file names, but " + labels->name + "." + column->name + " is " + columnTypeName(column->type),
            "the column must hold names such as \"cats/001.jpg\"");
        return;
    }

    LinkState link;
    const bool parquet = table.schema.format == TableFormat::Parquet;
    if (!parquet && column->exact && !column->valuesChanged && images->imageSchema.namesKnown) {
        link = linkImages(images->images, readColumnValues(table.schema, column->name));
    }
    link.images = images->name;
    link.table = labels->name;
    link.column = column->name;
    link.skipsBroken = images->images.brokenRemoved;

    DatasetSymbol dataset;
    dataset.name = node.dataset;
    dataset.kind = DatasetKind::Dataset;
    dataset.definedAt = node.location;
    dataset.link = link;
    const std::string summary = link.exact ? std::to_string(link.matched) + " of " + std::to_string(link.imageCount) +
                                                 " images match one of " + std::to_string(link.labelCount) + " labels"
                                : parquet ? "only the footer of " + table.schema.file +
                                                " is read at compile time, so matching is checked at run time"
                                          : "labels changed before LINK, so matching is checked at run time";
    result_.lineage.record(node.dataset, node.location.line,
                           "LINK " + images->name + " WITH " + labels->name + " ON " + column->name, summary);
    log(node, "LINK " + node.dataset + ": " + summary);
    symbols_.insert(std::move(dataset));
}

// ---------- FILTER conditions ----------

void SemanticAnalyzer::visit(const ComparisonExpr& node) {
    truth_ = Truth::Unknown;
    const ColumnState* column = resolver_.column(*filterDataset_, *filterTable_, node.column, node.location);
    if (!column) {
        return;
    }
    const std::string type = columnTypeName(column->type);
    if (!sameKind(column->type, node.value)) {
        diagnostics_.error(Phase::Type, node.location,
                           "cannot compare " + type + " column '" + node.column + "' with " + describe(node.value),
                           valueHint(*column));
        return;
    }
    if (!isNumeric(column->type)) {
        if (node.op != "==" && node.op != "!=") {
            diagnostics_.error(
                Phase::Type, node.location,
                "operator '" + node.op + "' needs a numeric column, but '" + node.column + "' is " + type,
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
        truth_ = (left == Truth::NeverTrue || right == Truth::NeverTrue)     ? Truth::NeverTrue
                 : (left == Truth::AlwaysTrue && right == Truth::AlwaysTrue) ? Truth::AlwaysTrue
                                                                             : Truth::Unknown;
    } else {
        truth_ = (left == Truth::AlwaysTrue || right == Truth::AlwaysTrue) ? Truth::AlwaysTrue
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
void SemanticAnalyzer::refine(const Expr& condition, TableState& table, std::vector<std::string>& learned) {
    if (const auto* logical = dynamic_cast<const LogicalExpr*>(&condition)) {
        if (logical->op == LogicalOp::And) {
            refine(*logical->left, table, learned);
            refine(*logical->right, table, learned);
        }
        return;
    }
    const auto* comparison = dynamic_cast<const ComparisonExpr*>(&condition);
    if (!comparison) {
        return;  // NOT: nothing is certain
    }
    ColumnState* column = table.column(comparison->column);
    column->nullable = false;  // a missing value never passes a comparison
    if (isNumeric(column->type)) {
        const double value = std::stod(comparison->value.value);
        const std::string& op = comparison->op;
        if (op == ">" || op == ">=" || op == "==") {
            column->range.min = std::max(column->range.min, value);
        }
        if (op == "<" || op == "<=" || op == "==") {
            column->range.max = std::min(column->range.max, value);
        }
    }
    learned.push_back(column->name + " is " + column->typeText());
}
