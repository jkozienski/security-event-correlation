#include "correlation/search.hpp"
#include "correlation/sequential.hpp"
#include <stdexcept>
#include <string>

namespace correlation {
Backend parse_backend(std::string_view name) {
    if (name == "sequential") return Backend::Sequential;
    if (name == "openmp") return Backend::OpenMP;
    if (name == "mpi") return Backend::MPI;
    if (name == "opencl") return Backend::OpenCL;
    throw std::invalid_argument("Unknown backend: " + std::string(name) +
                                ". Choose sequential, openmp, mpi or opencl.");
}

SearchFunction resolve_backend(Backend backend) {
    switch (backend) {
    case Backend::Sequential:
        return &search_sequential;
    case Backend::OpenMP:
        throw std::runtime_error("Backend openmp is not available: implementation not yet added. Available backend: sequential.");
    case Backend::MPI:
        throw std::runtime_error("Backend mpi is not available: implementation not yet added. Available backend: sequential.");
    case Backend::OpenCL:
        throw std::runtime_error("Backend opencl is not available: implementation not yet added. Available backend: sequential.");
    }
    throw std::invalid_argument("Invalid backend value");
}
} // namespace correlation
