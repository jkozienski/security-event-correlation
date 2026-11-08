#include "correlation/openmp.hpp"
#include "correlation/sequential.hpp"
#include <omp.h>
#include <cstddef>
#include <exception>
#include <stdexcept>

namespace correlation {
int openmp_thread_count(int threads) {
    if (threads < 0) throw std::invalid_argument("Thread count must not be negative");
    return threads > 0 ? threads : omp_get_max_threads();
}

std::vector<std::vector<Match>> search_batch_openmp(const std::vector<Event>& history,
                                                     const std::vector<Event>& queries,
                                                     const SearchConfig& config, int threads) {
    const int thread_count = openmp_thread_count(threads);
    validate_batch(history, queries, config);
    std::vector<std::vector<Match>> results(queries.size());
    std::exception_ptr failure; // an exception must never leave the parallel region
    const auto count = static_cast<std::ptrdiff_t>(queries.size());
    // Sharing: history, queries and config are read only; every iteration writes only
    // results[i]; `failure` is written only inside the named critical section.
    // Signed loop variable keeps the loop valid for OpenMP 2.0 compilers (MSVC).
    // Query cost can differ (threshold mode), hence dynamic scheduling.
#pragma omp parallel for schedule(dynamic) num_threads(thread_count)
    for (std::ptrdiff_t i = 0; i < count; ++i) {
        try {
            const auto index = static_cast<std::size_t>(i);
            results[index] = search_unchecked(history, queries[index], config);
        } catch (...) {
#pragma omp critical(correlation_openmp_failure)
            if (!failure) failure = std::current_exception();
        }
    }
    if (failure) std::rethrow_exception(failure);
    return results;
}
} // namespace correlation
