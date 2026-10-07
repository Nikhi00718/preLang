#pragma once

#include "schema/TableSchema.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The compile-time CSV analysis. The whole file is read into memory once, cut
// into pieces at line breaks, and every piece is analysed by its own thread.
// Cells are looked at in place (std::string_view), so no string is copied for
// an ordinary cell. The per-piece results are then merged in file order, which
// gives exactly the facts a single pass from the first line would give.

// What went wrong in a file, if anything. Only the first problem (in file order) is kept.
struct CsvProblem {
    enum class Kind { None, Empty, NotUtf8, FieldCount };
    Kind kind = Kind::None;
    std::int64_t line = 0;      // 1-based line number in the file
    unsigned char byte = 0;     // NotUtf8: the first byte that is not UTF-8
    size_t fields = 0;          // FieldCount: values found on the line
};

struct CsvScan {
    std::vector<ColumnInfo> columns;  // names from the header; type, missing values and range from the cells
    std::int64_t rowCount = 0;
    std::uint32_t fingerprint = 0;    // CRC-32 of the file bytes
    CsvProblem problem;
};

// Analyses the bytes of a CSV file with up to `threads` threads (0 = one per core).
CsvScan scanCsv(const std::string& bytes, unsigned threads = 0);

// Every cell of one column ("" when missing), in row order; blank lines are skipped.
// Empty if the column does not exist.
std::vector<std::string> csvColumnValues(const std::string& bytes, const std::string& column);

// The file's bytes, or false if it cannot be opened.
bool readFileBytes(const std::string& path, std::string& bytes);
