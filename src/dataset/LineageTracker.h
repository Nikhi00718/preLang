#pragma once

#include <map>
#include <string>
#include <vector>

// One step in the history of a column or an image set.
struct LineageEvent {
    int line;
    std::string operation;  // e.g. "FILL_NULL 0"
    std::string result;     // e.g. "INTEGER [0, 23]"
};

// Records how every column and image set changes through the program, so
// --lineage can explain where each output value came from.
class LineageTracker {
public:
    // `subject` is "dataset.column", "dataset.table.column" or an image dataset name.
    void record(const std::string& subject, int line, std::string operation, std::string result);

    // Subjects in the order they were first seen, with their events.
    std::vector<std::pair<std::string, std::vector<LineageEvent>>> history() const;

private:
    std::vector<std::string> order_;
    std::map<std::string, std::vector<LineageEvent>> events_;
};
