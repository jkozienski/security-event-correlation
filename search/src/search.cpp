#include "correlation/search.hpp"
#include "correlation/sequential.hpp"
#ifdef CORRELATION_HAS_OPENMP
#include "correlation/openmp.hpp"
#endif
#include <stdexcept>
#include <string>

namespace correlation {
namespace {
std::vector<std::vector<Match>> run_sequential(const std::vector<Event>& history,
                                               const std::vector<Event>& queries,
                                               const SearchConfig& config, const RunOptions&) {
    return search_batch_sequential(history, queries, config);
}
#ifdef CORRELATION_HAS_OPENMP
std::vector<std::vector<Match>> run_openmp(const std::vector<Event>& history,
                                           const std::vector<Event>& queries,
                                           const SearchConfig& config, const RunOptions& options) {
    return search_batch_openmp(history, queries, config, options.threads);
}
#endif
}

Backend parse_backend(std::string_view name) {
    if (name == "sequential") return Backend::Sequential;
    if (name == "openmp") return Backend::OpenMP;
    if (name == "mpi") return Backend::MPI;
    if (name == "opencl") return Backend::OpenCL;
    throw std::invalid_argument("Unknown backend: " + std::string(name) +
                                ". Choose sequential, openmp, mpi or opencl.");
}

BatchSearchFunction resolve_backend(Backend backend) {
    switch (backend) {
    case Backend::Sequential:
        return &run_sequential;
    case Backend::OpenMP:
#ifdef CORRELATION_HAS_OPENMP
        return &run_openmp;
#else
        throw std::runtime_error("Backend openmp is not available: this build has no OpenMP support. Available backend: sequential.");
#endif
    case Backend::MPI:
        throw std::runtime_error("Backend mpi is not available: implementation not yet added. Available backend: sequential"
#ifdef CORRELATION_HAS_OPENMP
                                 ", openmp"
#endif
                                 ".");
    case Backend::OpenCL:
        throw std::runtime_error("Backend opencl is not available: implementation not yet added. Available backend: sequential"
#ifdef CORRELATION_HAS_OPENMP
                                 ", openmp"
#endif
                                 ".");
    }
    throw std::invalid_argument("Invalid backend value");
}
} // namespace correlation
