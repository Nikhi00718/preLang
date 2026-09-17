#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

// Column types the compiler can infer from the text in a CSV file.
enum class ColumnType { Boolean, Integer, Float, String };

inline const char* columnTypeName(ColumnType type) {
    switch (type) {
        case ColumnType::Boolean: return "BOOLEAN";
        case ColumnType::Integer: return "INTEGER";
        case ColumnType::Float:   return "FLOAT";
        case ColumnType::String:  return "STRING";
    }
    return "";
}

inline bool isNumeric(ColumnType type) {
    return type == ColumnType::Integer || type == ColumnType::Float;
}

// 20 -> "20", 92.5 -> "92.5", infinity -> "inf"
inline std::string formatNumber(double value) {
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    std::ostringstream text;
    text << std::setprecision(10) << value;
    return text.str();
}

struct ColumnInfo {
    std::string name;
    ColumnType type = ColumnType::String;
    int missingCount = 0;   // empty cells; the column is nullable if this is above 0
    bool hasRange = false;  // true once a numeric value has been seen
    double min = 0;
    double max = 0;
    std::string stringReason;  // for STRING columns with other values too: e.g. line 4 has "abc"
};

// What the compiler knows about a CSV file before the program runs.
struct Schema {
    std::string file;
    std::uint64_t fingerprint = 0;  // hash of the file's bytes, checked again at run time
    int rowCount = 0;
    std::vector<ColumnInfo> columns;
};
