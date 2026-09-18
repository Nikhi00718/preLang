#include "schema/SchemaProvider.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace fs = std::filesystem;

namespace {

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// CRC-32, the same checksum as Python's zlib.crc32. The generated program
// computes it again and refuses to run when an input changed after compilation.
std::uint32_t crc32(const std::string& bytes) {
    static const std::vector<std::uint32_t> table = [] {
        std::vector<std::uint32_t> entries(256);
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            entries[i] = value;
        }
        return entries;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (char c : bytes) {
        crc = table[(crc ^ static_cast<unsigned char>(c)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint64_t fileFingerprint(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return crc32(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

// ---------- CSV ----------

// Splits one CSV line into trimmed fields. A field inside double quotes may
// contain commas, and "" inside quotes stands for a single quote character.
std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                inQuotes = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trim(field));
    return fields;
}

// Reads the header line, dropping the UTF-8 byte-order mark Excel may add.
bool readHeader(std::ifstream& file, std::string& line) {
    if (!std::getline(file, line)) {
        return false;
    }
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        line.erase(0, 3);
    }
    return true;
}

// The most specific type for one non-empty cell. Numbers use the same rule
// as PrepLang number literals: optional '-', digits, optional '.' and digits.
ColumnType typeOfCell(const std::string& cell) {
    const std::string lower = toLower(cell);
    if (lower == "true" || lower == "false") {
        return ColumnType::Boolean;
    }
    size_t i = (cell[0] == '-') ? 1 : 0;
    const size_t digitsStart = i;
    while (i < cell.size() && isDigit(cell[i])) {
        ++i;
    }
    if (i == digitsStart) {
        return ColumnType::String;
    }
    if (i == cell.size()) {
        return ColumnType::Integer;
    }
    if (cell[i] != '.') {
        return ColumnType::String;
    }
    const size_t fractionStart = ++i;
    while (i < cell.size() && isDigit(cell[i])) {
        ++i;
    }
    return (i > fractionStart && i == cell.size()) ? ColumnType::Float : ColumnType::String;
}

// Combines the type seen so far in a column with the type of a new cell.
// INTEGER and FLOAT give FLOAT; any other mix can only be STRING.
ColumnType combine(ColumnType current, ColumnType next) {
    if (current == next) {
        return current;
    }
    return isNumeric(current) && isNumeric(next) ? ColumnType::Float : ColumnType::String;
}

// ---------- Image headers ----------

std::uint32_t bigEndian32(const unsigned char* bytes) {
    return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) | (std::uint32_t{bytes[2]} << 8) |
           std::uint32_t{bytes[3]};
}

// PNG: an 8-byte signature, then the IHDR chunk with width, height, bit depth
// and color type (which gives the number of channels).
bool readPngHeader(std::istream& in, ImageInfo& info) {
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    unsigned char header[26];
    if (!in.read(reinterpret_cast<char*>(header), sizeof header)) {
        info.problem = "file is too short to be a PNG image";
        return false;
    }
    if (std::memcmp(header, signature, 8) != 0) {
        info.problem = "not a PNG file (bad signature)";
        return false;
    }
    if (std::memcmp(header + 12, "IHDR", 4) != 0) {
        info.problem = "PNG header chunk is missing";
        return false;
    }
    info.width = static_cast<int>(bigEndian32(header + 16));
    info.height = static_cast<int>(bigEndian32(header + 20));
    info.bitDepth = header[24];
    switch (header[25]) {
        case 0: info.channels = 1; break;  // grayscale
        case 2: info.channels = 3; break;  // RGB
        case 3: info.channels = 3; break;  // palette, decoded as RGB
        case 4: info.channels = 4; break;  // grayscale + alpha, decoded as RGBA
        case 6: info.channels = 4; break;  // RGBA
        default:
            info.problem = "unknown PNG color type";
            return false;
    }
    return true;
}

// JPEG: a sequence of marker segments. The frame header (SOFn) holds the
// height, width and number of components; everything before it is skipped.
bool readJpegHeader(std::istream& in, ImageInfo& info) {
    if (in.get() != 0xFF || in.get() != 0xD8) {
        info.problem = "not a JPEG file (bad signature)";
        return false;
    }
    while (true) {
        if (in.get() != 0xFF) {
            info.problem = "broken JPEG marker";
            return false;
        }
        int marker = in.get();
        while (marker == 0xFF) {  // fill bytes
            marker = in.get();
        }
        if (marker == EOF || marker == 0xD9 || marker == 0xDA) {
            info.problem = "JPEG frame header is missing (file is truncated)";
            return false;
        }
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;  // markers without a length
        }
        const int length = (in.get() << 8) | in.get();
        if (!in || length < 2) {
            info.problem = "broken JPEG segment";
            return false;
        }
        const bool frameHeader = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (frameHeader) {
            unsigned char frame[6];
            if (!in.read(reinterpret_cast<char*>(frame), sizeof frame)) {
                info.problem = "JPEG frame header is truncated";
                return false;
            }
            info.bitDepth = frame[0];
            info.height = (frame[1] << 8) | frame[2];
            info.width = (frame[3] << 8) | frame[4];
            info.channels = frame[5];
            return true;
        }
        in.seekg(length - 2, std::ios::cur);
    }
}

bool isImageFile(const fs::path& path) {
    const std::string extension = toLower(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

ImageInfo readImageHeader(const fs::path& file, const std::string& relative) {
    ImageInfo info;
    info.path = relative;
    info.format = toLower(file.extension().string()) == ".png" ? "PNG" : "JPG";
    std::error_code error;
    info.bytes = fs::file_size(file, error);
    std::ifstream in(file, std::ios::binary);
    const bool ok = in && (info.format == "PNG" ? readPngHeader(in, info) : readJpegHeader(in, info));
    if (ok && (info.width <= 0 || info.height <= 0 || info.channels <= 0)) {
        info.problem = "image has zero size";
    }
    info.corrupt = !info.problem.empty() || !ok;
    if (info.corrupt && info.problem.empty()) {
        info.problem = "cannot read the file";
    }
    return info;
}

}  // namespace

SchemaProvider::SchemaProvider(Diagnostics& diagnostics) : diagnostics_(diagnostics) {}

std::optional<TableSchema> SchemaProvider::readTable(const std::string& csvPath, const std::string& name,
                                                     SourceLocation location) {
    std::ifstream file(csvPath);
    if (!file) {
        diagnostics_.error(Phase::Schema, location, "cannot open CSV file '" + csvPath + "'",
                           "the path is relative to the folder of the .prep file");
        return std::nullopt;
    }
    std::string line;
    if (!readHeader(file, line)) {
        diagnostics_.error(Phase::Schema, location, "CSV file '" + csvPath + "' is empty",
                           "the first line must list the column names");
        return std::nullopt;
    }

    TableSchema table;
    table.name = name;
    table.file = csvPath;
    table.fingerprint = fileFingerprint(csvPath);
    for (const std::string& columnName : splitCsvLine(line)) {
        ColumnInfo column;
        column.name = columnName;
        table.columns.push_back(column);
    }

    // Type of each column so far; empty until the column's first value.
    std::vector<std::optional<ColumnType>> inferred(table.columns.size());
    // Whether the column has a value that is not plain text (a number or boolean).
    std::vector<bool> hasNonText(table.columns.size(), false);
    int lineNumber = 1;

    while (std::getline(file, line)) {
        ++lineNumber;
        if (trim(line).empty()) {
            continue;
        }
        const std::vector<std::string> cells = splitCsvLine(line);
        if (cells.size() != table.columns.size()) {
            diagnostics_.error(Phase::Schema, location,
                               "line " + std::to_string(lineNumber) + " of '" + csvPath + "' has " +
                                   std::to_string(cells.size()) + " values but the header has " +
                                   std::to_string(table.columns.size()) + " columns");
            return std::nullopt;
        }
        ++table.rowCount;

        for (size_t i = 0; i < cells.size(); ++i) {
            ColumnInfo& column = table.columns[i];
            if (cells[i].empty()) {
                ++column.missingCount;
                continue;
            }
            const ColumnType type = typeOfCell(cells[i]);
            inferred[i] = inferred[i] ? combine(*inferred[i], type) : type;
            if (type != ColumnType::String) {
                hasNonText[i] = true;
            }
            if (*inferred[i] == ColumnType::String && column.stringReason.empty()) {
                column.stringReason = "line " + std::to_string(lineNumber) + " has \"" + cells[i] + "\"";
            }
            if (isNumeric(type)) {
                const double value = std::stod(cells[i]);
                column.min = column.hasRange ? std::min(column.min, value) : value;
                column.max = column.hasRange ? std::max(column.max, value) : value;
                column.hasRange = true;
            }
        }
    }

    for (size_t i = 0; i < table.columns.size(); ++i) {
        ColumnInfo& column = table.columns[i];
        column.type = inferred[i].value_or(ColumnType::String);  // no values at all: STRING
        if (!hasNonText[i]) {
            column.stringReason.clear();  // an ordinary text column needs no explanation
        }
    }
    return table;
}

std::optional<TableSetSchema> SchemaProvider::readTableFolder(const std::string& folder, SourceLocation location) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        diagnostics_.error(Phase::Schema, location, "cannot open table folder '" + folder + "'",
                           "LOAD_TABLES expects a folder of CSV files; use LOAD for one file");
        return std::nullopt;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && toLower(entry.path().extension().string()) == ".csv") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        diagnostics_.error(Phase::Schema, location, "folder '" + folder + "' has no CSV files");
        return std::nullopt;
    }

    TableSetSchema tables;
    for (const fs::path& file : files) {
        std::optional<TableSchema> table = readTable(file.string(), file.stem().u8string(), location);
        if (!table) {
            return std::nullopt;
        }
        tables.push_back(std::move(*table));
    }
    return tables;
}

std::optional<ImageSetSchema> SchemaProvider::readImages(const std::string& path, bool singleFile, bool recursive,
                                                         SourceLocation location) {
    ImageSetSchema set;
    set.root = path;
    set.singleFile = singleFile;
    set.recursive = recursive;
    std::error_code error;

    if (singleFile) {
        if (!fs::is_regular_file(path, error)) {
            diagnostics_.error(Phase::Schema, location, "cannot open image file '" + path + "'",
                               "LOAD_IMAGE expects one file; use LOAD_IMAGES for a folder");
            return std::nullopt;
        }
        if (!isImageFile(path)) {
            diagnostics_.error(Phase::Schema, location, "'" + path + "' is not a PNG or JPG image");
            return std::nullopt;
        }
        set.images.push_back(readImageHeader(path, fs::path(path).filename().u8string()));
    } else {
        if (!fs::is_directory(path, error)) {
            diagnostics_.error(Phase::Schema, location, "cannot open image folder '" + path + "'",
                               "LOAD_IMAGES expects a folder; use LOAD_IMAGE for one file");
            return std::nullopt;
        }
        const auto add = [&](const fs::directory_entry& entry) {
            if (entry.is_regular_file(error) && isImageFile(entry.path())) {
                set.images.push_back(readImageHeader(entry.path(), entry.path().lexically_relative(path).generic_u8string()));
            }
        };
        if (recursive) {
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(path, error)) {
                add(entry);
            }
        } else {
            for (const fs::directory_entry& entry : fs::directory_iterator(path, error)) {
                add(entry);
            }
        }
        if (set.images.empty()) {
            diagnostics_.error(Phase::Schema, location, "folder '" + path + "' has no PNG or JPG images",
                               recursive ? "" : "add RECURSIVE to also read sub-folders");
            return std::nullopt;
        }
    }

    std::sort(set.images.begin(), set.images.end(),
              [](const ImageInfo& a, const ImageInfo& b) { return a.path < b.path; });
    std::string listing;
    for (const ImageInfo& image : set.images) {
        listing += image.path + "|" + std::to_string(image.bytes) + "\n";
    }
    set.fingerprint = crc32(listing);
    return set;
}

std::vector<std::string> readColumnValues(const TableSchema& table, const std::string& column) {
    std::ifstream file(table.file);
    std::string line;
    if (!readHeader(file, line)) {
        return {};
    }
    const std::vector<std::string> header = splitCsvLine(line);
    const auto found = std::find(header.begin(), header.end(), column);
    if (found == header.end()) {
        return {};
    }
    const size_t index = static_cast<size_t>(found - header.begin());
    std::vector<std::string> values;
    while (std::getline(file, line)) {
        if (!trim(line).empty()) {
            values.push_back(splitCsvLine(line)[index]);
        }
    }
    return values;
}

// ---------- Printing ----------

void printTableSchema(std::ostream& out, const std::string& title, const TableSchema& table) {
    out << title << " from " << table.file << " (" << table.rowCount << " rows, " << table.columns.size()
        << " columns, fingerprint " << std::hex << std::showbase << table.fingerprint << std::dec
        << std::noshowbase << ")\n";

    size_t nameWidth = 8;
    for (const ColumnInfo& column : table.columns) {
        nameWidth = std::max(nameWidth, column.name.size() + 2);
    }
    const int width = static_cast<int>(nameWidth);
    out << std::left << "  " << std::setw(width) << "COLUMN" << std::setw(10) << "TYPE" << std::setw(10)
        << "NULLABLE" << std::setw(9) << "MISSING" << "RANGE\n";
    for (const ColumnInfo& column : table.columns) {
        const std::string range = isNumeric(column.type) && column.hasRange
                                      ? "[" + formatNumber(column.min) + ", " + formatNumber(column.max) + "]"
                                      : "-";
        out << "  " << std::setw(width) << column.name << std::setw(10) << columnTypeName(column.type)
            << std::setw(10) << (column.missingCount > 0 ? "yes" : "no") << std::setw(9) << column.missingCount
            << range << '\n';
    }
}

void printImageSetSchema(std::ostream& out, const std::string& title, const ImageSetSchema& set) {
    int good = 0;
    int minWidth = 0, maxWidth = 0, minHeight = 0, maxHeight = 0;
    std::map<std::string, int> formats;
    std::map<int, int> channels;
    std::vector<const ImageInfo*> corrupt;
    for (const ImageInfo& image : set.images) {
        ++formats[image.format];
        if (image.corrupt) {
            corrupt.push_back(&image);
            continue;
        }
        minWidth = good == 0 ? image.width : std::min(minWidth, image.width);
        maxWidth = good == 0 ? image.width : std::max(maxWidth, image.width);
        minHeight = good == 0 ? image.height : std::min(minHeight, image.height);
        maxHeight = good == 0 ? image.height : std::max(maxHeight, image.height);
        ++channels[image.channels];
        ++good;
    }

    out << title << " from " << set.root << " (" << set.images.size() << " image"
        << (set.images.size() == 1 ? "" : "s") << ", fingerprint " << std::hex << std::showbase << set.fingerprint
        << std::dec << std::noshowbase << ")\n";
    out << "  formats:  ";
    for (const auto& [format, count] : formats) {
        out << format << " " << count << "  ";
    }
    out << "\n  width:    " << minWidth << ".." << maxWidth << "\n  height:   " << minHeight << ".." << maxHeight
        << "\n  channels: ";
    for (const auto& [count, images] : channels) {
        out << count << " (" << images << " image" << (images == 1 ? "" : "s") << ")  ";
    }
    out << "\n  corrupt:  " << corrupt.size() << '\n';
    for (const ImageInfo* image : corrupt) {
        out << "    " << image->path << ": " << image->problem << '\n';
    }
}
