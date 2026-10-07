#include "schema/Checksum.h"

#include <array>
#include <cstring>

namespace {

// tables[0] is the usual byte-at-a-time CRC table; tables[k][b] is the CRC of
// byte b followed by k zero bytes. With them eight bytes are processed per step
// ("slicing-by-8"), which gives the same checksum several times faster.
using Tables = std::array<std::array<std::uint32_t, 256>, 8>;

Tables makeTables() {
    Tables tables{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        }
        tables[0][i] = value;
    }
    for (std::uint32_t i = 0; i < 256; ++i) {
        for (size_t k = 1; k < 8; ++k) {
            tables[k][i] = (tables[k - 1][i] >> 8) ^ tables[0][tables[k - 1][i] & 0xFFu];
        }
    }
    return tables;
}

}  // namespace

std::uint32_t crc32(const std::string& bytes) {
    static const Tables t = makeTables();
    const unsigned char* data = reinterpret_cast<const unsigned char*>(bytes.data());
    size_t size = bytes.size();
    std::uint32_t crc = 0xFFFFFFFFu;
    while (size >= 8) {
        std::uint32_t low, high;  // little-endian, as on every machine this compiler targets
        std::memcpy(&low, data, 4);
        std::memcpy(&high, data + 4, 4);
        low ^= crc;
        crc = t[7][low & 0xFF] ^ t[6][(low >> 8) & 0xFF] ^ t[5][(low >> 16) & 0xFF] ^ t[4][low >> 24] ^
              t[3][high & 0xFF] ^ t[2][(high >> 8) & 0xFF] ^ t[1][(high >> 16) & 0xFF] ^ t[0][high >> 24];
        data += 8;
        size -= 8;
    }
    while (size-- > 0) {
        crc = t[0][(crc ^ *data++) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}
