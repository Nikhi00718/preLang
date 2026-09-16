#pragma once

#include <string>
#include <vector>

namespace prep {

struct Diagnostic {
    std::string phase;
    std::string problem;
    int line = 0;
    int column = 0;
    std::string expected;
    std::string found;
    std::string hint;
};

class ErrorReporter {
public:
    explicit ErrorReporter(std::string source);

    void report(Diagnostic diagnostic);
    bool ok() const;
    const std::vector<Diagnostic>& errors() const;
    std::string render() const;
    std::string summary() const;

private:
    std::string sourceLine(int line) const;

    std::string source_;
    std::vector<Diagnostic> errors_;
};

std::string quoteText(const std::string& text);
std::string upper(const std::string& text);

}  // namespace prep
