#pragma once

#include "common/Diagnostics.h"
#include "schema/ImageSchema.h"
#include "schema/TableSchema.h"

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Reads the input data at compile time: CSV files (column types, missing
// values, value ranges), the footers of Parquet files (the same facts from the
// file's statistics, without reading the data) and image files (width, height
// and channels, taken from the file headers only).
class SchemaProvider {
public:
    explicit SchemaProvider(Diagnostics& diagnostics);

    // A .parquet path is read from its footer, any other path as CSV.
    // `location` is the LOAD statement's position, used in error messages.
    std::optional<TableSchema> readTable(const std::string& path, const std::string& name, SourceLocation location);
    std::optional<TableSetSchema> readTableFolder(const std::string& folder, SourceLocation location);
    std::optional<ImageSetSchema> readImages(const std::string& path, bool singleFile, bool recursive,
                                             SourceLocation location);

    // From now on, a table whose overrideKey is in the map is not read: the given
    // facts are used instead. The certifier analyses the program again this way,
    // with weaker facts than the real file has, without touching the data.
    void setOverrides(std::map<std::string, TableSchema> overrides) { overrides_ = std::move(overrides); }
    // One key per LOAD, so two LOADs of the same file can be given different facts.
    static std::string overrideKey(const std::string& path, const std::string& tableName) {
        return path + "|" + tableName;
    }
    // Private compilation: from now on no input file is opened. A table is described by
    // the data owner's released facts (schema/ReleasedFacts.h), under any override; a
    // table without released facts, a folder of tables and images are errors.
    void setReleased(std::map<std::string, TableSchema> released) {
        released_ = std::move(released);
        private_ = true;
    }
    // The same for image inputs, keyed by imageKey.
    void setImageOverrides(std::map<std::string, ImageSetSchema> overrides) { imageOverrides_ = std::move(overrides); }
    static std::string imageKey(const std::string& path, bool singleFile, bool recursive) {
        return path + (singleFile ? "|file" : "|folder") + (recursive ? "|recursive" : "");
    }

private:
    std::optional<TableSchema> readCsv(const std::string& csvPath, const std::string& name, SourceLocation location);

    Diagnostics& diagnostics_;
    std::map<std::string, TableSchema> overrides_;
    std::map<std::string, TableSchema> released_;
    bool private_ = false;
    std::map<std::string, ImageSetSchema> imageOverrides_;
    std::map<std::string, ImageSetSchema> images_;  // image headers already read, by imageKey
};

// Every cell of one column ("" when missing). Read on demand for UNIQUE,
// REFERENCES and LINK, which need the values and not just the schema.
// Empty for a Parquet file, whose values are not read at compile time.
std::vector<std::string> readColumnValues(const TableSchema& table, const std::string& column);

// The date facts of a STRING column of a CSV file (ColumnInfo::allDates, dateMin, ...), from
// its values. Read on demand for PARSE_DATE.
void readDateFacts(const TableSchema& table, ColumnInfo& column);

void printTableSchema(std::ostream& out, const std::string& title, const TableSchema& table);
void printImageSetSchema(std::ostream& out, const std::string& title, const ImageSetSchema& images);
