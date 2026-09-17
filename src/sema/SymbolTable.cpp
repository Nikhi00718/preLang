#include "sema/SymbolTable.h"

#include <algorithm>
#include <iomanip>
#include <utility>

ColumnState* DatasetSymbol::column(const std::string& columnName) {
    for (ColumnState& state : columns) {
        if (state.name == columnName) {
            return &state;
        }
    }
    return nullptr;
}

std::vector<std::string> DatasetSymbol::columnNames() const {
    std::vector<std::string> names;
    for (const ColumnState& state : columns) {
        names.push_back(state.name);
    }
    return names;
}

DatasetSymbol* SymbolTable::lookup(const std::string& name) {
    for (DatasetSymbol& symbol : datasets_) {
        if (symbol.name == name) {
            return &symbol;
        }
    }
    return nullptr;
}

const DatasetSymbol* SymbolTable::lookup(const std::string& name) const {
    for (const DatasetSymbol& symbol : datasets_) {
        if (symbol.name == name) {
            return &symbol;
        }
    }
    return nullptr;
}

void SymbolTable::insert(DatasetSymbol symbol) {
    datasets_.push_back(std::move(symbol));
}

std::vector<std::string> SymbolTable::names() const {
    std::vector<std::string> result;
    for (const DatasetSymbol& symbol : datasets_) {
        result.push_back(symbol.name);
    }
    return result;
}

void SymbolTable::print(std::ostream& out) const {
    for (const DatasetSymbol& symbol : datasets_) {
        out << "Dataset '" << symbol.name << "' (LOAD at line " << symbol.definedAt.line << ", file "
            << symbol.schema.file << ")\n";

        size_t nameWidth = 8;
        for (const ColumnState& column : symbol.columns) {
            nameWidth = std::max(nameWidth, column.name.size() + 2);
        }
        const int width = static_cast<int>(nameWidth);

        out << std::left << "  " << std::setw(width) << "COLUMN" << std::setw(10) << "TYPE" << std::setw(10)
            << "NULLABLE" << std::setw(16) << "RANGE" << "EXACT\n";
        for (const ColumnState& column : symbol.columns) {
            const std::string range = isNumeric(column.type) ? column.range.text() : "-";
            out << "  " << std::setw(width) << column.name << std::setw(10) << columnTypeName(column.type)
                << std::setw(10) << (column.nullable ? "yes" : "no") << std::setw(16) << range
                << (column.exact ? "yes" : "no") << '\n';
        }
        for (const auto& [name, line] : symbol.removedAt) {
            out << "  removed: " << name << " (line " << line << ")\n";
        }
        out << "  saved: " << (symbol.saved ? "yes" : "no") << "\n\n";
    }
}
