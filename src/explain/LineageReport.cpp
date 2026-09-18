#include "explain/LineageReport.h"

#include <iomanip>

void printLineage(std::ostream& out, const LineageTracker& lineage) {
    for (const auto& [subject, events] : lineage.history()) {
        out << subject << '\n';
        for (size_t i = 0; i < events.size(); ++i) {
            const LineageEvent& event = events[i];
            const std::string step = "line " + std::to_string(event.line) + ": " + event.operation;
            out << "  " << (i + 1 == events.size() ? "`-- " : "|-- ") << std::left << std::setw(48) << step << "  "
                << event.result << '\n';
        }
        out << '\n';
    }
}
