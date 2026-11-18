#include "correlation/mpi.hpp"
#include "correlation/sequential.hpp"
#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace correlation {
namespace {
using Results = std::vector<std::vector<Match>>;
constexpr int root = 0;
enum Tag { HistoryFields, HistoryFeatures, MatchCounts, Matches };

// Finish work that can throw on any rank before entering the next transfer.
template<class Work>
void collective_phase(MPI_Comm comm, int rank, Work work) {
    std::exception_ptr failure;
    try {
        work();
    } catch (...) {
        failure = std::current_exception();
    }
    constexpr int success = std::numeric_limits<int>::max();
    int local = failure ? rank : success;
    int first_failure = success;
    MPI_Allreduce(&local, &first_failure, 1, MPI_INT, MPI_MIN, comm);
    if (failure) std::rethrow_exception(failure);
    if (first_failure != success)
        throw std::runtime_error("MPI search failed on rank " + std::to_string(first_failure));
}

std::size_t checked_product(std::size_t count, std::size_t width) {
    if (width && count > std::numeric_limits<std::size_t>::max() / width)
        throw std::length_error("MPI buffer size overflow");
    return count * width;
}

// Keep MPI counts and message sizes well below their signed integer limits.
template<class T, class Transfer>
void transfer(T* data, std::size_t count, Transfer send_or_receive) {
    while (count != 0) {
        const auto chunk = std::min<std::size_t>(count, 1u << 20);
        send_or_receive(data, static_cast<int>(chunk));
        data += chunk;
        count -= chunk;
    }
}

template<class T>
void broadcast(T* data, std::size_t count, MPI_Datatype type, MPI_Comm comm) {
    transfer(data, count, [&](T* part, int size) { MPI_Bcast(part, size, type, root, comm); });
}

template<class T>
void send(const T* data, std::size_t count, MPI_Datatype type, int peer, Tag tag, MPI_Comm comm) {
    transfer(data, count, [&](const T* part, int size) { MPI_Send(part, size, type, peer, tag, comm); });
}

template<class T>
void receive(T* data, std::size_t count, MPI_Datatype type, int peer, Tag tag, MPI_Comm comm) {
    transfer(data, count, [&](T* part, int size) { MPI_Recv(part, size, type, peer, tag, comm, MPI_STATUS_IGNORE); });
}

struct Slice {
    std::size_t first;
    std::size_t count;
};

Slice history_slice(std::size_t count, int size, int rank) {
    const auto shard_size = count / size;
    const auto extra = count % size;
    const auto index = static_cast<std::size_t>(rank);
    return {index * shard_size + std::min(index, extra), shard_size + (index < extra ? 1 : 0)};
}

// Event contains a vector, so transfer its scalar fields and feature values
// separately, using numeric MPI types for both.
struct PackedEvents {
    static constexpr std::size_t field_count = 7;
    std::vector<std::uint64_t> fields;
    std::vector<double> features;

    void resize(std::size_t count, std::size_t dimension) {
        fields.resize(checked_product(count, field_count));
        features.resize(checked_product(count, dimension));
    }

    void pack(const std::vector<Event>& events, Slice slice, std::size_t dimension) {
        resize(slice.count, dimension);
        for (std::size_t i = 0; i < slice.count; ++i) {
            const auto& event = events[slice.first + i];
            const std::uint64_t header[] = {event.id, event.timestamp, event.operation_id,
                event.device_id, event.account_id, event.source_id, event.destination_id};
            std::copy_n(header, field_count, fields.begin() + i * field_count);
            if (dimension != 0)
                std::copy_n(event.features.begin(), dimension, features.begin() + i * dimension);
        }
    }

    std::vector<Event> unpack(std::size_t dimension) const {
        std::vector<Event> events;
        events.reserve(fields.size() / field_count);
        for (std::size_t i = 0; i < fields.size() / field_count; ++i) {
            const auto* header = fields.data() + i * field_count;
            Event event{header[0], header[1], static_cast<std::uint32_t>(header[2]),
                static_cast<std::uint32_t>(header[3]), static_cast<std::uint32_t>(header[4]),
                static_cast<std::uint32_t>(header[5]), static_cast<std::uint32_t>(header[6]), {}};
            if (dimension != 0) {
                const auto first = features.begin() + i * dimension;
                event.features.assign(first, first + dimension);
            }
            events.push_back(std::move(event));
        }
        return events;
    }
};

struct LocalData {
    std::vector<Event> history;
    std::vector<Event> queries;
};

LocalData distribute(const std::vector<Event>& history, const std::vector<Event>& queries,
                     std::size_t history_count, std::size_t query_count, std::size_t dimension,
                     int rank, int size, MPI_Comm comm) {
    const auto local_slice = history_slice(history_count, size, rank);
    PackedEvents packed_history, packed_queries;
    LocalData local;
    collective_phase(comm, rank, [&] {
        if (rank == root) {
            local.history.assign(history.begin(), history.begin() + local_slice.count);
            packed_queries.pack(queries, {0, query_count}, dimension);
        } else {
            packed_queries.resize(query_count, dimension);
        }
        // The first slice is the largest. The root can reuse this allocation
        // to pack each peer's slice without allocating during sends.
        packed_history.resize(local_slice.count, dimension);
    });
    broadcast(packed_queries.fields.data(), packed_queries.fields.size(), MPI_UINT64_T, comm);
    broadcast(packed_queries.features.data(), packed_queries.features.size(), MPI_DOUBLE, comm);
    if (rank == root) {
        for (int peer = 1; peer < size; ++peer) {
            packed_history.pack(history, history_slice(history_count, size, peer), dimension);
            send(packed_history.fields.data(), packed_history.fields.size(), MPI_UINT64_T, peer, HistoryFields, comm);
            send(packed_history.features.data(), packed_history.features.size(), MPI_DOUBLE, peer, HistoryFeatures, comm);
        }
    } else {
        receive(packed_history.fields.data(), packed_history.fields.size(), MPI_UINT64_T, root, HistoryFields, comm);
        receive(packed_history.features.data(), packed_history.features.size(), MPI_DOUBLE, root, HistoryFeatures, comm);
    }
    collective_phase(comm, rank, [&] {
        if (rank != root) {
            local.history = packed_history.unpack(dimension);
            local.queries = packed_queries.unpack(dimension);
        }
    });
    return local;
}

class MatchDatatype {
public:
    MatchDatatype() {
        int blocks[] = {1, 1, 1};
        MPI_Aint offsets[] = {offsetof(Match, event_id), offsetof(Match, timestamp), offsetof(Match, score)};
        MPI_Datatype types[] = {MPI_UINT64_T, MPI_UINT64_T, MPI_DOUBLE};
        MPI_Datatype fields;
        MPI_Type_create_struct(3, blocks, offsets, types, &fields);
        MPI_Type_create_resized(fields, 0, sizeof(Match), &type);
        MPI_Type_free(&fields);
        MPI_Type_commit(&type);
    }
    ~MatchDatatype() { MPI_Type_free(&type); }
    MatchDatatype(const MatchDatatype&) = delete;
    MatchDatatype& operator=(const MatchDatatype&) = delete;
    MPI_Datatype type;
};

Results gather(Results results, const SearchConfig& config, int rank, int size, MPI_Comm comm) {
    const auto queries = results.size();
    std::vector<std::uint64_t> counts;
    collective_phase(comm, rank, [&] {
        counts.resize(rank == root ? checked_product(queries, size) : queries);
        for (std::size_t q = 0; q < queries; ++q) counts[q] = results[q].size();
    });
    if (rank == root) {
        for (int peer = 1; peer < size; ++peer)
            receive(counts.data() + peer * queries, queries, MPI_UINT64_T, peer, MatchCounts, comm);
    } else {
        send(counts.data(), queries, MPI_UINT64_T, root, MatchCounts, comm);
    }

    const bool top_k = config.mode == SearchMode::TopK;
    std::vector<Match> candidates;
    collective_phase(comm, rank, [&] {
        if (rank != root) return;
        std::size_t max_candidates = 0;
        for (std::size_t q = 0; q < queries; ++q) {
            std::size_t total = 0;
            // Disjoint history slices bound the sum by the history size.
            for (int peer = 0; peer < size; ++peer) total += counts[peer * queries + q];
            results[q].reserve(top_k ? std::min(total, config.k) : total);
            max_candidates = std::max(max_candidates, total);
        }
        if (top_k) candidates.reserve(max_candidates);
    });

    const MatchDatatype match_type;
    if (rank != root) {
        for (const auto& matches : results)
            send(matches.data(), matches.size(), match_type.type, root, Matches, comm);
        return {};
    }

    for (std::size_t q = 0; q < queries; ++q) {
        if (top_k) candidates.assign(results[q].begin(), results[q].end());
        auto& matches = top_k ? candidates : results[q];
        for (int peer = 1; peer < size; ++peer) {
            const auto count = counts[peer * queries + q];
            if (count == 0) continue;
            const auto offset = matches.size();
            matches.resize(offset + count);
            receive(matches.data() + offset, count, match_type.type, peer, Matches, comm);
        }
        if (top_k) {
            // A candidate outside its local top-k already has k better
            // candidates globally, so only local top-k lists are needed.
            select_matches(matches, config);
            results[q].assign(matches.begin(), matches.end());
        } else {
            std::sort(matches.begin(), matches.end(), better_match);
        }
    }
    return results;
}
}

MpiSession::MpiSession(int& argc, char**& argv) {
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) throw std::runtime_error("MPI initialization failed");
    MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
    MPI_Comm_size(MPI_COMM_WORLD, &size_);
}

MpiSession::~MpiSession() { MPI_Finalize(); }

void MpiSession::run_on_root(void (*action)(void*), void* context) const {
    if (size_ == 1) {
        action(context);
        return;
    }
    collective_phase(MPI_COMM_WORLD, rank_, [&] {
        if (rank_ == root) action(context);
    });
}

std::vector<std::vector<Match>> search_batch_mpi(
    const std::vector<Event>& history, const std::vector<Event>& queries,
    const SearchConfig& config, MPI_Comm comm) {
    int initialized = 0, finalized = 0;
    MPI_Initialized(&initialized);
    MPI_Finalized(&finalized);
    if (!initialized || finalized) throw std::logic_error("MPI search requires an active MPI session");
    int rank = 0, size = 0;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    const auto dimension = !history.empty() ? history.front().features.size()
                         : (!queries.empty() ? queries.front().features.size() : 0);
    std::uint64_t shape[] = {history.size(), queries.size(), dimension, config.window_seconds,
                            config.k, config.mode == SearchMode::Threshold ? 1u : 0u};
    double scoring[] = {config.threshold, config.weights.operation, config.weights.device,
                        config.weights.account, config.weights.source, config.weights.destination,
                        config.weights.numeric};
    collective_phase(comm, rank, [&] {
        if (rank == root) validate_batch(history, queries, config);
    });
    broadcast(shape, 6, MPI_UINT64_T, comm);
    const auto [history_count, query_count, feature_count, window, k, threshold_mode] = shape;
    const auto mode = threshold_mode ? SearchMode::Threshold : SearchMode::TopK;
    Results results;
    if (history_count == 0 || query_count == 0 || (mode == SearchMode::TopK && k == 0)) {
        collective_phase(comm, rank, [&] {
            if (rank == root) results.resize(query_count);
        });
        return results;
    }
    broadcast(scoring, 7, MPI_DOUBLE, comm);
    const auto [threshold, operation, device, account, source, destination, numeric] = scoring;
    const SearchConfig shared{window, mode, static_cast<std::size_t>(k), threshold,
                              {operation, device, account, source, destination, numeric}};
    LocalData local;
    if (size > 1)
        local = distribute(history, queries, history_count, query_count, feature_count, rank, size, comm);
    const auto& local_history = size == 1 ? history : local.history;
    const auto& local_queries = rank == root ? queries : local.queries;
    collective_phase(comm, rank, [&] {
        results.reserve(local_queries.size());
        for (const auto& query : local_queries) results.push_back(search_unchecked(local_history, query, shared));
    });
    if (size == 1) return results;
    return gather(std::move(results), shared, rank, size, comm);
}
}
