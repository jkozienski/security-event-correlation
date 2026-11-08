#include "correlation/generator.hpp"
#include "correlation/sequential.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected) {
    check(std::abs(actual - expected) <= 1e-12, "Unexpected numeric score");
}
void rejects(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid input was accepted");
}
correlation::Event event(std::uint64_t id, std::uint64_t time, std::vector<double> features = {0.0, 1.0}) {
    return {id, time, 1, 4, 5, 2, 3, std::move(features)};
}
std::string csv(const std::vector<correlation::Event>& events) {
    std::ostringstream stream;
    correlation::write_events_csv(stream, events);
    return stream.str();
}
void math_tests() {
    auto q = event(100, 4000);
    auto e = event(1, 4000);
    near(correlation::similarity(q, e, {}), 1.0);
    // Each categorical field contributes independently to the score.
    for (auto field : {&correlation::Event::operation_id, &correlation::Event::device_id,
                       &correlation::Event::account_id, &correlation::Event::source_id,
                       &correlation::Event::destination_id}) {
        e = event(1, 4000);
        e.*field = 99;
        near(correlation::similarity(q, e, {}), 5.0 / 6.0);
    }
    e = event(1, 4000);
    e.device_id = 99;
    near(correlation::similarity(q, e, {0, 2, 0, 0, 0, 0}), 0.0);
    near(correlation::similarity(q, e, {0, 0, 2, 0, 0, 0}), 1.0);
    e.operation_id = 9; e.device_id = 9; e.account_id = 9; e.source_id = 9; e.destination_id = 9; e.features = {1.0, 0.0};
    near(correlation::similarity(q, e, {}), 0.0);
    e = event(1, 4000, {0.5, 0.5}); e.destination_id = 9;
    // Four matching categories plus numeric similarity 0.5; six equally weighted blocks.
    near(correlation::similarity(q, e, {}), 0.75);
    near(correlation::similarity(q, e, {0, 0, 0, 0, 0, 2}), 0.5);
    q.features.clear(); e.features.clear();
    near(correlation::similarity(q, e, {}), 4.0 / 5.0);
    const auto normalized = correlation::normalize_features({15, 7}, {{10, 20}, {7, 7}});
    near(normalized[0], 0.5); near(normalized[1], 0.0);
    rejects([] { correlation::normalize_features({21}, {{10, 20}}); });
    rejects([] { correlation::normalize_features({1}, {}); });
    rejects([] { correlation::normalize_features({1}, {{2, 1}}); });
}
void search_tests() {
    const auto q = event(100, 4000);
    correlation::SearchConfig c;
    std::vector<correlation::Event> history{event(4, 400), event(3, 3999), event(2, 3999),
        event(1, 4000), event(5, 399), event(6, 4001), q};
    auto matches = correlation::search_sequential(history, q, c);
    check(matches.size() == 4, "Window/self filtering failed");
    check(matches[0].event_id == 1 && matches[1].event_id == 2 &&
          matches[2].event_id == 3 && matches[3].event_id == 4, "Tie ordering failed");
    c.k = 2;
    matches = correlation::search_sequential(history, q, c);
    check(matches.size() == 2 && matches[1].event_id == 2, "Top-k failed");
    std::reverse(history.begin(), history.end());
    check(correlation::search_sequential(history, q, c)[1].event_id == 2, "Input order affects ties");
    c.k = 0;
    check(correlation::search_sequential(history, q, c).empty(), "k=0 failed");
    c.k = 100; c.window_seconds = 0;
    check(correlation::search_sequential(history, q, c).size() == 1, "Zero window failed");
    c.window_seconds = 3600; c.mode = correlation::SearchMode::Threshold; c.threshold = 1.0;
    check(correlation::search_sequential(history, q, c).size() == 4, "Inclusive threshold failed");
    auto e = event(7, 4000, {0.5, 0.5}); e.destination_id = 99;
    c.threshold = 0.75;
    check(correlation::search_sequential({e}, q, c).size() == 1, "Exact numeric threshold failed");
    c.threshold = std::nextafter(0.75, 1.0);
    check(correlation::search_sequential({e}, q, c).empty(), "Threshold above score failed");
    c.threshold = 0.0;
    check(correlation::search_sequential({e}, q, c).size() == 1, "Zero threshold failed");
    check(correlation::search_sequential({}, q, c).empty(), "Empty history failed");
    check(correlation::search_sequential({event(1, 0)}, q, c).empty(), "No eligible events failed");
    const auto max = std::numeric_limits<std::uint64_t>::max();
    check(!correlation::in_history_window(event(1, max), event(2, 0), 3600), "Timestamp arithmetic overflow");
    check(!correlation::in_history_window(event(1, 0), event(2, max), max), "Future event accepted");
    check(correlation::search_sequential({event(1, 1, {})}, event(2, 1, {})).size() == 1, "Zero features failed");
}
void batch_tests() {
    correlation::GeneratorConfig g;
    g.history_count = 200; g.query_count = 9; g.feature_count = 3;
    const auto data = correlation::generate_dataset(g);
    for (auto mode : {correlation::SearchMode::TopK, correlation::SearchMode::Threshold}) {
        correlation::SearchConfig c;
        c.mode = mode; c.k = 7; c.window_seconds = 1000;
        const auto batch = correlation::search_batch_sequential(data.history, data.queries, c);
        check(batch.size() == data.queries.size(), "Batch size failed");
        for (std::size_t i = 0; i < data.queries.size(); ++i) {
            const auto single = correlation::search_sequential(data.history, data.queries[i], c);
            check(batch[i].size() == single.size(), "Batch result size differs from single query");
            for (std::size_t j = 0; j < single.size(); ++j)
                check(batch[i][j].event_id == single[j].event_id && batch[i][j].score == single[j].score,
                      "Batch result differs from single query");
        }
    }
    check(correlation::search_batch_sequential(data.history, {}, {}).empty(), "Empty batch failed");
    check(correlation::search_batch_sequential({}, data.queries, {}).size() == data.queries.size(), "Empty history batch failed");
    auto bad = data.queries;
    bad.back().features.pop_back();
    rejects([&] { correlation::search_batch_sequential(data.history, bad, {}); });
    rejects([&] { correlation::validate_batch(data.history, data.queries, [] { correlation::SearchConfig c; c.threshold = -0.1; return c; }()); });
}
void invalid_tests() {
    const auto q = event(100, 4000);
    rejects([&] { correlation::search_sequential({event(1, 1), event(1, 2)}, q); });
    rejects([&] { correlation::search_sequential({event(1, 1, {})}, q); });
    rejects([&] { correlation::search_sequential({event(1, 1, {2.0, 0.0})}, q); });
    rejects([&] { correlation::search_sequential({}, event(1, 1, {std::numeric_limits<double>::quiet_NaN()})); });
    correlation::SearchConfig c;
    c.threshold = 1.1;
    rejects([&] { correlation::search_sequential({}, q, c); });
    c = {}; c.weights = {0, 0, 0, 0, 0, 0};
    rejects([&] { correlation::search_sequential({}, q, c); });
    c.weights = {0, 0, 0, 0, 0, 1};
    rejects([&] { correlation::search_sequential({}, event(1, 1, {}), c); });
    c.weights = {-1, 1, 1, 1, 1, 1};
    rejects([&] { correlation::search_sequential({}, q, c); });
    c.weights = {std::numeric_limits<double>::infinity(), 1, 1, 1, 1, 1};
    rejects([&] { correlation::search_sequential({}, q, c); });
    c.weights = {1, -1, 1, 1, 1, 1};
    rejects([&] { correlation::search_sequential({}, q, c); });
    c.weights = {1, 1, std::numeric_limits<double>::quiet_NaN(), 1, 1, 1};
    rejects([&] { correlation::search_sequential({}, q, c); });
}
void csv_tests() {
    const std::vector<correlation::Event> input{event(1, 0, {0.12345678901234567, 1.0}),
        event(std::numeric_limits<std::uint64_t>::max(), 100, {0, 0.5})};
    std::istringstream stream(csv(input));
    const auto output = correlation::read_events_csv(stream);
    check(csv(output) == csv(input), "CSV roundtrip changed values");
    std::istringstream empty("id,timestamp,operation_id,device_id,account_id,source_id,destination_id\r\n");
    check(correlation::read_events_csv(empty).empty(), "Empty CSV failed");
    const std::string header = "id,timestamp,operation_id,device_id,account_id,source_id,destination_id,f0\n";
    for (const auto& row : {"1,2,3,6,7,4,5", "-1,2,3,6,7,4,5,0", "1,2,3,6,7,4,5,nan",
            "1,2,4294967296,6,7,4,5,0", "1,2,3,4294967296,7,4,5,0",
            "1,2,3,6,4294967296,4,5,0", "1,2,3,6,7,4,5,1.1", "1,2,3,6,7,4,5,0junk",
            "1,2,3,6,7,4,5,", "1,2,3,6,7,4,5,0,", "1,2,3,6,7,4,5, 0"}) {
        rejects([&] { std::istringstream bad(header + row + "\n"); correlation::read_events_csv(bad); });
    }
    rejects([] { std::istringstream bad("wrong\n"); correlation::read_events_csv(bad); });
    rejects([&] { std::istringstream bad(header + "1,2,3,6,7,4,5,0\n1,3,3,6,7,4,5,0\n"); correlation::read_events_csv(bad); });
}
void generated_tests() {
    correlation::GeneratorConfig g;
    g.history_count = 137; g.query_count = 8;
    for (std::size_t dimension : {0, 1, 7}) {
        g.feature_count = dimension;
        const auto a = correlation::generate_dataset(g);
        const auto b = correlation::generate_dataset(g);
        check(csv(a.history) == csv(b.history) && csv(a.queries) == csv(b.queries), "Generator is not repeatable");
        correlation::validate_events(a.history); correlation::validate_events(a.queries);
        for (const auto& query : a.queries) {
            correlation::SearchConfig c;
            c.window_seconds = 30;
            // Independent score expression, full sort and resize as oracle.
            std::vector<correlation::Match> expected;
            for (const auto& e : a.history) {
                if (e.timestamp > query.timestamp || query.timestamp - e.timestamp > 30) continue;
                double sum = static_cast<double>(e.operation_id == query.operation_id) + (e.device_id == query.device_id)
                    + (e.account_id == query.account_id) + (e.source_id == query.source_id) + (e.destination_id == query.destination_id);
                if (dimension) {
                    double numeric = 0;
                    for (std::size_t j = 0; j < dimension; ++j) numeric += 1.0 - std::abs(e.features[j] - query.features[j]);
                    sum += numeric / dimension;
                }
                expected.push_back({e.id, e.timestamp, sum / (dimension ? 6.0 : 5.0)});
            }
            std::sort(expected.begin(), expected.end(), [](const auto& x, const auto& y) {
                if (x.score != y.score) return x.score > y.score;
                if (x.timestamp != y.timestamp) return x.timestamp > y.timestamp;
                return x.event_id < y.event_id;
            });
            for (std::size_t k : {0, 1, 10, 200}) {
                c.k = k;
                const auto actual = correlation::search_sequential(a.history, query, c);
                check(actual.size() == std::min(k, expected.size()), "Generated top-k size failed");
                for (std::size_t j = 0; j < actual.size(); ++j) {
                    check(actual[j].event_id == expected[j].event_id, "Generated top-k IDs failed");
                    near(actual[j].score, expected[j].score);
                }
            }
            c.mode = correlation::SearchMode::Threshold; c.threshold = 0.8;
            const auto actual = correlation::search_sequential(a.history, query, c);
            expected.erase(std::remove_if(expected.begin(), expected.end(), [](const auto& m) { return m.score < 0.8; }), expected.end());
            check(actual.size() == expected.size(), "Generated threshold size failed");
            for (std::size_t j = 0; j < actual.size(); ++j) check(actual[j].event_id == expected[j].event_id, "Generated threshold IDs failed");
        }
    }
    g.history_count = 0; g.query_count = 0;
    check(correlation::generate_dataset(g).history.empty(), "Empty generator failed");
    g.history_count = 2; g.start_timestamp = std::numeric_limits<std::uint64_t>::max();
    rejects([&] { correlation::generate_dataset(g); });
}
}
int main() {
    try {
        math_tests(); search_tests(); batch_tests(); invalid_tests(); csv_tests(); generated_tests();
        std::cout << "Passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
