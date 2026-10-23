#include "sema/SymbolTable.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <utility>

std::string ColumnState::typeText() const {
    std::string text = std::string(columnTypeName(type)) + (nullable ? "?" : "");
    if (isNumeric(type) && range.isKnown()) {
        text += " " + range.text();
    } else if (type == ColumnType::DateTime && range.isKnown()) {
        text += " [" + dateText(range.min) + ", " + dateText(range.max) + "]";
    }
    return text;
}

ColumnState* TableState::column(const std::string& columnName) {
    for (ColumnState& state : columns) {
        if (state.name == columnName) {
            return &state;
        }
    }
    return nullptr;
}

std::vector<std::string> TableState::columnNames() const {
    std::vector<std::string> names;
    for (const ColumnState& state : columns) {
        names.push_back(state.name);
    }
    return names;
}

int ImageState::corruptCount() const {
    return static_cast<int>(std::count_if(shapes.begin(), shapes.end(), [](const ImageShape& s) { return s.corrupt; }));
}

std::string ImageState::summary() const {
    int count = 0;
    int minWidth = 0, maxWidth = 0, minHeight = 0, maxHeight = 0;
    std::set<int> channels;
    for (const ImageShape& shape : shapes) {
        if (shape.corrupt) {
            continue;
        }
        minWidth = count == 0 ? shape.width : std::min(minWidth, shape.width);
        maxWidth = count == 0 ? shape.width : std::max(maxWidth, shape.width);
        minHeight = count == 0 ? shape.height : std::min(minHeight, shape.height);
        maxHeight = count == 0 ? shape.height : std::max(maxHeight, shape.height);
        channels.insert(shape.channels);
        ++count;
    }
    const auto span = [](int low, int high) {
        return low == high ? std::to_string(low) : std::to_string(low) + ".." + std::to_string(high);
    };
    std::string channelText;
    for (int c : channels) {
        channelText += (channelText.empty() ? "" : " or ") + std::to_string(c);
    }
    std::string text = std::to_string(count) + " image" + (count == 1 ? "" : "s") + ", " + span(minWidth, maxWidth) +
                       "x" + span(minHeight, maxHeight) + ", " + channelText + " channel" +
                       (channels.size() == 1 && *channels.begin() == 1 ? "" : "s") + ", " + dtype + " " + values.text();
    const int corrupt = corruptCount();
    if (corrupt > 0) {
        text += ", plus " + std::to_string(corrupt) + " corrupt file" + (corrupt == 1 ? "" : "s");
    }
    return text;
}

std::string datasetKindName(DatasetKind kind) {
    switch (kind) {
        case DatasetKind::Table:    return "TABLE";
        case DatasetKind::TableSet: return "TABLESET";
        case DatasetKind::Image:    return "IMAGE";
        case DatasetKind::ImageSet: return "IMAGESET";
        case DatasetKind::Dataset:  return "DATASET";
    }
    return "";
}

TableState* DatasetSymbol::table(const std::string& tableName) {
    for (TableState& state : tables) {
        if (state.name == tableName) {
            return &state;
        }
    }
    return nullptr;
}

std::vector<std::string> DatasetSymbol::tableNames() const {
    std::vector<std::string> names;
    for (const TableState& state : tables) {
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

void SymbolTable::insert(DatasetSymbol symbol) { datasets_.push_back(std::move(symbol)); }

std::vector<std::string> SymbolTable::names() const {
    std::vector<std::string> result;
    for (const DatasetSymbol& symbol : datasets_) {
        result.push_back(symbol.name);
    }
    return result;
}

namespace {

void printTable(std::ostream& out, const TableState& table, const std::string& indent) {
    size_t nameWidth = 8;
    for (const ColumnState& column : table.columns) {
        nameWidth = std::max(nameWidth, column.name.size() + 2);
    }
    const int width = static_cast<int>(nameWidth);
    out << std::left << indent << std::setw(width) << "COLUMN" << std::setw(26) << "TYPE" << "EXACT\n";
    for (const ColumnState& column : table.columns) {
        out << indent << std::setw(width) << column.name << std::setw(26) << column.typeText()
            << (column.exact ? "yes" : "no") << '\n';
    }
    for (const auto& [name, line] : table.removedAt) {
        out << indent << "removed: " << name << " (line " << line << ")\n";
    }
}

}  // namespace

void SymbolTable::print(std::ostream& out) const {
    for (const DatasetSymbol& symbol : datasets_) {
        out << symbol.name << " : " << datasetKindName(symbol.kind) << "   (line " << symbol.definedAt.line << ")\n";
        switch (symbol.kind) {
            case DatasetKind::Table: printTable(out, symbol.tables.front(), "  "); break;
            case DatasetKind::TableSet:
                for (const TableState& table : symbol.tables) {
                    out << "  " << table.name << '\n';
                    printTable(out, table, "    ");
                }
                break;
            case DatasetKind::Image:
            case DatasetKind::ImageSet:
                out << "  " << symbol.images.summary() << "\n  output format: " << symbol.images.format
                    << (symbol.images.quality > 0 ? ", quality " + std::to_string(symbol.images.quality) : "") << '\n';
                break;
            case DatasetKind::Dataset:
                out << "  images " << symbol.link.images << " linked with " << symbol.link.table << "."
                    << symbol.link.column << '\n';
                if (symbol.link.exact) {
                    out << "  matched " << symbol.link.matched << " of " << symbol.link.imageCount << " images and "
                        << symbol.link.labelCount << " labels\n";
                }
                break;
        }
        if (symbol.kind != DatasetKind::Dataset) {
            out << "  saved: " << (symbol.saved ? "yes" : "no") << '\n';
        }
        out << '\n';
    }
}
