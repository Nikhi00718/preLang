#include "schema/ParquetFooter.h"

#include "schema/Checksum.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

// A Parquet file ends with its footer: a FileMetaData structure in Thrift's
// compact encoding, then its length (4 bytes, little-endian) and "PAR1".
// FileMetaData holds the schema, the number of rows and, for every row group
// and column, statistics: the smallest and largest value and the number of
// missing values. Field numbers are those of parquet.thrift in the Apache
// Parquet format specification.

namespace {

// ---------- Thrift compact protocol ----------

enum CompactType {
    BoolTrue = 1, BoolFalse = 2, Byte = 3, I16 = 4, I32 = 5, I64 = 6,
    Double = 7, Binary = 8, List = 9, Set = 10, Map = 11, Struct = 12,
};

// Decodes the few fields PrepLang needs and skips all others. Every read
// checks the bounds; a malformed footer sets failed() instead of crashing.
class CompactReader {
public:
    explicit CompactReader(const std::string& bytes) : bytes_(bytes) {}

    bool failed() const { return failed_; }

    std::uint64_t varint() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64 && !failed_; shift += 7) {
            const int byte = next();
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
        }
        failed_ = true;
        return 0;
    }

    // i16, i32 and i64 are all zigzag-encoded varints.
    std::int64_t integer() {
        const std::uint64_t n = varint();
        return static_cast<std::int64_t>(n >> 1) ^ -static_cast<std::int64_t>(n & 1);
    }

    std::string binary() {
        const std::uint64_t size = varint();
        if (failed_ || size > bytes_.size() - position_) {
            failed_ = true;
            return "";
        }
        std::string value = bytes_.substr(position_, static_cast<size_t>(size));
        position_ += static_cast<size_t>(size);
        return value;
    }

    // Reads the next field header of the current struct. Returns false at the
    // end of the struct. Each struct counts its field ids from its own `lastId`.
    bool field(int& lastId, int& id, int& type) {
        const int header = next();
        if (header == 0 || failed_) {
            return false;
        }
        type = header & 0x0F;
        const int delta = header >> 4;
        id = delta != 0 ? lastId + delta : static_cast<int>(integer());
        lastId = id;
        return true;
    }

    // Number of elements of a list; `elementType` receives their type.
    int listSize(int& elementType) {
        const int header = next();
        elementType = header & 0x0F;
        const int size = header >> 4;
        return size == 15 ? static_cast<int>(varint()) : size;
    }

    void skip(int type) {
        if (++depth_ > 64) {  // no real footer nests this deep
            failed_ = true;
        }
        switch (type) {
            case BoolTrue:
            case BoolFalse:
                break;  // a boolean field is stored in its type
            case Byte:
                next();
                break;
            case I16:
            case I32:
            case I64:
                varint();
                break;
            case Double:
                advance(8);
                break;
            case Binary:
                binary();
                break;
            case List:
            case Set: {
                int elementType = 0;
                const int size = listSize(elementType);
                for (int i = 0; i < size && !failed_; ++i) {
                    skipElement(elementType);
                }
                break;
            }
            case Map: {
                const std::uint64_t size = varint();
                const int types = size > 0 ? next() : 0;
                for (std::uint64_t i = 0; i < size && !failed_; ++i) {
                    skipElement(types >> 4);
                    skipElement(types & 0x0F);
                }
                break;
            }
            case Struct: {
                int lastId = 0, id = 0, fieldType = 0;
                while (field(lastId, id, fieldType)) {
                    skip(fieldType);
                }
                break;
            }
            default:
                failed_ = true;
        }
        --depth_;
    }

private:
    int next() {
        if (position_ >= bytes_.size()) {
            failed_ = true;
            return 0;
        }
        return static_cast<unsigned char>(bytes_[position_++]);
    }

    void advance(size_t count) {
        if (count > bytes_.size() - position_) {
            failed_ = true;
        } else {
            position_ += count;
        }
    }

    // Inside lists and maps a boolean takes one byte.
    void skipElement(int type) {
        if (type == BoolTrue || type == BoolFalse) {
            next();
        } else {
            skip(type);
        }
    }

    const std::string& bytes_;
    size_t position_ = 0;
    int depth_ = 0;
    bool failed_ = false;
};

// ---------- The parts of FileMetaData PrepLang uses ----------

// Physical types (enum Type)
enum PhysicalType { Boolean = 0, Int32 = 1, Int64 = 2, Int96 = 3, Float = 4, DoubleType = 5, ByteArray = 6, FixedLen = 7 };

struct SchemaElement {
    int type = -1;         // physical type; -1 for a group
    int repetition = 0;    // 0 REQUIRED, 1 OPTIONAL, 2 REPEATED
    std::string name;
    int children = 0;
    int converted = -1;    // ConvertedType, the older way to annotate a type
    int logical = 0;       // field id of the LogicalType member, 0 if none
    bool isSigned = true;  // false for unsigned integers
};

struct ChunkStatistics {
    std::int64_t values = 0;  // number of values, missing ones included
    bool hasNulls = false;
    std::int64_t nulls = 0;
    bool hasNaNs = false;
    std::int64_t nans = 0;
    bool hasMin = false, hasMax = false;
    std::string min, max;     // plain-encoded values
    bool minExact = false, maxExact = false;
};

struct RowGroup {
    std::vector<ChunkStatistics> columns;
};

struct FileMetaData {
    std::vector<SchemaElement> schema;
    std::int64_t rows = 0;
    std::vector<RowGroup> rowGroups;
};

void readLogicalType(CompactReader& in, SchemaElement& element) {
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {  // a union: exactly one member is set
        element.logical = id;
        if (id == 10 && type == Struct) {  // INTEGER(bitWidth, isSigned)
            int intLast = 0, intId = 0, intType = 0;
            while (in.field(intLast, intId, intType)) {
                if (intId == 2) {
                    element.isSigned = intType == BoolTrue;
                } else {
                    in.skip(intType);
                }
            }
        } else {
            in.skip(type);
        }
    }
}

SchemaElement readSchemaElement(CompactReader& in) {
    SchemaElement element;
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {
        switch (id) {
            case 1: element.type = static_cast<int>(in.integer()); break;
            case 3: element.repetition = static_cast<int>(in.integer()); break;
            case 4: element.name = in.binary(); break;
            case 5: element.children = static_cast<int>(in.integer()); break;
            case 6: element.converted = static_cast<int>(in.integer()); break;
            case 10: readLogicalType(in, element); break;
            default: in.skip(type);
        }
    }
    if (element.converted >= 11 && element.converted <= 14) {  // UINT_8 ... UINT_64
        element.isSigned = false;
    }
    return element;
}

void readStatistics(CompactReader& in, ChunkStatistics& stats) {
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {
        switch (id) {
            case 3: stats.hasNulls = true; stats.nulls = in.integer(); break;
            case 5: stats.hasMax = true; stats.max = in.binary(); break;
            case 6: stats.hasMin = true; stats.min = in.binary(); break;
            case 7: stats.maxExact = type == BoolTrue; break;
            case 8: stats.minExact = type == BoolTrue; break;
            case 9: stats.hasNaNs = true; stats.nans = in.integer(); break;
            default: in.skip(type);  // 1 and 2 are the deprecated min/max
        }
    }
}

ChunkStatistics readColumnChunk(CompactReader& in) {
    ChunkStatistics stats;
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {
        if (id != 3) {  // ColumnChunk.meta_data
            in.skip(type);
            continue;
        }
        int metaLast = 0, metaId = 0, metaType = 0;
        while (in.field(metaLast, metaId, metaType)) {
            if (metaId == 5) {
                stats.values = in.integer();
            } else if (metaId == 12) {
                readStatistics(in, stats);
            } else {
                in.skip(metaType);
            }
        }
    }
    return stats;
}

RowGroup readRowGroup(CompactReader& in) {
    RowGroup group;
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {
        if (id == 1) {
            int elementType = 0;
            const int size = in.listSize(elementType);
            for (int i = 0; i < size && !in.failed(); ++i) {
                group.columns.push_back(readColumnChunk(in));
            }
        } else {
            in.skip(type);
        }
    }
    return group;
}

FileMetaData readFileMetaData(CompactReader& in) {
    FileMetaData meta;
    int lastId = 0, id = 0, type = 0;
    while (in.field(lastId, id, type)) {
        int elementType = 0;
        switch (id) {
            case 2: {
                const int size = in.listSize(elementType);
                for (int i = 0; i < size && !in.failed(); ++i) {
                    meta.schema.push_back(readSchemaElement(in));
                }
                break;
            }
            case 3:
                meta.rows = in.integer();
                break;
            case 4: {
                const int size = in.listSize(elementType);
                for (int i = 0; i < size && !in.failed(); ++i) {
                    meta.rowGroups.push_back(readRowGroup(in));
                }
                break;
            }
            default:
                in.skip(type);
        }
    }
    return meta;
}

// ---------- From Parquet types to PrepLang types ----------

// Sets the PrepLang type of a column, or returns false with the reason in `problem`.
bool columnType(const SchemaElement& element, ColumnType& type, std::string& problem) {
    const bool dateOrTime = element.logical == 6 || element.logical == 7 || element.logical == 8 ||
                            (element.converted >= 6 && element.converted <= 10);
    const bool decimal = element.logical == 5 || element.converted == 5;
    const bool text = element.logical == 1 || element.logical == 4 || element.logical == 12 ||
                      element.converted == 0 || element.converted == 4 || element.converted == 19;
    switch (element.type) {
        case Boolean:
            type = ColumnType::Boolean;
            return true;
        case Int32:
        case Int64:
            if (!dateOrTime && !decimal) {
                type = ColumnType::Integer;
                return true;
            }
            break;
        case Float:
        case DoubleType:
            if (element.logical != 15) {  // FLOAT16 is stored differently
                type = ColumnType::Float;
                return true;
            }
            break;
        case ByteArray:
            if (text) {
                type = ColumnType::String;
                return true;
            }
            break;
        default:
            break;
    }
    problem = dateOrTime ? "a date or time" : decimal ? "a decimal" : "a binary or other special";
    problem = "column '" + element.name + "' is " + problem + " column; PrepLang reads numbers, text and booleans";
    return false;
}

// Little-endian bytes as an unsigned number.
std::uint64_t littleEndian(const std::string& bytes) {
    std::uint64_t value = 0;
    for (size_t i = bytes.size(); i-- > 0;) {
        value = (value << 8) | static_cast<unsigned char>(bytes[i]);
    }
    return value;
}

// A plain-encoded statistics value as a number; false if it is not usable.
bool decodeNumber(const std::string& bytes, const SchemaElement& element, double& value) {
    switch (element.type) {
        case Int32:
            if (bytes.size() != 4) return false;
            value = element.isSigned ? static_cast<double>(static_cast<std::int32_t>(littleEndian(bytes)))
                                     : static_cast<double>(static_cast<std::uint32_t>(littleEndian(bytes)));
            return true;
        case Int64:
            if (bytes.size() != 8) return false;
            value = element.isSigned ? static_cast<double>(static_cast<std::int64_t>(littleEndian(bytes)))
                                     : static_cast<double>(littleEndian(bytes));
            return true;
        case Float: {
            if (bytes.size() != 4) return false;
            const std::uint32_t bits = static_cast<std::uint32_t>(littleEndian(bytes));
            float number = 0;
            std::memcpy(&number, &bits, sizeof number);
            value = number;
            return !std::isnan(value);
        }
        case DoubleType: {
            if (bytes.size() != 8) return false;
            const std::uint64_t bits = littleEndian(bytes);
            std::memcpy(&value, &bits, sizeof value);
            return !std::isnan(value);
        }
        default:
            return false;
    }
}

// Combines one column's statistics over all row groups.
ColumnInfo columnInfo(const SchemaElement& element, ColumnType type, const std::vector<ChunkStatistics>& chunks) {
    ColumnInfo info;
    info.name = element.name;
    info.type = type;
    const bool floating = element.type == Float || element.type == DoubleType;
    bool rangeKnown = true;
    for (const ChunkStatistics& chunk : chunks) {
        info.missingCount += (chunk.hasNulls ? chunk.nulls : 0) + (chunk.hasNaNs ? chunk.nans : 0);
        // pandas reads NaN as a missing value, and the null count leaves NaN out:
        // a float column is only known to have no missing values if NaN is counted too.
        if (!chunk.hasNulls || (floating && !chunk.hasNaNs)) {
            info.missingKnown = false;
        }
        if (!isNumeric(type)) {
            continue;
        }
        double low = 0, high = 0;
        if (chunk.hasMin && chunk.hasMax && decodeNumber(chunk.min, element, low) &&
            decodeNumber(chunk.max, element, high)) {
            info.min = info.hasRange ? std::min(info.min, low) : low;
            info.max = info.hasRange ? std::max(info.max, high) : high;
            info.hasRange = true;
            info.boundsExact = info.boundsExact && chunk.minExact && chunk.maxExact;
        } else if (!chunk.hasNulls || chunk.values > chunk.nulls) {
            rangeKnown = false;  // this row group has values but no usable statistics
        }
    }
    if (element.repetition == 0 && !floating) {  // REQUIRED: the format forbids missing values
        info.missingKnown = true;
        info.missingCount = 0;
    }
    if (!rangeKnown) {
        info.hasRange = false;
    }
    info.boundsExact = info.boundsExact && info.hasRange;
    return info;
}

}  // namespace

std::optional<TableSchema> readParquetFooter(const std::string& path, const std::string& name, std::string& problem) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        problem = "cannot open Parquet file '" + path + "'";
        return std::nullopt;
    }
    const std::int64_t size = static_cast<std::int64_t>(file.tellg());
    std::string tail(8, '\0');
    if (size < 12 || !file.seekg(size - 8).read(tail.data(), 8)) {
        problem = "'" + path + "' is too short to be a Parquet file";
        return std::nullopt;
    }
    if (tail.compare(4, 4, "PARE") == 0) {
        problem = "'" + path + "' is an encrypted Parquet file, which PrepLang cannot read";
        return std::nullopt;
    }
    const std::int64_t length = static_cast<std::int64_t>(littleEndian(tail.substr(0, 4)));
    if (tail.compare(4, 4, "PAR1") != 0 || length <= 0 || length > size - 12) {
        problem = "'" + path + "' is not a Parquet file (no footer)";
        return std::nullopt;
    }
    std::string footer(static_cast<size_t>(length), '\0');
    file.seekg(size - 8 - length).read(footer.data(), length);

    CompactReader in(footer);
    const FileMetaData meta = readFileMetaData(in);
    const size_t leaves = meta.schema.empty() ? 0 : meta.schema.size() - 1;
    if (in.failed() || !file || meta.schema.empty()) {
        problem = "the footer of Parquet file '" + path + "' is damaged";
        return std::nullopt;
    }
    if (static_cast<size_t>(meta.schema.front().children) != leaves) {
        problem = "'" + path + "' has nested columns (lists or structs); PrepLang reads flat tables";
        return std::nullopt;
    }

    TableSchema table;
    table.name = name;
    table.file = path;
    table.format = TableFormat::Parquet;
    table.fingerprint = crc32(footer + tail);  // the proofs use only what the footer says
    table.rowCount = meta.rows;
    for (size_t column = 0; column < leaves; ++column) {
        const SchemaElement& element = meta.schema[column + 1];
        ColumnType type = ColumnType::String;
        if (element.repetition == 2 || element.children > 0 || !columnType(element, type, problem)) {
            if (problem.empty()) {
                problem = "column '" + element.name + "' of '" + path + "' is nested; PrepLang reads flat tables";
            }
            return std::nullopt;
        }
        std::vector<ChunkStatistics> chunks;
        for (const RowGroup& group : meta.rowGroups) {
            if (group.columns.size() != leaves) {
                problem = "the footer of Parquet file '" + path + "' is damaged";
                return std::nullopt;
            }
            chunks.push_back(group.columns[column]);
        }
        table.columns.push_back(columnInfo(element, type, chunks));
    }
    return table;
}
