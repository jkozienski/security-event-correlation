#pragma once
#include "correlation/common.hpp"
#include <string_view>

namespace correlation {
enum class Backend { Sequential, OpenMP, MPI, OpenCL };

struct RunOptions {
    int threads = 0; // OpenMP: 0 = runtime default; ignored by sequential
};

// One call answers a whole query set: validation happens once, results come back in
// input query order, and the caller (not the backend) writes the output.
using BatchSearchFunction = std::vector<std::vector<Match>> (*)(
    const std::vector<Event>& history, const std::vector<Event>& queries,
    const SearchConfig& config, const RunOptions& options);

Backend parse_backend(std::string_view name);
// Resolve once before reading input or writing results. Unavailable backends fail
// explicitly; they never fall back to a different implementation.
BatchSearchFunction resolve_backend(Backend backend);
} // namespace correlation
