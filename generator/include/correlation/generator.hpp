#pragma once
#include "correlation/common.hpp"

namespace correlation {
struct GeneratorConfig {
    std::size_t history_count = 1000;
    std::size_t query_count = 10;
    std::size_t feature_count = 4;
    std::uint32_t seed = 1337;
    std::uint64_t start_timestamp = 1700000000;
    std::uint64_t step_seconds = 1;
};
struct Dataset { std::vector<Event> history; std::vector<Event> queries; };
Dataset generate_dataset(const GeneratorConfig& config = {});
}
