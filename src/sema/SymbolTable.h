#pragma once

#include "common/Diagnostics.h"
#include "schema/Schema.h"

#include <limits>
#include <map>
#include <ostream>
#include <string>
#include <vector>

// Every non-missing value of a numeric column lies in [min, max].
// When nothing is known the range is (-inf, +inf).
struct Range {
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();

    bool isKnown() const { return !std::isinf(min) || !std::isinf(max); }
    std::string text() const { return "[" + formatNumber(min) + ", " + formatNumber(max) + "]"; }
};

// What the compiler knows about one column at the current point of the program.
struct ColumnState {
    std::string name;
    ColumnType type;
    bool nullable;             // may contain missing values
    Range range;               // numeric columns only
    bool exact;                // nullable and range are the real data, not an estimate:
                               // true after LOAD, false once rows are removed or values change
    std::string stringReason;  // why a column with numbers in it is STRING, e.g. line 4 has "abc"
};

struct DatasetSymbol {
    std::string name;
    SourceLocation definedAt;
    Schema schema;                        // the CSV file as it was read by LOAD
    std::vector<ColumnState> columns;     // current columns, in order
    std::map<std::string, int> removedAt; // removed column -> line of the SELECT or DROP that removed it
    bool saved = false;

    ColumnState* column(const std::string& name);
    std::vector<std::string> columnNames() const;
};

// One global scope: every dataset created by LOAD, in program order.
class SymbolTable {
public:
    // Pointers stay valid until the next insert().
    DatasetSymbol* lookup(const std::string& name);
    const DatasetSymbol* lookup(const std::string& name) const;
    void insert(DatasetSymbol symbol);

    std::vector<std::string> names() const;
    const std::vector<DatasetSymbol>& datasets() const { return datasets_; }

    // Prints every dataset with the final state of its columns.
    void print(std::ostream& out) const;

private:
    std::vector<DatasetSymbol> datasets_;
};
