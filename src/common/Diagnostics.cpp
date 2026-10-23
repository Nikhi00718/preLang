#include "common/Diagnostics.h"

#include "common/Json.h"
#include "common/SourceManager.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

const char* phaseName(Phase phase) {
    switch (phase) {
        case Phase::Lexical:  return "lexical";
        case Phase::Syntax:   return "syntax";
        case Phase::Schema:   return "schema";
        case Phase::Semantic: return "semantic";
        case Phase::Type:     return "type";
        case Phase::Contract: return "contract";
    }
    return "";
}

char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

// Levenshtein distance ignoring case: the number of single-character edits
// (insert, delete, replace) that turn `a` into `b`.
size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> previousRow(b.size() + 1);
    std::vector<size_t> currentRow(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) {
        previousRow[j] = j;
    }
    for (size_t i = 1; i <= a.size(); ++i) {
        currentRow[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const bool same = upper(a[i - 1]) == upper(b[j - 1]);
            currentRow[j] = std::min({previousRow[j] + 1,                     // delete
                                      currentRow[j - 1] + 1,                  // insert
                                      previousRow[j - 1] + (same ? 0 : 1)});  // replace
        }
        std::swap(previousRow, currentRow);
    }
    return previousRow[b.size()];
}

std::vector<Diagnostic> inSourceOrder(std::vector<Diagnostic> diagnostics) {
    std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& a, const Diagnostic& b) {
        return a.location.line != b.location.line ? a.location.line < b.location.line
                                                  : a.location.column < b.location.column;
    });
    return diagnostics;
}

}  // namespace

Diagnostic& Diagnostics::error(Phase phase, SourceLocation location, std::string message, std::string hint) {
    ++errorCount_;
    all_.push_back({Severity::Error, phase, location, std::move(message), std::move(hint), {}});
    return all_.back();
}

Diagnostic& Diagnostics::warning(Phase phase, SourceLocation location, std::string message, std::string hint) {
    all_.push_back({Severity::Warning, phase, location, std::move(message), std::move(hint), {}});
    return all_.back();
}

// Output format:
//   error[syntax]: expected ';' at the end of the statement, found 'SAVE'
//    --> examples/invalid.prep:10:19
//     |
//   10 | DROP_NULL students
//     |                   ^
void Diagnostics::print(std::ostream& out, const SourceManager& source) const {
    for (const Diagnostic& d : inSourceOrder(all_)) {
        const std::string lineNumber = std::to_string(d.location.line);
        const std::string gutter(lineNumber.size(), ' ');
        const std::string caretIndent(static_cast<size_t>(d.location.column - 1), ' ');

        out << (d.severity == Severity::Error ? "error[" : "warning[") << phaseName(d.phase) << "]: " << d.message
            << '\n';
        out << gutter << " --> " << source.path() << ':' << d.location.line << ':' << d.location.column << '\n';
        out << gutter << " |\n";
        out << lineNumber << " | " << source.line(d.location.line) << '\n';
        out << gutter << " | " << caretIndent << "^\n";
        for (const std::string& note : d.notes) {
            out << gutter << " = note: " << note << '\n';
        }
        if (!d.hint.empty()) {
            out << gutter << " = hint: " << d.hint << '\n';
        }
        out << '\n';
    }
}

// Output format (one object; diagnostics in source order, line and column from 1):
//   {"file": "a.prep", "errors": 1, "warnings": 0, "diagnostics": [
//     {"severity": "error", "phase": "syntax", "line": 10, "column": 19,
//      "message": "...", "hint": "...", "notes": []}]}
void Diagnostics::printJson(std::ostream& out, const SourceManager& source) const {
    out << "{\"file\": " << jsonString(source.path()) << ", \"errors\": " << errorCount()
        << ", \"warnings\": " << warningCount() << ", \"diagnostics\": [";
    bool first = true;
    for (const Diagnostic& d : inSourceOrder(all_)) {
        out << (first ? "\n" : ",\n") << "  {\"severity\": \"" << (d.severity == Severity::Error ? "error" : "warning")
            << "\", \"phase\": \"" << phaseName(d.phase) << "\", \"line\": " << d.location.line
            << ", \"column\": " << d.location.column << ", \"message\": " << jsonString(d.message)
            << ", \"hint\": " << jsonString(d.hint) << ", \"notes\": [";
        for (size_t i = 0; i < d.notes.size(); ++i) {
            out << (i > 0 ? ", " : "") << jsonString(d.notes[i]);
        }
        out << "]}";
        first = false;
    }
    out << (first ? "]}\n" : "\n]}\n");
}

std::string suggestClosest(const std::string& word, const std::vector<std::string>& candidates) {
    std::string best;
    size_t bestDistance = word.size() <= 3 ? 2 : 3;  // short words may differ by 1 edit, longer ones by 2
    for (const std::string& candidate : candidates) {
        const size_t distance = editDistance(word, candidate);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best.empty() ? "" : "did you mean '" + best + "'?";
}
