// Fuzz target: any bytes as a Parquet file, through the footer reader (Thrift compact protocol).
// The reader takes a path, so each input is written to a file of this process first. A broken
// file is reported through `problem`; a crash, a hang or a sanitizer report is a bug.

#include "schema/ParquetFooter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // One file per process, so that parallel fuzzing jobs (-jobs=N) do not share it.
    static const std::filesystem::path file = std::filesystem::temp_directory_path() /
                                              ("preplang_fuzz_" + std::to_string(std::random_device{}()) + ".parquet");
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    std::string problem;
    readParquetFooter(file.string(), "t", problem);
    return 0;
}
