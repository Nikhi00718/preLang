#pragma once

#include "ast/AST.h"
#include "common/Diagnostics.h"
#include "sema/SymbolTable.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

// One column matched by a selector.
struct ColumnMatch {
    TableState* table;
    ColumnState* column;
};

struct ColumnResolution {
    DatasetSymbol* dataset = nullptr;
    std::vector<ColumnMatch> matched;
    std::vector<std::string> skipped;  // e.g. "company.courses (no column age)", "company.staff.id (EXCEPT)"
};

struct TableResolution {
    DatasetSymbol* dataset = nullptr;
    std::vector<TableState*> tables;
};

// Turns selectors such as company.*.age or company.*.* WHERE TYPE NUMERIC into
// the concrete tables and columns they name, with clear errors for names
// that do not exist. `statement` names the statement in messages.
class SelectorResolver {
public:
    SelectorResolver(SymbolTable& symbols, Diagnostics& diagnostics, const std::set<std::string>& unreadable);

    DatasetSymbol* dataset(const std::string& name, SourceLocation location);
    DatasetSymbol* imageDataset(const std::string& name, SourceLocation location, const std::string& statement);

    // t, ts, ts.* or ts.name
    std::optional<TableResolution> tables(const Selector& selector, const std::string& statement);
    // Exactly one table: t or ts.name
    std::optional<TableResolution> singleTable(const Selector& selector, const std::string& statement);
    // Columns, after applying WHERE, EXCEPT and IF_EXISTS
    std::optional<ColumnResolution> columns(const Selector& selector, const std::string& statement);
    // One named column: t.col or ts.name.col
    std::optional<ColumnResolution> singleColumn(const Selector& selector, const std::string& statement);

    ColumnState* column(const DatasetSymbol& dataset, TableState& table, const std::string& name,
                        SourceLocation location);

private:
    SymbolTable& symbols_;
    Diagnostics& diagnostics_;
    const std::set<std::string>& unreadable_;  // datasets whose files could not be read (already reported)

    bool requireTabular(const DatasetSymbol& dataset, SourceLocation location, const std::string& statement);
    TableState* namedTable(DatasetSymbol& dataset, const std::string& name, SourceLocation location);
};

// "students.age" for a TABLE, "company.staff.age" for a TABLESET
std::string columnDisplayName(const DatasetSymbol& dataset, const TableState& table, const std::string& column);
