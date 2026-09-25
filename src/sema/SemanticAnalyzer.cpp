#include "sema/SemanticAnalyzer.h"

#include "ast/ASTPrinter.h"
#include "dataset/DatasetLinker.h"

#include <algorithm>
#include <cctype>
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
        const bool exact = (info.boundsExact || !info.hasRange) && info.missingKnown;
        ColumnState column{info.name, info.type, nullable, Range{}, exact, false, info.stringReason};
        if (isNumeric(info.type) && info.hasRange) {
            column.range = {info.min, info.max};
        }
        table.columns.push_back(column);
    }
    table.schema = std::move(schema);
    return table;
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
        if (!dataset.saved && dataset.kind != DatasetKind::Dataset) {
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
        Diagnostic& error = diagnostics_.error(Phase::Type, selector.location,
                                               rule + ": " + std::to_string(bad) + " of " +
                                                   std::to_string(matched.size()) + " columns matched by '" +
                                                   selector.text() + "' break it",
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
        diagnostics_.error(Phase::Semantic, node.location,
                           "dataset '" + node.dataset + "' is already defined at line " +
                               std::to_string(existing->definedAt.line),
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
            summary.push_back(schema.name + " (" + std::to_string(schema.rowCount) + " rows, " +
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
    std::vector<std::string> changed;
    std::vector<std::string> idle;
    for (TableState* table : resolution->tables) {
        const std::string tableName = columnDisplayName(dataset, *table, "");
        std::vector<std::string> nullable;
        for (const ColumnState& column : table->columns) {
            if (column.nullable) {
                nullable.push_back(column.name);
            }
        }
        if (nullable.empty()) {
            addTarget(node, tableTarget(dataset, *table), "no column of " + tableName + " has missing values here");
            idle.push_back(tableName);
            continue;
        }
        for (ColumnState& column : table->columns) {
            column.nullable = false;
            column.exact = false;
            recordColumn(dataset, *table, column, node.location.line, "DROP_NULL");
        }
        addTarget(node, tableTarget(dataset, *table));
        changed.push_back(tableName + " (rows with missing " + join(nullable) + ")");
    }
    if (changed.empty()) {
        diagnostics_.warning(Phase::Semantic, node.location,
                             "DROP_NULL has no effect: no column has missing values here");
    }
    log(node, "DROP_NULL " + (changed.empty() ? "has no effect" : "removes rows of " + join(changed)) +
                  (idle.empty() || changed.empty() ? "" : "; no effect on " + join(idle)));
}

void SemanticAnalyzer::visit(const FillNullStmt& node) {
    std::optional<ColumnResolution> resolution = resolver_.columns(node.target, "FILL_NULL");
    if (!resolution) {
        return;
    }
    DatasetSymbol& dataset = *resolution->dataset;
    const auto fits = [&](const ColumnState& column) { return canStore(column.type, node.value); };
    const auto hint = [](const ColumnState& column) { return valueHint(column); };
    if (!checkTypes(node.target, dataset, resolution->matched, fits,
                    "FILL_NULL with " + describe(node.value) + " needs a matching column type", hint)) {
        return;
    }

    std::vector<std::string> changed;
    std::vector<std::string> idle;
    for (const ColumnMatch& match : resolution->matched) {
        ColumnState& column = *match.column;
        const std::string name = columnDisplayName(dataset, *match.table, column.name);
        const Target target = tableTarget(dataset, *match.table, column.name);
        if (!column.nullable) {
            addTarget(node, target, name + " has no missing values here");
            idle.push_back(name);
            continue;
        }
        column.nullable = false;
        column.valuesChanged = true;
        if (node.value.kind == LiteralKind::Number) {
            const double value = std::stod(node.value.value);
            column.range.min = std::min(column.range.min, value);
            column.range.max = std::max(column.range.max, value);
        }
        recordColumn(dataset, *match.table, column, node.location.line, "FILL_NULL " + literalText(node.value));
        addTarget(node, target);
        changed.push_back(name);
    }
    if (changed.empty() && !idle.empty()) {
        diagnostics_.warning(Phase::Semantic, node.location,
                             "FILL_NULL has no effect: " +
                                 (idle.size() == 1 ? idle.front() + " has no missing values here"
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
        diagnostics_.warning(Phase::Semantic, node.location, "FILTER has no effect: the condition is true for every row",
                             "the compared columns have no missing values and their ranges always pass");
        addTarget(node, target, "the condition is true for every row of " + tableName);
        log(node, "FILTER " + tableName + " has no effect");
        return;
    }
    if (truth_ == Truth::NeverTrue) {
        diagnostics_.warning(Phase::Semantic, node.location, "FILTER removes every row: the condition is never true",
                             neverTrueReason_);
    }

    std::vector<std::string> learned;
    refine(*node.condition, *filterTable_, learned);
    for (ColumnState& column : filterTable_->columns) {
        column.exact = false;
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
        for (const ColumnState& column : table->columns) {
            recordColumn(dataset, *table, column, node.location.line, "saved to \"" + node.path + "\"");
        }
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
                const Target target{linked->name, "", "", {linked->link.images, linked->link.table, linked->link.column}};
                contracts_.matchRate(node, *linked, target);
                addTarget(node, target);
            }
            return;
        }
        case ConstraintKind::SchemaSameAs: {
            std::optional<TableResolution> left = resolver_.singleTable(node.target, "SCHEMA_SAME_AS");
            std::optional<TableResolution> right = resolver_.singleTable(node.other, "SCHEMA_SAME_AS");
            if (left && right) {
                contracts_.schemaSameAs(node, columnDisplayName(*left->dataset, *left->tables.front(), ""),
                                        *left->tables.front(),
                                        columnDisplayName(*right->dataset, *right->tables.front(), ""),
                                        *right->tables.front());
            }
            return;
        }
        default:
            expectColumns(node);
            return;
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
    DatasetSymbol* images = resolver_.imageDataset(node.target.parts.front(), node.target.location, "NORMALIZE ... RANGE");
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
    const int alpha = static_cast<int>(std::count_if(state.shapes.begin(), state.shapes.end(), [&](const ImageShape& s) {
        return s.channels == 4 && becomesJpg(s);
    }));
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
        diagnostics_.error(Phase::Semantic, node.location,
                           "dataset '" + node.dataset + "' is already defined at line " +
                               std::to_string(existing->definedAt.line));
        return;
    }
    DatasetSymbol* images = resolver_.imageDataset(node.images, node.location, "LINK");
    DatasetSymbol* labels = resolver_.dataset(node.table, node.location);
    if (!images || !labels) {
        return;
    }
    if (labels->kind != DatasetKind::Table) {
        diagnostics_.error(Phase::Type, node.location,
                           "LINK needs a TABLE of labels, but '" + labels->name + "' is a " +
                               datasetKindName(labels->kind));
        return;
    }
    TableState& table = labels->tables.front();
    ColumnState* column = resolver_.column(*labels, table, node.column, node.location);
    if (!column) {
        return;
    }
    if (column->type != ColumnType::String) {
        diagnostics_.error(Phase::Type, node.location,
                           "LINK matches file names, but " + labels->name + "." + column->name + " is " +
                               columnTypeName(column->type),
                           "the column must hold names such as \"cats/001.jpg\"");
        return;
    }

    LinkState link;
    const bool parquet = table.schema.format == TableFormat::Parquet;
    if (!parquet && column->exact && !column->valuesChanged) {
        link = linkImages(images->images, readColumnValues(table.schema, column->name));
    }
    link.images = images->name;
    link.table = labels->name;
    link.column = column->name;

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
            diagnostics_.error(Phase::Type, node.location,
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
