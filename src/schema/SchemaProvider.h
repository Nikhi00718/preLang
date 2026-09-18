#pragma once

#include "common/Diagnostics.h"
#include "schema/ImageSchema.h"
#include "schema/TableSchema.h"

#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Reads the input data at compile time: CSV files (column types, missing
// values, value ranges) and image files (width, height and channels, taken
// from the file headers only).
class SchemaProvider {
public:
    explicit SchemaProvider(Diagnostics& diagnostics);

    // `location` is the LOAD statement's position, used in error messages.
    std::optional<TableSchema> readTable(const std::string& csvPath, const std::string& name, SourceLocation location);
    std::optional<TableSetSchema> readTableFolder(const std::string& folder, SourceLocation location);
    std::optional<ImageSetSchema> readImages(const std::string& path, bool singleFile, bool recursive,
                                             SourceLocation location);

private:
    Diagnostics& diagnostics_;
};

// Every cell of one column ("" when missing). Read on demand for UNIQUE,
// REFERENCES and LINK, which need the values and not just the schema.
std::vector<std::string> readColumnValues(const TableSchema& table, const std::string& column);

void printTableSchema(std::ostream& out, const std::string& title, const TableSchema& table);
void printImageSetSchema(std::ostream& out, const std::string& title, const ImageSetSchema& images);
