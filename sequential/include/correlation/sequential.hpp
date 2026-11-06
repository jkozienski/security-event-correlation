#pragma once
#include "correlation/common.hpp"

namespace correlation {
// Exhaustive reference: no approximate candidate preselection; arbitrary input order.
std::vector<Match> search_sequential(const std::vector<Event>& history,
                                     const Event& query, const SearchConfig& config = {});
}
