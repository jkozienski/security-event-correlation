#include "correlation/sequential.hpp"

namespace correlation {
std::vector<Match> search_sequential(const std::vector<Event>& history,
                                     const Event& query, const SearchConfig& config) {
    validate_event(query, query.features.size());
    validate_events(history);
    if (!history.empty()) validate_event(query, history.front().features.size());
    validate_config(config, query.features.size());
    std::vector<Match> matches;
    
    for (const auto& event : history) {
        if (in_history_window(query, event, config.window_seconds))
            matches.push_back({event.id, event.timestamp, similarity(query, event, config.weights)});
    }
    select_matches(matches, config);
    return matches;
}
} // namespace correlation
