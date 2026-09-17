#pragma once

#include "common/Diagnostics.h"
#include "schema/Schema.h"

#include <optional>
#include <ostream>
#include <string>

// Reads a CSV file at compile time and infers, for every column, its type,
// how many values are missing, and the range of its numbers.
class SchemaProvider {
public:
    explicit SchemaProvider(Diagnostics& diagnostics);

    // loadLocation is the LOAD statement's position, used in error messages.
    std::optional<Schema> infer(const std::string& csvPath, SourceLocation loadLocation);

private:
    Diagnostics& diagnostics_;
};

// Prints the schema as a table with one row per column.
void printSchema(std::ostream& out, const std::string& dataset, const Schema& schema);
