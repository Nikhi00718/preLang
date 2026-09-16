#pragma once

#include "Ast.h"

#include <ostream>
#include <string>
#include <unordered_map>

namespace prep {

enum class ValueType { Unknown, Number, String, Boolean };

std::string valueTypeName(ValueType type);
ValueType valueTypeFromName(const std::string& name);
ValueType valueTypeFromLiteral(const Literal& literal);

struct ColumnSymbol {
    ValueType type = ValueType::Unknown;
    bool notNull = false;
};

struct DatasetSymbol {
    std::string file;
    std::unordered_map<std::string, ColumnSymbol> columns;
};

// The symbol table is the small interface used by semantic analysis.
class SymbolTable {
public:
    bool addDataset(const std::string& name, const std::string& file);
    DatasetSymbol* findDataset(const std::string& name);
    const DatasetSymbol* findDataset(const std::string& name) const;

    bool declareColumn(const std::string& dataset, const std::string& column,
                       ValueType type, bool notNull, std::string& problem);
    const ColumnSymbol* findColumn(const std::string& dataset,
                                   const std::string& column) const;

    void print(std::ostream& out) const;

private:
    std::unordered_map<std::string, DatasetSymbol> datasets_;
};

}  // namespace prep
