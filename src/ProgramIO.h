#pragma once

#include <string>

namespace prep {

bool readTextFile(const std::string& path, std::string& source);
void printSource(const std::string& source);

}  // namespace prep
