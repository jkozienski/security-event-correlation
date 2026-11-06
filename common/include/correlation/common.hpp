#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace correlation {
struct Event {
    std::uint64_t id = 0;
    std::uint64_t timestamp = 0; // Unix seconds, UTC
    std::uint32_t operation_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t account_id = 0;
    std::uint32_t source_id = 0;
    std::uint32_t destination_id = 0;
    std::vector<double> features; // finite, normalized to [0,1]
};
struct Weights {
    double operation = 1.0;
    double device = 1.0;
    double account = 1.0;
    double source = 1.0;
    double destination = 1.0;
    double numeric = 1.0; // weight of the entire numeric block
};
enum class SearchMode { TopK, Threshold };
struct SearchConfig {
    std::uint64_t window_seconds = 3600;
    SearchMode mode = SearchMode::TopK;
    std::size_t k = 10;
    double threshold = 0.8; // inclusive
    Weights weights;
};
struct Match {
    std::uint64_t event_id;
    std::uint64_t timestamp;
    double score;
};
struct FeatureRange { double min; double max; };
// Bounds must come from a shared schema/training set, never from each partition.
std::vector<double> normalize_features(const std::vector<double>& values,
                                     const std::vector<FeatureRange>& ranges);
void validate_event(const Event& event, std::size_t feature_count);
void validate_events(const std::vector<Event>& events);
void validate_config(const SearchConfig& config, std::size_t feature_count);
bool in_history_window(const Event& query, const Event& candidate,
                       std::uint64_t window_seconds);
// Preconditions: validated events of equal dimension and valid active weights.
double similarity(const Event& a, const Event& b, const Weights& weights);
bool better_match(const Match& a, const Match& b);
// Shared output selection for future OpenMP/MPI implementations.
void select_matches(std::vector<Match>& matches, const SearchConfig& config);
// Strict numeric CSV; header defines the variable feature count.
std::vector<Event> read_events_csv(std::istream& input);
void write_events_csv(std::ostream& output, const std::vector<Event>& events);
void write_matches_csv(std::ostream& output, std::uint64_t query_id,
                       const std::vector<Match>& matches, bool header = true);
} // namespace correlation
