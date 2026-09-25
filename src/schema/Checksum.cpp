#include "schema/Checksum.h"

#include <vector>

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
