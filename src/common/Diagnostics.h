#pragma once

#include <ostream>
#include <string>
#include <vector>

class SourceManager;

// A position in the .prep file. Line and column both start at 1.
struct SourceLocation {
    int line = 1;
    int column = 1;
};

enum class Severity { Error, Warning };

// The compiler phase that found the problem.
enum class Phase { Lexical, Syntax, Schema, Semantic, Type, Contract };

struct Diagnostic {
    Severity severity;
    Phase phase;
    SourceLocation location;
    std::string message;
    std::string hint;                // suggestion for fixing it; empty if there is none
    std::vector<std::string> notes;  // extra lines, e.g. one per column matched by a wildcard
};

// Collects errors and warnings from every phase so they can be printed together.
class Diagnostics {
public:
    // Both return the new diagnostic so notes can be added to it.
    Diagnostic& error(Phase phase, SourceLocation location, std::string message, std::string hint = "");
    Diagnostic& warning(Phase phase, SourceLocation location, std::string message, std::string hint = "");

    bool hasErrors() const { return errorCount_ > 0; }
    int errorCount() const { return errorCount_; }
    int warningCount() const { return static_cast<int>(all_.size()) - errorCount_; }
    const std::vector<Diagnostic>& all() const { return all_; }

    // Prints every diagnostic in source order, with its line and a ^ under the column.
    void print(std::ostream& out, const SourceManager& source) const;

private:
    std::vector<Diagnostic> all_;
    int errorCount_ = 0;
};

// Returns "did you mean 'x'?" for the candidate closest to `word` (ignoring
// case, at most 2 edits away), or an empty string if none is close enough.
std::string suggestClosest(const std::string& word, const std::vector<std::string>& candidates);
