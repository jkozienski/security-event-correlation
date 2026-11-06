#include "correlation/common.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace correlation {
std::vector<double> normalize_features(const std::vector<double>& values,
                                     const std::vector<FeatureRange>& ranges) {
    if (values.size() != ranges.size()) throw std::invalid_argument("Feature/range dimension mismatch");
    std::vector<double> result;
    result.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto r = ranges[i];
        const double span = r.max - r.min;
        if (!std::isfinite(values[i]) || !std::isfinite(r.min) || !std::isfinite(r.max) ||
            r.max < r.min || !std::isfinite(span) || values[i] < r.min || values[i] > r.max)
            throw std::invalid_argument("Invalid feature or normalization range");
        result.push_back(span == 0.0 ? 0.0 : (values[i] - r.min) / span);
    }
    return result;
}
void validate_event(const Event& e, std::size_t count) {
    if (e.features.size() != count) throw std::invalid_argument("Inconsistent feature count");
    for (double v : e.features)
        if (!std::isfinite(v) || v < 0.0 || v > 1.0)
            throw std::invalid_argument("Features must be finite and normalized to [0,1]");
}
void validate_events(const std::vector<Event>& events) {
    std::unordered_set<std::uint64_t> ids;
    const auto count = events.empty() ? 0 : events.front().features.size();
    for (const auto& e : events) {
        validate_event(e, count);
        if (!ids.insert(e.id).second) throw std::invalid_argument("Duplicate event ID");
    }
}
void validate_config(const SearchConfig& c, std::size_t count) {
    if (c.mode != SearchMode::TopK && c.mode != SearchMode::Threshold)
        throw std::invalid_argument("Unknown search mode");
    if (!std::isfinite(c.threshold) || c.threshold < 0.0 || c.threshold > 1.0)
        throw std::invalid_argument("Threshold must be in [0,1]");
    double total = 0;
    for (double w : {c.weights.operation, c.weights.device, c.weights.account, c.weights.source, c.weights.destination, c.weights.numeric}) {
        if (!std::isfinite(w) || w < 0.0) throw std::invalid_argument("Invalid similarity weight");
    }
    total = c.weights.operation + c.weights.device + c.weights.account + c.weights.source + c.weights.destination + (count ? c.weights.numeric : 0.0);
    if (!std::isfinite(total) || total <= 0.0) throw std::invalid_argument("Active weights must have a positive finite sum");
}
bool in_history_window(const Event& q, const Event& e, std::uint64_t window) {
    return e.id != q.id && e.timestamp <= q.timestamp && q.timestamp - e.timestamp <= window;
}
double similarity(const Event& a, const Event& b, const Weights& w) {
    double sum = w.operation * (a.operation_id == b.operation_id)
               + w.device * (a.device_id == b.device_id)
               + w.account * (a.account_id == b.account_id)
               + w.source * (a.source_id == b.source_id)
               + w.destination * (a.destination_id == b.destination_id);
    double total = w.operation + w.device + w.account + w.source + w.destination;
    if (!a.features.empty()) {
        double distance = 0.0;
        for (std::size_t i = 0; i < a.features.size(); ++i)
            distance += std::abs(a.features[i] - b.features[i]);
        sum += w.numeric * (1.0 - distance / static_cast<double>(a.features.size()));
        total += w.numeric;
    }
    return std::clamp(sum / total, 0.0, 1.0);
}
bool better_match(const Match& a, const Match& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
    return a.event_id < b.event_id;
}
void select_matches(std::vector<Match>& matches, const SearchConfig& c) {
    if (c.mode == SearchMode::Threshold) {
        matches.erase(std::remove_if(matches.begin(), matches.end(), [&](const Match& m) {
            return m.score < c.threshold;
        }), matches.end());
        std::sort(matches.begin(), matches.end(), better_match);
    } else {
        const auto count = std::min(c.k, matches.size());
        std::partial_sort(matches.begin(), matches.begin() + count, matches.end(), better_match);
        matches.resize(count);
    }
}
} // namespace correlation
