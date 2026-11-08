#include "correlation/generator.hpp"
#include "correlation/openmp.hpp"
#include "correlation/sequential.hpp"
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid input was accepted");
}
bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }
// OpenMP changes only which thread answers a query, so results must match bit for bit.
void check_equal(const std::vector<std::vector<correlation::Match>>& a,
                 const std::vector<std::vector<correlation::Match>>& b) {
    check(a.size() == b.size(), "Query count differs");
    for (std::size_t q = 0; q < a.size(); ++q) {
        check(a[q].size() == b[q].size(), "Match count differs");
        for (std::size_t i = 0; i < a[q].size(); ++i) {
            check(a[q][i].event_id == b[q][i].event_id, "Event ID/order differs");
            check(a[q][i].timestamp == b[q][i].timestamp, "Timestamp differs");
            check(same_bits(a[q][i].score, b[q][i].score), "Score differs");
        }
    }
}
void matches_reference() {
    for (std::size_t features : {0, 3, 8}) {
        correlation::GeneratorConfig g;
        g.history_count = 600; g.query_count = 37; g.feature_count = features; g.seed = 1337;
        const auto data = correlation::generate_dataset(g);
        for (std::uint64_t window : {0, 30, 100000}) {
            for (auto mode : {correlation::SearchMode::TopK, correlation::SearchMode::Threshold}) {
                for (std::size_t k : {0, 1, 10, 5000}) {
                    correlation::SearchConfig c;
                    c.window_seconds = window; c.mode = mode; c.k = k; c.threshold = 0.8;
                    const auto expected = correlation::search_batch_sequential(data.history, data.queries, c);
                    for (int threads : {0, 1, 2, 4, 8, 64})
                        check_equal(correlation::search_batch_openmp(data.history, data.queries, c, threads), expected);
                }
            }
        }
    }
}
void edge_cases() {
    correlation::GeneratorConfig g;
    g.history_count = 50; g.query_count = 3;
    const auto data = correlation::generate_dataset(g);
    const correlation::SearchConfig c;
    check(correlation::search_batch_openmp(data.history, {}, c, 4).empty(), "Empty query set failed");
    const auto no_history = correlation::search_batch_openmp({}, data.queries, c, 4);
    check(no_history.size() == 3 && no_history[0].empty(), "Empty history failed");
    // Fewer queries than threads, and a single query.
    check_equal(correlation::search_batch_openmp(data.history, {data.queries[0]}, c, 16),
                correlation::search_batch_sequential(data.history, {data.queries[0]}, c));
    check(correlation::openmp_thread_count(3) == 3 && correlation::openmp_thread_count(0) >= 1,
          "Thread count resolution failed");
    rejects([] { correlation::openmp_thread_count(-1); });
    // Invalid input is rejected before the parallel region, never from a worker thread.
    auto bad = data.queries;
    bad[1].features.push_back(0.5);
    rejects([&] { correlation::search_batch_openmp(data.history, bad, c, 4); });
    correlation::SearchConfig invalid;
    invalid.threshold = 2.0;
    rejects([&] { correlation::search_batch_openmp(data.history, data.queries, invalid, 4); });
}
}
int main() {
    try {
        matches_reference(); edge_cases();
        std::cout << "Passed " << checks << " OpenMP checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
