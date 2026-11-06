#include "correlation/generator.hpp"
#include <limits>
#include <random>
#include <stdexcept>

namespace correlation {
Dataset generate_dataset(const GeneratorConfig& c) {
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    if (c.history_count > limit - c.query_count)
        throw std::invalid_argument("Too many events");
    const std::uint64_t total = c.history_count + c.query_count;
    if (total && c.step_seconds && (total - 1) > (limit - c.start_timestamp) / c.step_seconds)
        throw std::invalid_argument("Generated timestamps overflow");
    std::mt19937 random(c.seed);
    Dataset data;
    data.history.reserve(c.history_count);
    data.queries.reserve(c.query_count);
    for (std::uint64_t i = 0; i < total; ++i) {
        // Eight recurring incident profiles, with bounded numeric noise.
        // Raw MT output and explicit conversion avoid distribution-specific behavior.
        const auto cluster = static_cast<std::uint32_t>(random() % 8);
        Event e;
        e.id = i + 1;
        e.timestamp = c.start_timestamp + i * c.step_seconds;
        e.operation_id = cluster % 4;
        e.device_id = cluster % 3;
        e.account_id = cluster % 5;
        e.source_id = cluster;
        e.destination_id = (cluster * 3) % 8;
        e.features.reserve(c.feature_count);
        for (std::size_t j = 0; j < c.feature_count; ++j) {
            const double center = static_cast<double>((cluster + j % 8) % 8) / 8.0;
            const double noise = static_cast<double>(random()) / 4294967296.0 / 8.0;
            e.features.push_back(center + noise);
        }
        if (i < c.history_count) data.history.push_back(std::move(e));
        else data.queries.push_back(std::move(e));
    }
    return data;
}
}
