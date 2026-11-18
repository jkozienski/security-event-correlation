#pragma once
#include "correlation/common.hpp"
#include <mpi.h>

namespace correlation {
// Owns the MPI_COMM_WORLD session for a process started by the CLI.
class MpiSession {
public:
    MpiSession(int& argc, char**& argv);
    ~MpiSession();
    MpiSession(const MpiSession&) = delete;
    MpiSession& operator=(const MpiSession&) = delete;

    int rank() const { return rank_; }
    int size() const { return size_; }

    // All ranks enter here. Only rank 0 runs the action.
    template<class Action>
    void on_root(Action action) const {
        run_on_root([](void* context) { (*static_cast<Action*>(context))(); }, &action);
    }

private:
    void run_on_root(void (*action)(void*), void* context) const;
    int rank_ = 0;
    int size_ = 1;
};

// Collective on an initialized intracommunicator. Only rank 0 supplies input
// and receives results. Every other rank returns an empty vector.
std::vector<std::vector<Match>> search_batch_mpi(
    const std::vector<Event>& history, const std::vector<Event>& queries,
    const SearchConfig& config, MPI_Comm comm = MPI_COMM_WORLD);
}
