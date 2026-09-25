#pragma once

#include <cstdint>
#include <string>

// CRC-32, the same checksum as Python's zlib.crc32. The generated program
// computes it again and refuses to run when an input changed after compilation.
std::uint32_t crc32(const std::string& bytes);
