#pragma once

#include "schema/TableSchema.h"

#include <optional>
#include <string>

// Reads only the footer of a Parquet file: the column names and types, the
// number of rows, and each column's statistics (minimum, maximum, number of
// missing values). No data page is read, so the cost does not depend on how
// much data the file holds. On failure `problem` says why.
std::optional<TableSchema> readParquetFooter(const std::string& path, const std::string& name, std::string& problem);
