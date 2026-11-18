#include "correlation/search.hpp"
#include "correlation/sequential.hpp"
#ifdef CORRELATION_HAS_OPENMP
#include "correlation/openmp.hpp"
#endif
#ifdef CORRELATION_HAS_MPI
#include "correlation/mpi.hpp"
#endif
#include <stdexcept>
#include <string>

namespace correlation {
namespace {
std::runtime_error unavailable(const char* backend, const char* reason) {
    std::string available = "sequential";
#ifdef CORRELATION_HAS_OPENMP
    available += ", openmp";
#endif
#ifdef CORRELATION_HAS_MPI
    available += ", mpi";
#endif
    return std::runtime_error(std::string("Backend ") + backend + " is not available: " + reason
                              + ". Available backend: " + available + ".");
}

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
#ifdef CORRELATION_HAS_MPI
std::vector<std::vector<Match>> run_mpi(const std::vector<Event>& history,
                                       const std::vector<Event>& queries,
                                       const SearchConfig& config, const RunOptions&) {
    return search_batch_mpi(history, queries, config);
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
        throw unavailable("openmp", "this build has no OpenMP support");
#endif
    case Backend::MPI:
#ifdef CORRELATION_HAS_MPI
        return &run_mpi;
#else
        throw unavailable("mpi", "this build has no MPI support");
#endif
    case Backend::OpenCL:
        throw unavailable("opencl", "implementation not yet added");
    }
    throw std::invalid_argument("Invalid backend value");
}
} // namespace correlation
