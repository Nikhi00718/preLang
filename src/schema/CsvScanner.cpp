#include "schema/CsvScanner.h"

#include "schema/Checksum.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <thread>

namespace {

// ---------- Cells ----------

std::string_view trim(std::string_view text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool isBlank(std::string_view text) { return text.find_first_not_of(" \t\r") == std::string_view::npos; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool equalsIgnoreCase(std::string_view text, const char* word) {
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i] >= 'A' && text[i] <= 'Z' ? static_cast<char>(text[i] - 'A' + 'a') : text[i];
        if (c != word[i]) {
            return false;
        }
    }
    return word[text.size()] == '\0';
}

// Moves i past a run of digits, adding them to `digits` (while it stays exact)
// and counting them; returns false if there was none.
bool readDigits(std::string_view text, size_t& i, std::uint64_t& digits, int& count) {
    const size_t start = i;
    while (i < text.size() && isDigit(text[i])) {
        if (count < 19) {
            digits = digits * 10 + static_cast<std::uint64_t>(text[i] - '0');
        }
        ++count;
        ++i;
    }
    return i > start;
}

// The most specific type for one non-empty cell, and for a number its value.
// Numbers are an optional '-', digits, an optional '.' and digits, and an
// optional exponent such as e-05 (pandas writes small floats that way). A
// number with '.' or an exponent is FLOAT.
//
// The value is computed while the cell is checked. When the digits fit in
// 2^53 and the power of ten is at most 10^22, both are exact doubles and one
// multiplication or division rounds correctly (Clinger's fast path), so the
// result equals std::from_chars; any other number is handed to std::from_chars.
ColumnType typeOfCell(std::string_view cell, double& value) {
    if ((cell.size() == 4 || cell.size() == 5) && (equalsIgnoreCase(cell, "true") || equalsIgnoreCase(cell, "false"))) {
        return ColumnType::Boolean;
    }
    static const double powers[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const bool negative = cell[0] == '-';
    size_t i = negative ? 1 : 0;
    std::uint64_t digits = 0;
    int count = 0;
    if (!readDigits(cell, i, digits, count)) {
        return ColumnType::String;
    }
    bool isFloat = false;
    int scale = 0;  // the value is digits * 10^scale
    if (i < cell.size() && cell[i] == '.') {
        ++i;
        const int before = count;
        if (!readDigits(cell, i, digits, count)) {
            return ColumnType::String;
        }
        scale = -(std::min(count, 19) - std::min(before, 19));
        isFloat = true;
    }
    if (i < cell.size() && (cell[i] == 'e' || cell[i] == 'E')) {
        ++i;
        const bool down = i < cell.size() && cell[i] == '-';
        if (i < cell.size() && (cell[i] == '-' || cell[i] == '+')) {
            ++i;
        }
        std::uint64_t exponent = 0;
        int exponentDigits = 0;
        if (!readDigits(cell, i, exponent, exponentDigits)) {
            return ColumnType::String;
        }
        scale += exponentDigits > 4 ? 100000 : (down ? -static_cast<int>(exponent) : static_cast<int>(exponent));
        isFloat = true;
    }
    if (i != cell.size()) {
        return ColumnType::String;
    }
    if (count <= 15 && scale >= -22 && scale <= 22) {
        const double mantissa = static_cast<double>(digits);
        value = scale < 0 ? mantissa / powers[-scale] : mantissa * powers[scale];
        value = negative ? -value : value;
    } else {
        std::from_chars(cell.data(), cell.data() + cell.size(), value);
    }
    return isFloat ? ColumnType::Float : ColumnType::Integer;
}

// Splits one line into trimmed fields. A line without a double quote (almost
// every line) is split in place; a field inside double quotes may contain
// commas, and "" inside quotes stands for one quote character, so a line with
// quotes is copied into `owned` first.
void splitLine(std::string_view line, std::vector<std::string_view>& fields, std::vector<std::string>& owned) {
    fields.clear();
    if (line.find('"') == std::string_view::npos) {
        size_t start = 0;
        while (true) {
            const size_t comma = line.find(',', start);
            if (comma == std::string_view::npos) {
                fields.push_back(trim(line.substr(start)));
                return;
            }
            fields.push_back(trim(line.substr(start, comma - start)));
            start = comma + 1;
        }
    }
    owned.clear();
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
            owned.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    owned.push_back(field);
    for (const std::string& text : owned) {
        fields.push_back(trim(text));
    }
}

// Position of the first byte that is not part of valid UTF-8, or npos.
// Eight bytes of plain ASCII are skipped at a time. (Overlong forms are not
// checked; no editor writes them.)
size_t invalidUtf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        if (i + 8 <= text.size()) {
            std::uint64_t word;
            std::memcpy(&word, text.data() + i, 8);
            if ((word & 0x8080808080808080ull) == 0) {
                i += 8;
                continue;
            }
        }
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t length = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >> 4) == 0xE ? 3
                                             : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (length == 0 || i + length > text.size()) {
            return i;
        }
        for (size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return i;
            }
        }
        i += length;
    }
    return std::string_view::npos;
}

// Calls visit(line) for every line of `text`, without the line break. A last
// line without a line break counts; an empty piece after the last break does not.
template <typename Visit>
void forEachLine(std::string_view text, Visit visit) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (!visit(text.substr(start, end - start), start)) {
            return;
        }
        start = end + 1;
    }
}

// ---------- One piece of the file ----------

// What one thread learns about one column. "first..." are line indexes inside
// the piece (-1: never), and the cell found there.
struct ColumnFacts {
    std::int64_t missing = 0;
    bool seen[4] = {false, false, false, false};  // indexed by ColumnType
    bool hasRange = false;
    double min = 0;
    double max = 0;
    std::int64_t firstString = -1, firstBoolean = -1, firstNumber = -1;
    std::string stringCell, booleanCell, numberCell;
};

struct PieceFacts {
    std::int64_t lines = 0;  // every line, blank ones too
    std::int64_t rows = 0;
    std::vector<ColumnFacts> columns;
    CsvProblem problem;      // line is an index inside the piece
};

void notice(std::int64_t& first, std::string& cell, std::int64_t line, std::string_view text) {
    if (first < 0) {
        first = line;
        cell = std::string(text);
    }
}

PieceFacts scanPiece(std::string_view piece, size_t columnCount) {
    PieceFacts facts;
    facts.columns.resize(columnCount);
    const size_t badByte = invalidUtf8(piece);
    std::vector<std::string_view> cells;
    std::vector<std::string> owned;

    forEachLine(piece, [&](std::string_view line, size_t start) {
        const std::int64_t index = facts.lines++;
        if (badByte != std::string_view::npos && badByte >= start && badByte <= start + line.size()) {
            facts.problem = {CsvProblem::Kind::NotUtf8, index, static_cast<unsigned char>(piece[badByte]), 0};
            return false;
        }
        if (isBlank(line)) {
            return true;
        }
        splitLine(line, cells, owned);
        if (cells.size() != columnCount) {
            facts.problem = {CsvProblem::Kind::FieldCount, index, 0, cells.size()};
            return false;
        }
        ++facts.rows;
        for (size_t i = 0; i < columnCount; ++i) {
            ColumnFacts& column = facts.columns[i];
            const std::string_view cell = cells[i];
            if (cell.empty()) {
                ++column.missing;
                continue;
            }
            double value = 0;
            const ColumnType type = typeOfCell(cell, value);
            column.seen[static_cast<int>(type)] = true;
            if (type == ColumnType::String) {
                notice(column.firstString, column.stringCell, index, cell);
            } else if (type == ColumnType::Boolean) {
                notice(column.firstBoolean, column.booleanCell, index, cell);
            } else {
                notice(column.firstNumber, column.numberCell, index, cell);
                column.min = column.hasRange ? std::min(column.min, value) : value;
                column.max = column.hasRange ? std::max(column.max, value) : value;
                column.hasRange = true;
            }
        }
        return true;
    });
    return facts;
}

// ---------- Merging the pieces ----------

// First line of the file where the column's type becomes STRING, and the cell
// on that line. Folding cell types from the first line, the type becomes
// STRING at the first text cell, or where a boolean and a number have both
// appeared, whichever comes first.
void stringReason(const ColumnFacts& column, ColumnInfo& info) {
    std::int64_t line = column.firstString;
    const std::string* cell = &column.stringCell;
    if (column.firstBoolean >= 0 && column.firstNumber >= 0) {
        const bool numberLater = column.firstNumber > column.firstBoolean;
        const std::int64_t mixed = numberLater ? column.firstNumber : column.firstBoolean;
        if (line < 0 || mixed < line) {
            line = mixed;
            cell = numberLater ? &column.numberCell : &column.booleanCell;
        }
    }
    if (line >= 0) {
        info.stringReason = "line " + std::to_string(line) + " has \"" + *cell + "\"";
    }
}

ColumnType finalType(const ColumnFacts& column) {
    const bool boolean = column.seen[static_cast<int>(ColumnType::Boolean)];
    const bool integer = column.seen[static_cast<int>(ColumnType::Integer)];
    const bool real = column.seen[static_cast<int>(ColumnType::Float)];
    if (column.seen[static_cast<int>(ColumnType::String)] || (boolean && (integer || real))) {
        return ColumnType::String;
    }
    if (boolean) {
        return ColumnType::Boolean;
    }
    if (real) {
        return ColumnType::Float;
    }
    return integer ? ColumnType::Integer : ColumnType::String;  // no values at all: STRING
}

// Adds a later piece's facts to the total; `offset` turns its line indexes into file line numbers.
void merge(ColumnFacts& total, const ColumnFacts& piece, std::int64_t offset) {
    total.missing += piece.missing;
    for (int t = 0; t < 4; ++t) {
        total.seen[t] = total.seen[t] || piece.seen[t];
    }
    if (piece.hasRange) {
        total.min = total.hasRange ? std::min(total.min, piece.min) : piece.min;
        total.max = total.hasRange ? std::max(total.max, piece.max) : piece.max;
        total.hasRange = true;
    }
    const auto first = [&](std::int64_t& line, std::string& cell, std::int64_t pieceLine, const std::string& pieceCell) {
        if (line < 0 && pieceLine >= 0) {
            line = pieceLine + offset;
            cell = pieceCell;
        }
    };
    first(total.firstString, total.stringCell, piece.firstString, piece.stringCell);
    first(total.firstBoolean, total.booleanCell, piece.firstBoolean, piece.booleanCell);
    first(total.firstNumber, total.numberCell, piece.firstNumber, piece.numberCell);
}

// Cuts `body` into about `count` pieces that end just after a line break.
std::vector<std::string_view> cutAtLineBreaks(std::string_view body, unsigned count) {
    std::vector<std::string_view> pieces;
    size_t start = 0;
    for (unsigned i = 1; i <= count && start < body.size(); ++i) {
        size_t end = i == count ? body.size() : std::max(start, body.size() / count * i);
        end = end >= body.size() ? body.size() : body.find('\n', end);
        end = end == std::string_view::npos ? body.size() : end + 1;
        pieces.push_back(body.substr(start, end - start));
        start = end;
    }
    return pieces;
}

// The header line, without a UTF-8 byte-order mark (Excel may add one), and the rest of the file.
void splitHeader(std::string_view bytes, std::string_view& header, std::string_view& body) {
    if (bytes.substr(0, 3) == "\xEF\xBB\xBF") {
        bytes.remove_prefix(3);
    }
    const size_t end = bytes.find('\n');
    header = bytes.substr(0, end);
    body = end == std::string_view::npos ? std::string_view{} : bytes.substr(end + 1);
}

}  // namespace

bool readFileBytes(const std::string& path, std::string& bytes) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    std::FILE* file = error ? nullptr : std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    std::setvbuf(file, nullptr, _IONBF, 0);  // one large read straight into the string, no stdio buffer
    bytes.resize(static_cast<size_t>(size));
    bytes.resize(std::fread(bytes.data(), 1, bytes.size(), file));
    std::fclose(file);
    return true;
}

CsvScan scanCsv(const std::string& bytes, unsigned threads) {
    CsvScan scan;
    // The checksum runs on its own thread while the cells are analysed.
    std::future<std::uint32_t> fingerprint = std::async(std::launch::async, [&bytes] { return crc32(bytes); });

    std::string_view header, body;
    splitHeader(bytes, header, body);
    if (bytes.empty()) {
        scan.problem.kind = CsvProblem::Kind::Empty;
        scan.fingerprint = fingerprint.get();
        return scan;
    }
    const size_t bad = invalidUtf8(header);
    if (bad != std::string_view::npos) {
        scan.problem = {CsvProblem::Kind::NotUtf8, 1, static_cast<unsigned char>(header[bad]), 0};
        scan.fingerprint = fingerprint.get();
        return scan;
    }
    std::vector<std::string_view> names;
    std::vector<std::string> owned;
    splitLine(header, names, owned);
    for (std::string_view name : names) {
        ColumnInfo column;
        column.name = std::string(name);
        scan.columns.push_back(column);
    }

    // Small files are not worth a thread each.
    if (threads == 0) {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    const unsigned count = std::min<unsigned>(threads, static_cast<unsigned>(body.size() / (1 << 20)) + 1);
    const std::vector<std::string_view> pieces = cutAtLineBreaks(body, count);
    std::vector<std::future<PieceFacts>> work;
    for (size_t i = 1; i < pieces.size(); ++i) {
        work.push_back(std::async(std::launch::async, scanPiece, pieces[i], names.size()));
    }
    std::vector<PieceFacts> results;
    results.push_back(pieces.empty() ? PieceFacts{} : scanPiece(pieces[0], names.size()));
    results.back().columns.resize(names.size());
    for (std::future<PieceFacts>& piece : work) {
        results.push_back(piece.get());
    }

    // Merge in file order; the first problem in the file wins.
    std::vector<ColumnFacts> total(names.size());
    std::int64_t offset = 2;  // line numbers start at 1, and line 1 is the header
    for (const PieceFacts& piece : results) {
        if (piece.problem.kind != CsvProblem::Kind::None) {
            scan.problem = piece.problem;
            scan.problem.line += offset;
            break;
        }
        for (size_t i = 0; i < names.size(); ++i) {
            merge(total[i], piece.columns[i], offset);
        }
        scan.rowCount += piece.rows;
        offset += piece.lines;
    }
    for (size_t i = 0; i < names.size(); ++i) {
        ColumnInfo& column = scan.columns[i];
        const ColumnFacts& facts = total[i];
        column.type = finalType(facts);
        column.missingCount = facts.missing;
        column.hasRange = facts.hasRange;
        column.min = facts.min;
        column.max = facts.max;
        // Only a column that also holds numbers or booleans needs an explanation for being STRING.
        if (column.type == ColumnType::String && (facts.firstBoolean >= 0 || facts.firstNumber >= 0)) {
            stringReason(facts, column);
        }
    }
    scan.fingerprint = fingerprint.get();
    return scan;
}

std::vector<std::string> csvColumnValues(const std::string& bytes, const std::string& column) {
    std::string_view header, body;
    splitHeader(bytes, header, body);
    std::vector<std::string_view> cells;
    std::vector<std::string> owned;
    splitLine(header, cells, owned);
    const auto found = std::find(cells.begin(), cells.end(), column);
    if (found == cells.end()) {
        return {};
    }
    const size_t index = static_cast<size_t>(found - cells.begin());
    std::vector<std::string> values;
    forEachLine(body, [&](std::string_view line, size_t) {
        if (!isBlank(line)) {
            splitLine(line, cells, owned);
            values.emplace_back(index < cells.size() ? cells[index] : std::string_view{});
        }
        return true;
    });
    return values;
}
