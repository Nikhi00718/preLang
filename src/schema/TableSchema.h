#pragma once

#include "schema/ColumnType.h"

#include <cstdint>
#include <string>
#include <vector>

struct ColumnInfo {
    std::string name;
    ColumnType type = ColumnType::String;
    std::int64_t missingCount = 0;  // missing values; the column is nullable if this is above 0
    bool missingKnown = true;       // false when a Parquet file does not record every missing value
    bool hasRange = false;          // true once a numeric value has been seen
    double min = 0;
    double max = 0;
    bool boundsExact = true;        // min and max are values in the data, not just bounds
    std::string stringReason;       // for STRING columns with other values too: e.g. line 4 has "abc"
};

// CSV files are read completely at compile time. For a Parquet file only the
// footer is read: its schema and per-column statistics.
enum class TableFormat { Csv, Parquet };

// One table file as it was read at compile time.
struct TableSchema {
    std::string name;               // table name: the dataset name, or the file name inside a TABLESET
    std::string file;               // path of the file
    TableFormat format = TableFormat::Csv;
    std::uint64_t fingerprint = 0;  // CRC-32 of the file (Parquet: of its footer), checked again at run time
    std::int64_t rowCount = 0;
    std::vector<ColumnInfo> columns;
    bool valuesKnown = true;        // false when these facts describe every file a data certificate
                                    // admits, not the values of one file
};

// LOAD_TABLES reads every CSV and Parquet file of a folder.
using TableSetSchema = std::vector<TableSchema>;
