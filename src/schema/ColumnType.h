#pragma once

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

// Column types the compiler can infer from the text in a CSV file, plus
// DATETIME, which only Parquet files declare (dates and timestamps). A DATETIME
// column can be loaded, kept, dropped, saved and checked for missing values,
// but not compared or filled: the language has no date values.
enum class ColumnType { Boolean, Integer, Float, String, DateTime };

inline const char* columnTypeName(ColumnType type) {
    switch (type) {
        case ColumnType::Boolean: return "BOOLEAN";
        case ColumnType::Integer: return "INTEGER";
        case ColumnType::Float:   return "FLOAT";
        case ColumnType::String:  return "STRING";
        case ColumnType::DateTime: return "DATETIME";
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
