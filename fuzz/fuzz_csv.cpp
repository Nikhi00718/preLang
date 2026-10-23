// Fuzz target: any bytes as a CSV file, through the compile-time scanner and the column reader
// that FILL_NULL ... MODE and the certificates use. A problem in the file is reported in
// CsvScan::problem; a crash, a hang or a sanitizer report is a bug.

#include "schema/CsvScanner.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string bytes(reinterpret_cast<const char*>(data), size);
    const CsvScan scan = scanCsv(bytes, 1);
    if (scan.problem.kind == CsvProblem::Kind::None && !scan.columns.empty()) {
        csvColumnValues(bytes, scan.columns.front().name);
    }
    return 0;
}
