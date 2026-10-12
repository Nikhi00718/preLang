#pragma once

#include "common/Dates.h"
#include "common/Diagnostics.h"
#include "schema/ImageSchema.h"
#include "schema/TableSchema.h"

#include <limits>
#include <map>
#include <ostream>
#include <string>
#include <vector>

// The abstract state of every dataset: what the compiler knows about the data
// at the current point of the program. LOAD fills it from the real files and
// every statement updates it (its "transfer function").

// Every non-missing value of a numeric column lies in [min, max].
// When nothing is known the range is (-inf, +inf).
struct Range {
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();

    bool isKnown() const { return !std::isinf(min) || !std::isinf(max); }
    std::string text() const { return "[" + formatNumber(min) + ", " + formatNumber(max) + "]"; }
};

struct ColumnState {
    std::string name;
    ColumnType type;
    bool nullable;             // may contain missing values
    Range range;               // numeric columns only
    bool exact;                // nullable and range are the real data, not an estimate:
                               // true after LOAD, false once rows are removed or values are rescaled
    bool valuesChanged;        // FILL_NULL or NORMALIZE changed values, so they differ from the file
    std::string stringReason;  // why a column with numbers in it is STRING, e.g. line 4 has "abc"
    bool rowsRemoved = false;  // a FILTER or DROP_NULL removed rows since LOAD

    // e.g. "INTEGER? [19, 23]"; '?' means the column may have missing values
    std::string typeText() const;
};

struct TableState {
    std::string name;                     // the dataset name for a TABLE, the file name in a TABLESET
    TableSchema schema;                   // the CSV file as read by LOAD
    std::vector<ColumnState> columns;     // current columns, in order
    std::map<std::string, int> removedAt; // removed column -> line of the SELECT or DROP that removed it

    ColumnState* column(const std::string& name);
    std::vector<std::string> columnNames() const;
};

// The shape of one image. Shapes come from the file headers and every image
// operation changes them in a known way, so they stay exact.
struct ImageShape {
    std::string path;
    int width;
    int height;
    int channels;
    bool corrupt;
    std::string problem;
};

struct ImageState {
    std::vector<ImageShape> shapes;  // one per file
    std::string dtype = "UINT8";     // pixel type: UINT8, UINT16, MIXED or FLOAT32
    Range values{0, 255};            // pixel value range
    std::string format = "SOURCE";   // output encoding: SOURCE (keep each file's format), JPG, PNG or NPY
    int quality = 0;                 // JPG quality; 0 means the library default
    int normalizedLine = 0;          // line of the NORMALIZE ... RANGE that made the pixels FLOAT32
    bool brokenRemoved = false;      // REMOVE_CORRUPT ran: files that fail to decode are skipped

    int corruptCount() const;
    std::string summary() const;     // e.g. "12 images, 224x224, 3 channels, UINT8 [0, 255]"
};

// A DATASET made by LINK: images matched with table rows by file name.
struct LinkState {
    std::string images;  // IMAGESET name
    std::string table;   // TABLE name
    std::string column;  // column holding the file names
    bool exact = false;  // the counts below are known at compile time
    int imageCount = 0;
    int labelCount = 0;
    int matched = 0;
    std::vector<std::string> imagesWithoutLabel;
    std::vector<std::string> labelsWithoutImage;
    bool skipsBroken = false;  // REMOVE_CORRUPT ran before LINK, so broken files are not linked
};

enum class DatasetKind { Table, TableSet, Image, ImageSet, Dataset };

std::string datasetKindName(DatasetKind kind);

struct DatasetSymbol {
    std::string name;
    DatasetKind kind;
    SourceLocation definedAt;
    std::vector<TableState> tables;  // TABLE: one table; TABLESET: one per CSV file
    ImageSetSchema imageSchema;      // IMAGE and IMAGESET: the files as read by LOAD
    ImageState images;               // IMAGE and IMAGESET: what is known now
    LinkState link;                  // DATASET
    bool saved = false;

    bool isTabular() const { return kind == DatasetKind::Table || kind == DatasetKind::TableSet; }
    bool isImage() const { return kind == DatasetKind::Image || kind == DatasetKind::ImageSet; }
    TableState* table(const std::string& name);
    std::vector<std::string> tableNames() const;
};

// One global scope: every dataset created by LOAD or LINK, in program order.
class SymbolTable {
public:
    // Pointers stay valid until the next insert().
    DatasetSymbol* lookup(const std::string& name);
    const DatasetSymbol* lookup(const std::string& name) const;
    void insert(DatasetSymbol symbol);

    std::vector<std::string> names() const;
    const std::vector<DatasetSymbol>& datasets() const { return datasets_; }

    // Prints every dataset with the final state of its data.
    void print(std::ostream& out) const;

private:
    std::vector<DatasetSymbol> datasets_;
};
