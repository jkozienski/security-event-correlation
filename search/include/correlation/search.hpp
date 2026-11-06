#pragma once
#include "correlation/common.hpp"
#include <string_view>

namespace correlation {
enum class Backend { Sequential, OpenMP, MPI, OpenCL };

using SearchFunction = std::vector<Match> (*)(const std::vector<Event>& history,
                                             const Event& query,
                                             const SearchConfig& config);

Backend parse_backend(std::string_view name);
// Resolve once before reading input or writing results. Unavailable backends fail
// explicitly; they never fall back to a different implementation.
SearchFunction resolve_backend(Backend backend);
} // namespace correlation
