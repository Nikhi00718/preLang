#include "common/SourceManager.h"

#include <fstream>
#include <iomanip>
#include <sstream>

bool SourceManager::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    path_ = path;
    text_.clear();
    for (char c : buffer.str()) {
        if (c != '\r') {  // Windows line endings become plain '\n'
            text_ += c;
        }
    }
    if (text_.compare(0, 3, "\xEF\xBB\xBF") == 0) {  // Notepad may add a UTF-8 byte-order mark
        text_.erase(0, 3);
    }

    lines_.clear();
    std::istringstream stream(text_);
    std::string current;
    while (std::getline(stream, current)) {
        lines_.push_back(current);
    }
    return true;
}

std::string SourceManager::line(int lineNumber) const {
    if (lineNumber < 1 || lineNumber > static_cast<int>(lines_.size())) {
        return "";
    }
    return lines_[lineNumber - 1];
}

void SourceManager::print(std::ostream& out) const {
    for (size_t i = 0; i < lines_.size(); ++i) {
        out << std::right << std::setw(4) << i + 1 << " | " << lines_[i] << '\n';
    }
}
