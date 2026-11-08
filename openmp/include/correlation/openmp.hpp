#pragma once
#include "correlation/common.hpp"

namespace correlation {
// OpenMP batch: queries are distributed among threads, the history is shared read-only and
// every query writes only its own result slot, so no locks are needed. Results are in
// input query order and identical to search_batch_sequential.
// threads == 0 uses the OpenMP default (OMP_NUM_THREADS / number of cores).
// Validates once, before the parallel region.
std::vector<std::vector<Match>> search_batch_openmp(const std::vector<Event>& history,
                                                     const std::vector<Event>& queries,
                                                     const SearchConfig& config,
                                                     int threads = 0);
// Number of threads the next call with `threads` would use (for logging and reports).
int openmp_thread_count(int threads);
} // namespace correlation
