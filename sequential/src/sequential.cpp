#include "correlation/sequential.hpp"
#include <algorithm>

namespace correlation {
void validate_batch(const std::vector<Event>& history, const std::vector<Event>& queries,
                    const SearchConfig& config) {
    const auto dimension = !history.empty() ? history.front().features.size()
                         : (!queries.empty() ? queries.front().features.size() : 0);
    validate_events(history);
    for (const auto& query : queries) validate_event(query, dimension);
    validate_config(config, dimension);
}

std::vector<Match> search_unchecked(const std::vector<Event>& history,
                                    const Event& query, const SearchConfig& config) {
    std::vector<Match> matches;
    if (config.mode == SearchMode::Threshold) {
        for (const auto& event : history) {
            if (!in_history_window(query, event, config.window_seconds)) continue;
            const double score = similarity(query, event, config.weights);
            if (score >= config.threshold) matches.push_back({event.id, event.timestamp, score});
        }
        std::sort(matches.begin(), matches.end(), better_match);
        return matches;
    }
    if (config.k == 0) return matches;
    matches.reserve(std::min(config.k, history.size()));
    for (const auto& event : history) {
        if (!in_history_window(query, event, config.window_seconds)) continue;
        const Match candidate{event.id, event.timestamp, similarity(query, event, config.weights)};
        // Heap with better_match as "less": the top is the worst kept match.
        if (matches.size() < config.k) {
            matches.push_back(candidate);
            std::push_heap(matches.begin(), matches.end(), better_match);
        } else if (better_match(candidate, matches.front())) {
            std::pop_heap(matches.begin(), matches.end(), better_match);
            matches.back() = candidate;
            std::push_heap(matches.begin(), matches.end(), better_match);
        }
    }
    std::sort_heap(matches.begin(), matches.end(), better_match); // best first
    return matches;
}

std::vector<Match> search_sequential(const std::vector<Event>& history,
                                     const Event& query, const SearchConfig& config) {
    validate_batch(history, {query}, config);
    return search_unchecked(history, query, config);
}

std::vector<std::vector<Match>> search_batch_sequential(const std::vector<Event>& history,
                                                         const std::vector<Event>& queries,
                                                         const SearchConfig& config) {
    validate_batch(history, queries, config);
    std::vector<std::vector<Match>> results;
    results.reserve(queries.size());
    for (const auto& query : queries) results.push_back(search_unchecked(history, query, config));
    return results;
}
} // namespace correlation
