#include "Diagnostics.h"

#include <cctype>
#include <sstream>
#include <utility>

namespace prep {

ErrorReporter::ErrorReporter(std::string source) : source_(std::move(source)) {}

void ErrorReporter::report(Diagnostic diagnostic) {
    errors_.push_back(std::move(diagnostic));
}

bool ErrorReporter::ok() const {
    return errors_.empty();
}

const std::vector<Diagnostic>& ErrorReporter::errors() const {
    return errors_;
}

std::string ErrorReporter::sourceLine(int wantedLine) const {
    if (wantedLine < 1) return "";

    std::size_t start = 0;
    for (int line = 1; line < wantedLine; ++line) {
        std::size_t newline = source_.find('\n', start);
        if (newline == std::string::npos) return "";
        start = newline + 1;
    }

    std::size_t end = source_.find('\n', start);
    if (end == std::string::npos) end = source_.size();
    if (end > start && source_[end - 1] == '\r') --end;
    return source_.substr(start, end - start);
}

std::string ErrorReporter::render() const {
    std::ostringstream out;
    for (std::size_t i = 0; i < errors_.size(); ++i) {
        if (i > 0) out << "\n\n" << std::string(60, '-') << "\n\n";
        const Diagnostic& error = errors_[i];
        out << error.phase;
        if (error.line > 0) out << " - Line " << error.line;
        out << "\n\nProblem:\n    " << error.problem;

        if (error.line > 0) {
            out << "\n\nSource:\n    " << sourceLine(error.line);
            if (error.column > 0) {
                out << "\n    " << std::string(static_cast<std::size_t>(error.column - 1), ' ') << '^';
            }
        }
        if (!error.expected.empty()) out << "\n\nExpected:\n    " << error.expected;
        if (!error.found.empty()) out << "\n\nFound:\n    " << error.found;
        if (!error.hint.empty()) out << "\n\nHint:\n    " << error.hint;
    }
    return out.str();
}

std::string ErrorReporter::summary() const {
    return std::to_string(errors_.size()) + " error(s)";
}

std::string quoteText(const std::string& text) {
    return "'" + text + "'";
}

std::string upper(const std::string& text) {
    std::string result = text;
    for (char& character : result) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return result;
}

}  // namespace prep
