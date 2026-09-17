#pragma once

#include <ostream>
#include <string>
#include <vector>

// Owns the text of the .prep program. Diagnostics use it to show the line
// where an error happened.
class SourceManager {
public:
    // Reads the whole file. Returns false if the file cannot be opened.
    bool loadFile(const std::string& path);

    const std::string& path() const { return path_; }
    const std::string& text() const { return text_; }

    // Text of a line, counting from 1. Empty if the line does not exist.
    std::string line(int lineNumber) const;

    // Prints the program with line numbers.
    void print(std::ostream& out) const;

private:
    std::string path_;
    std::string text_;
    std::vector<std::string> lines_;
};
