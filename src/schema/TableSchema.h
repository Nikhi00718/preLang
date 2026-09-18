#pragma once

#include "schema/ColumnType.h"

#include <cstdint>
#include <string>
#include <vector>

struct ColumnInfo {
    std::string name;
    ColumnType type = ColumnType::String;
    int missingCount = 0;      // empty cells; the column is nullable if this is above 0
    bool hasRange = false;     // true once a numeric value has been seen
    double min = 0;
    double max = 0;
    std::string stringReason;  // for STRING columns with other values too: e.g. line 4 has "abc"
};

// One CSV file as it was read at compile time.
struct TableSchema {
    std::string name;               // table name: the dataset name, or the file name inside a TABLESET
    std::string file;               // path of the CSV file
    std::uint64_t fingerprint = 0;  // CRC-32 of the file, checked again at run time
    int rowCount = 0;
    std::vector<ColumnInfo> columns;
};

// LOAD_TABLES reads every CSV file of a folder.
using TableSetSchema = std::vector<TableSchema>;
