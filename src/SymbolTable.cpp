#include "SymbolTable.h"
#include "Diagnostics.h"

#include <algorithm>
#include <vector>

namespace prep {

std::string valueTypeName(ValueType type) {
    switch (type) {
        case ValueType::Unknown: return "UNKNOWN";
        case ValueType::Number: return "NUMBER";
        case ValueType::String: return "STRING";
        case ValueType::Boolean: return "BOOLEAN";
    }
    return "UNKNOWN";
}

ValueType valueTypeFromName(const std::string& name) {
    if (name == "NUMBER") return ValueType::Number;
    if (name == "STRING") return ValueType::String;
    if (name == "BOOLEAN") return ValueType::Boolean;
    return ValueType::Unknown;
}

ValueType valueTypeFromLiteral(const Literal& literal) {
    switch (literal.kind) {
        case LiteralKind::Number: return ValueType::Number;
        case LiteralKind::String: return ValueType::String;
        case LiteralKind::Boolean: return ValueType::Boolean;
    }
    return ValueType::Unknown;
}

bool SymbolTable::addDataset(const std::string& name, const std::string& file) {
    return datasets_.emplace(name, DatasetSymbol{file, {}}).second;
}

DatasetSymbol* SymbolTable::findDataset(const std::string& name) {
    auto found = datasets_.find(name);
    return found == datasets_.end() ? nullptr : &found->second;
}

const DatasetSymbol* SymbolTable::findDataset(const std::string& name) const {
    auto found = datasets_.find(name);
    return found == datasets_.end() ? nullptr : &found->second;
}

bool SymbolTable::declareColumn(const std::string& dataset, const std::string& column,
                                ValueType type, bool notNull, std::string& problem) {
    DatasetSymbol* data = findDataset(dataset);
    if (data == nullptr) {
        problem = "Dataset " + quoteText(dataset) + " has not been loaded.";
        return false;
    }

    auto found = data->columns.find(column);
    if (found == data->columns.end()) {
        data->columns[column] = {type, notNull};
        return true;
    }

    ColumnSymbol& symbol = found->second;
    if (symbol.type != ValueType::Unknown && type != ValueType::Unknown && symbol.type != type) {
        problem = "Column " + dataset + "." + column + " was given two different types.";
        return false;
    }
    if (symbol.type == ValueType::Unknown) symbol.type = type;
    symbol.notNull = symbol.notNull || notNull;
    return true;
}

const ColumnSymbol* SymbolTable::findColumn(const std::string& dataset,
                                            const std::string& column) const {
    const DatasetSymbol* data = findDataset(dataset);
    if (data == nullptr) return nullptr;
    auto found = data->columns.find(column);
    return found == data->columns.end() ? nullptr : &found->second;
}

void SymbolTable::print(std::ostream& out) const {
    std::vector<std::string> datasets;
    for (const auto& item : datasets_) datasets.push_back(item.first);
    std::sort(datasets.begin(), datasets.end());

    out << "Symbol Table\n";
    for (const std::string& datasetName : datasets) {
        const DatasetSymbol& dataset = datasets_.at(datasetName);
        out << "  dataset " << datasetName << " -> " << dataset.file << '\n';
        std::vector<std::string> columns;
        for (const auto& item : dataset.columns) columns.push_back(item.first);
        std::sort(columns.begin(), columns.end());
        for (const std::string& column : columns) {
            const ColumnSymbol& symbol = dataset.columns.at(column);
            out << "    " << column << " : " << valueTypeName(symbol.type)
                << (symbol.notNull ? ", NOT_NULL" : "") << '\n';
        }
    }
}

}  // namespace prep
