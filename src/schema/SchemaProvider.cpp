#include "schema/SchemaProvider.h"

#include "common/Dates.h"
#include "schema/Checksum.h"
#include "schema/CsvScanner.h"
#include "schema/ParquetFooter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
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

// The format comes from the first bytes, not the file name, just as OpenCV
// decides when it decodes: a PNG screenshot saved as "shot.jpg" is a PNG image,
// possibly with transparency. A file that is neither is read by its extension,
// so the error says what is wrong with it.
ImageInfo readImageHeader(const fs::path& file, const std::string& relative) {
    ImageInfo info;
    info.path = relative;
    std::error_code error;
    info.bytes = fs::file_size(file, error);
    std::ifstream in(file, std::ios::binary);
    char first[2] = {0, 0};
    in.read(first, 2);
    const bool pngContent = first[0] == '\x89' && first[1] == 'P';
    const bool jpegContent = first[0] == '\xFF' && first[1] == '\xD8';
    const bool pngName = toLower(file.extension().string()) == ".png";
    info.format = pngContent || (pngName && !jpegContent) ? "PNG" : "JPG";
    in.clear();
    in.seekg(0);
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

std::optional<TableSchema> SchemaProvider::readTable(const std::string& path, const std::string& name,
                                                     SourceLocation location) {
    const auto overridden = overrides_.find(overrideKey(path, name));
    if (overridden != overrides_.end()) {
        TableSchema table = overridden->second;
        table.name = name;
        return table;
    }
    if (private_) {
        const auto released = released_.find(overrideKey(path, name));
        if (released == released_.end()) {
            diagnostics_.error(Phase::Schema, location, "the facts file has no table '" + name + "' from '" + path + "'",
                               "release facts for this program: preplang <file.prep> --queries, then "
                               "tools/release_facts.py");
            return std::nullopt;
        }
        return released->second;
    }
    if (toLower(fs::path(path).extension().string()) != ".parquet") {
        return readCsv(path, name, location);
    }
    std::string problem;
    std::optional<TableSchema> table = readParquetFooter(path, name, problem);
    if (!table) {
        std::error_code error;
        diagnostics_.error(Phase::Schema, location, problem,
                           fs::exists(path, error) ? "" : "the path is relative to the folder of the .prep file");
    }
    return table;
}

std::optional<TableSchema> SchemaProvider::readCsv(const std::string& csvPath, const std::string& name,
                                                   SourceLocation location) {
    std::string bytes;
    if (!readFileBytes(csvPath, bytes)) {
        diagnostics_.error(Phase::Schema, location, "cannot open CSV file '" + csvPath + "'",
                           "the path is relative to the folder of the .prep file");
        return std::nullopt;
    }
    CsvScan scan = scanCsv(bytes);
    const CsvProblem& problem = scan.problem;
    const std::string line = "line " + std::to_string(problem.line) + " of '" + csvPath + "'";
    switch (problem.kind) {
        case CsvProblem::Kind::None:
            break;
        case CsvProblem::Kind::Empty:
            diagnostics_.error(Phase::Schema, location, "CSV file '" + csvPath + "' is empty",
                               "the first line must list the column names");
            return std::nullopt;
        case CsvProblem::Kind::NotUtf8: {
            // pandas reads CSV files as UTF-8, so a file saved in another encoding
            // would stop the generated program. Every line is read here anyway, so say it now.
            const char* digits = "0123456789ABCDEF";
            diagnostics_.error(Phase::Schema, location,
                               line + " is not UTF-8 text (byte 0x" + digits[problem.byte >> 4] +
                                   digits[problem.byte & 15] + ")",
                               "the file was probably saved as Windows-1252 or Latin-1; save it as UTF-8 "
                               "(in Excel: Save As > CSV UTF-8)");
            return std::nullopt;
        }
        case CsvProblem::Kind::FieldCount:
            diagnostics_.error(Phase::Schema, location,
                               line + " has " + std::to_string(problem.fields) + " values but the header has " +
                                   std::to_string(scan.columns.size()) + " columns");
            return std::nullopt;
    }

    TableSchema table;
    table.name = name;
    table.file = csvPath;
    table.fingerprint = scan.fingerprint;
    table.rowCount = scan.rowCount;
    table.columns = std::move(scan.columns);
    return table;
}

std::optional<TableSetSchema> SchemaProvider::readTableFolder(const std::string& folder, SourceLocation location) {
    if (private_) {
        diagnostics_.error(Phase::Schema, location, "a private compilation (--facts) cannot load the folder '" +
                           folder + "': its files are not known without opening it", "LOAD each table instead");
        return std::nullopt;
    }
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        diagnostics_.error(Phase::Schema, location, "cannot open table folder '" + folder + "'",
                           "LOAD_TABLES expects a folder of CSV or Parquet files; use LOAD for one file");
        return std::nullopt;
    }
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        const std::string extension = toLower(entry.path().extension().string());
        if (entry.is_regular_file(error) && (extension == ".csv" || extension == ".parquet")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        diagnostics_.error(Phase::Schema, location, "folder '" + folder + "' has no CSV or Parquet files");
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
    const std::string key = imageKey(path, singleFile, recursive);
    const auto overridden = imageOverrides_.find(key);
    if (overridden != imageOverrides_.end()) {
        return overridden->second;
    }
    const auto known = images_.find(key);
    if (known != images_.end()) {
        return known->second;
    }
    if (private_) {
        diagnostics_.error(Phase::Schema, location, "a private compilation (--facts) cannot load images",
                           "facts are released for tables only");
        return std::nullopt;
    }
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
    images_[key] = set;
    return set;
}

// The date facts of a STRING column, from its values in the file: whether every
// present value is an ISO date in one layout, and the first and last date.
void readDateFacts(const TableSchema& schema, ColumnInfo& info) {
    info.datesKnown = true;
    info.allDates = true;
    info.hasDates = false;
    DateLayout first = DateLayout::None;
    const std::vector<std::string> values = readColumnValues(schema, info.name);
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].empty()) {
            continue;
        }
        double seconds = 0;
        const DateLayout layout = parseIsoDate(values[i], seconds);
        if (layout == DateLayout::None || (first != DateLayout::None && layout != first)) {
            info.allDates = false;
            info.hasDates = false;
            info.notDateReason = "row " + std::to_string(i + 1) + " has \"" + values[i] + "\"" +
                                 (layout == DateLayout::None ? std::string()
                                                             : ", not " + std::string(dateLayoutText(first)) +
                                                                   " like the rows before");
            return;
        }
        first = layout;
        info.dateMin = info.hasDates ? std::min(info.dateMin, seconds) : seconds;
        info.dateMax = info.hasDates ? std::max(info.dateMax, seconds) : seconds;
        info.hasDates = true;
    }
}

std::vector<std::string> readColumnValues(const TableSchema& table, const std::string& column) {
    if (table.format == TableFormat::Parquet) {
        return {};  // only the footer of a Parquet file is read at compile time
    }
    std::string bytes;
    return readFileBytes(table.file, bytes) ? csvColumnValues(bytes, column) : std::vector<std::string>{};
}

// ---------- Printing ----------

void printTableSchema(std::ostream& out, const std::string& title, const TableSchema& table) {
    const bool parquet = table.format == TableFormat::Parquet;
    out << title << " from " << table.file << " (" << table.rowCount << " rows, " << table.columns.size()
        << " columns, " << (parquet ? "footer " : "") << "fingerprint " << std::hex << std::showbase
        << table.fingerprint << std::dec << std::noshowbase << ")\n";
    if (parquet) {
        out << "  Parquet: only the footer statistics were read, not the data\n";
    }

    size_t nameWidth = 8;
    for (const ColumnInfo& column : table.columns) {
        nameWidth = std::max(nameWidth, column.name.size() + 2);
    }
    const int width = static_cast<int>(nameWidth);
    out << std::left << "  " << std::setw(width) << "COLUMN" << std::setw(10) << "TYPE" << std::setw(10)
        << "NULLABLE" << std::setw(9) << "MISSING" << "RANGE\n";
    for (const ColumnInfo& column : table.columns) {
        std::string range = "-";
        if (isNumeric(column.type) && column.hasRange) {
            range = "[" + formatNumber(column.min) + ", " + formatNumber(column.max) + "]" +
                    (column.boundsExact ? "" : " (bounds only)");
        }
        const std::string nullable = column.missingCount > 0 ? "yes" : column.missingKnown ? "no" : "maybe";
        const std::string missing = (column.missingKnown ? "" : ">=") + std::to_string(column.missingCount);
        out << "  " << std::setw(width) << column.name << std::setw(10) << columnTypeName(column.type)
            << std::setw(10) << nullable << std::setw(9) << missing << range << '\n';
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
