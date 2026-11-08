#pragma once
#include "correlation/common.hpp"

namespace correlation {
// Exhaustive reference: no approximate candidate preselection; arbitrary input order.
// Validates history, query and config on every call (use the batch API to validate once).
std::vector<Match> search_sequential(const std::vector<Event>& history,
                                     const Event& query, const SearchConfig& config = {});

// Validation shared by every batch implementation. Call it once, outside the timed core.
// Checks the history (feature count, ranges, unique IDs), every query against the common
// feature dimension and the configuration.
void validate_batch(const std::vector<Event>& history, const std::vector<Event>& queries,
                    const SearchConfig& config);

// Core of one query. Precondition: inputs already passed validate_batch. Read-only on
// history, no shared state, so it is safe to call concurrently. Top-k keeps at most k
// candidates (bounded heap); the result order equals select_matches().
std::vector<Match> search_unchecked(const std::vector<Event>& history,
                                    const Event& query, const SearchConfig& config);

// Reference batch: validates once, then answers queries in input order.
std::vector<std::vector<Match>> search_batch_sequential(const std::vector<Event>& history,
                                                         const std::vector<Event>& queries,
                                                         const SearchConfig& config);
}
