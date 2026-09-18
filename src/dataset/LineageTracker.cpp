#include "dataset/LineageTracker.h"

#include <utility>

void LineageTracker::record(const std::string& subject, int line, std::string operation, std::string result) {
    std::vector<LineageEvent>& events = events_[subject];
    if (events.empty()) {
        order_.push_back(subject);
    }
    events.push_back({line, std::move(operation), std::move(result)});
}

std::vector<std::pair<std::string, std::vector<LineageEvent>>> LineageTracker::history() const {
    std::vector<std::pair<std::string, std::vector<LineageEvent>>> result;
    for (const std::string& subject : order_) {
        result.emplace_back(subject, events_.at(subject));
    }
    return result;
}
