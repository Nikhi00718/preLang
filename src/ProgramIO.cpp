#include "ProgramIO.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace prep {

bool readTextFile(const std::string& path, std::string& source) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    source = buffer.str();
    return true;
}

void printSource(const std::string& source) {
    std::cout << "\n--- SOURCE PROGRAM -----------------------------------------\n";
    std::istringstream input(source);
    std::string line;
    int lineNumber = 1;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::cout << std::right << std::setw(3) << lineNumber++ << " | " << line << '\n';
    }
}

}  // namespace prep
