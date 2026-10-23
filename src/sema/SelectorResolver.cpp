#include "sema/SelectorResolver.h"

#include <algorithm>

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

bool passesFilter(SelectorFilter filter, const ColumnState& column) {
    switch (filter) {
        case SelectorFilter::None:     return true;
        case SelectorFilter::Numeric:  return isNumeric(column.type);
        case SelectorFilter::String:   return column.type == ColumnType::String;
        case SelectorFilter::Boolean:  return column.type == ColumnType::Boolean;
        case SelectorFilter::Nullable: return column.nullable;
    }
    return false;
}

std::string article(DatasetKind kind) {
    return kind == DatasetKind::Image || kind == DatasetKind::ImageSet ? "an " : "a ";
}

}  // namespace

std::string columnDisplayName(const DatasetSymbol& dataset, const TableState& table, const std::string& column) {
    const std::string prefix = dataset.kind == DatasetKind::TableSet ? dataset.name + "." + table.name : dataset.name;
    return column.empty() ? prefix : prefix + "." + column;
}

SelectorResolver::SelectorResolver(SymbolTable& symbols, Diagnostics& diagnostics,
                                   const std::set<std::string>& unreadable)
    : symbols_(symbols), diagnostics_(diagnostics), unreadable_(unreadable) {}

DatasetSymbol* SelectorResolver::dataset(const std::string& name, SourceLocation location) {
    if (DatasetSymbol* found = symbols_.lookup(name)) {
        return found;
    }
    if (unreadable_.count(name) == 0) {  // a failed LOAD was already reported
        std::string hint = suggestClosest(name, symbols_.names());
        if (hint.empty()) {
            hint = "create it first, e.g. LOAD \"file.csv\" AS " + name + ";";
        }
        diagnostics_.error(Phase::Semantic, location, "dataset '" + name + "' is not defined", hint);
    }
    return nullptr;
}

DatasetSymbol* SelectorResolver::imageDataset(const std::string& name, SourceLocation location,
                                              const std::string& statement) {
    DatasetSymbol* found = dataset(name, location);
    if (found && !found->isImage()) {
        diagnostics_.error(Phase::Type, location,
                           statement + " expects an IMAGE or IMAGESET, but '" + name + "' is " + article(found->kind) +
                               datasetKindName(found->kind));
        return nullptr;
    }
    return found;
}

bool SelectorResolver::requireTabular(const DatasetSymbol& found, SourceLocation location,
                                      const std::string& statement) {
    if (found.isTabular()) {
        return true;
    }
    std::string hint;
    if (found.isImage() && statement == "NORMALIZE") {
        hint = "images are normalized with NORMALIZE " + found.name + " RANGE 0 1;";
    }
    diagnostics_.error(Phase::Type, location,
                       statement + " expects a TABLE or TABLESET, but '" + found.name + "' is " + article(found.kind) +
                           datasetKindName(found.kind),
                       hint);
    return false;
}

TableState* SelectorResolver::namedTable(DatasetSymbol& owner, const std::string& name, SourceLocation location) {
    if (TableState* table = owner.table(name)) {
        return table;
    }
    std::string hint = suggestClosest(name, owner.tableNames());
    if (hint.empty()) {
        hint = "tables of '" + owner.name + "': " + join(owner.tableNames());
    }
    diagnostics_.error(Phase::Semantic, location,
                       "table '" + name + "' does not exist in TABLESET '" + owner.name + "'", hint);
    return nullptr;
}

std::optional<TableResolution> SelectorResolver::tables(const Selector& selector, const std::string& statement) {
    DatasetSymbol* owner = dataset(selector.parts[0], selector.location);
    if (!owner || !requireTabular(*owner, selector.location, statement)) {
        return std::nullopt;
    }
    TableResolution resolution{owner, {}};
    if (owner->kind == DatasetKind::Table) {
        if (selector.parts.size() > 1) {
            diagnostics_.error(Phase::Semantic, selector.location,
                               "'" + owner->name + "' is a TABLE, so it has no inner tables",
                               "write just " + owner->name);
            return std::nullopt;
        }
        resolution.tables.push_back(&owner->tables.front());
    } else if (selector.parts.size() == 1 || selector.parts[1] == "*") {
        for (TableState& table : owner->tables) {
            resolution.tables.push_back(&table);
        }
    } else if (TableState* table = namedTable(*owner, selector.parts[1], selector.location)) {
        resolution.tables.push_back(table);
    } else {
        return std::nullopt;
    }
    return resolution;
}

std::optional<TableResolution> SelectorResolver::singleTable(const Selector& selector, const std::string& statement) {
    std::optional<TableResolution> resolution = tables(selector, statement);
    if (resolution && resolution->tables.size() != 1) {
        diagnostics_.error(Phase::Semantic, selector.location, statement + " works on one table at a time",
                           "write " + resolution->dataset->name + ".<table>, e.g. " + resolution->dataset->name + "." +
                               resolution->dataset->tables.front().name);
        return std::nullopt;
    }
    return resolution;
}

std::optional<ColumnResolution> SelectorResolver::columns(const Selector& selector, const std::string& statement) {
    if (selector.filter == SelectorFilter::Nullable) {
        // Which columns have missing values depends on the batch: the statement would act on other
        // columns of the next batch, which no certificate of the compiled program can stand in for.
        // FILL_NULL resolves WHERE NULLABLE itself (a fill of a column without missing values does nothing).
        diagnostics_.error(Phase::Semantic, selector.location,
                           "WHERE NULLABLE is allowed only in FILL_NULL: which columns have missing values depends "
                           "on the batch, so " +
                               statement + " would act on other columns of the next one",
                           "name the columns, or select them with WHERE TYPE");
        return std::nullopt;
    }
    DatasetSymbol* owner = dataset(selector.parts[0], selector.location);
    if (!owner || !requireTabular(*owner, selector.location, statement)) {
        return std::nullopt;
    }

    // A TABLE path is table.column; a TABLESET path is set.table.column.
    // A missing part means "*".
    std::string tablePart = owner->name;
    std::string columnPart = "*";
    if (owner->kind == DatasetKind::Table) {
        if (selector.parts.size() > 2) {
            diagnostics_.error(Phase::Semantic, selector.location, "'" + owner->name + "' is a TABLE",
                               "write " + owner->name + ".<column>");
            return std::nullopt;
        }
        if (selector.parts.size() == 2) {
            columnPart = selector.parts[1];
        }
    } else {
        tablePart = selector.parts.size() >= 2 ? selector.parts[1] : "*";
        if (selector.parts.size() == 3) {
            columnPart = selector.parts[2];
        }
    }

    std::vector<TableState*> tables;
    if (owner->kind == DatasetKind::Table) {
        tables.push_back(&owner->tables.front());
    } else if (tablePart == "*") {
        for (TableState& table : owner->tables) {
            tables.push_back(&table);
        }
    } else if (TableState* table = namedTable(*owner, tablePart, selector.location)) {
        tables.push_back(table);
    } else {
        return std::nullopt;
    }

    ColumnResolution resolution;
    resolution.dataset = owner;
    std::vector<std::string> missingIn;
    for (TableState* table : tables) {
        std::vector<ColumnState*> candidates;
        if (columnPart == "*") {
            for (ColumnState& column : table->columns) {
                candidates.push_back(&column);
            }
        } else if (ColumnState* named = table->column(columnPart)) {
            candidates.push_back(named);
        } else if (tables.size() == 1 && !selector.ifExists) {
            column(*owner, *table, columnPart, selector.location);  // reports the precise error
            return std::nullopt;
        } else {
            missingIn.push_back(columnDisplayName(*owner, *table, ""));
            continue;
        }
        for (ColumnState* column : candidates) {
            const std::string name = columnDisplayName(*owner, *table, column->name);
            if (contains(selector.except, column->name)) {
                resolution.skipped.push_back(name + " (EXCEPT)");
            } else if (!passesFilter(selector.filter, *column)) {
                resolution.skipped.push_back(name + " (" + column->typeText() + ")");
            } else {
                resolution.matched.push_back({table, column});
            }
        }
    }

    if (!missingIn.empty() && !selector.ifExists) {
        diagnostics_.error(Phase::Semantic, selector.location,
                           "column '" + columnPart + "' is missing in " + join(missingIn),
                           "add IF_EXISTS to skip tables that do not have the column");
        return std::nullopt;
    }
    for (const std::string& table : missingIn) {
        resolution.skipped.push_back(table + " (no column " + columnPart + ")");
    }
    if (resolution.matched.empty()) {
        Diagnostic& problem =
            diagnostics_.warning(Phase::Semantic, selector.location,
                                 "'" + selector.text() + "' matches no columns, so " + statement + " does nothing");
        problem.notes = resolution.skipped;
    }
    return resolution;
}

std::optional<ColumnResolution> SelectorResolver::singleColumn(const Selector& selector, const std::string& statement) {
    std::optional<ColumnResolution> resolution = columns(selector, statement);
    const bool wildcard = std::find(selector.parts.begin(), selector.parts.end(), "*") != selector.parts.end();
    if (resolution && (wildcard || resolution->matched.size() != 1)) {
        diagnostics_.error(Phase::Semantic, selector.location, statement + " needs one column, e.g. table.column");
        return std::nullopt;
    }
    return resolution;
}

ColumnState* SelectorResolver::column(const DatasetSymbol& owner, TableState& table, const std::string& name,
                                      SourceLocation location) {
    if (ColumnState* found = table.column(name)) {
        return found;
    }
    const std::string tableName = columnDisplayName(owner, table, "");
    const auto removed = table.removedAt.find(name);
    if (removed != table.removedAt.end()) {
        diagnostics_.error(
            Phase::Semantic, location,
            "column '" + name + "' was removed from '" + tableName + "' at line " + std::to_string(removed->second),
            "use the column before that line, or keep it in the SELECT/DROP");
        return nullptr;
    }
    std::string hint = suggestClosest(name, table.columnNames());
    if (hint.empty()) {
        hint = "columns of '" + tableName + "': " + join(table.columnNames());
    }
    diagnostics_.error(Phase::Semantic, location, "column '" + name + "' does not exist in '" + tableName + "'", hint);
    return nullptr;
}
