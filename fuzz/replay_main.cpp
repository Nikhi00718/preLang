// A main() for the fuzz targets when they are not built with libFuzzer (PREPLANG_FUZZ off, or a
// compiler without it, such as MSVC or GCC): it runs the target once on every file named on the
// command line, and on every file under every folder named, so that each build replays the seed
// corpus and any crash a fuzzer found (saved into fuzz/corpus/) as an ordinary test.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

void runFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

}  // namespace

int main(int argc, char* argv[]) {
    int files = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path = argv[i];
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    runFile(entry.path());
                    ++files;
                }
            }
        } else if (std::filesystem::is_regular_file(path)) {
            runFile(path);
            ++files;
        } else {
            std::cerr << "error: no file or folder '" << path.string() << "'\n";
            return 1;
        }
    }
    std::cout << "replayed " << files << " input(s)\n";
    return 0;
}
