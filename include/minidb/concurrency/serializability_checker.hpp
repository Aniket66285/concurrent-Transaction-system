
#pragma once

#include "minidb/concurrency/history_recorder.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace minidb {

// Result produced by conflict serializability analysis.
struct SerializabilityResult {

    // True if the precedence graph contains no cycle.
    bool conflict_serializable{true};

    // Number of unique transactions in the history.
    std::size_t transactions_checked{0};

    // Directed edges: T1 -> T2.
    std::vector<std::pair<txn_id_t, txn_id_t>>
        precedence_edges;

    // A valid serial order if the graph is acyclic.
    // Empty if the graph contains a cycle.
    std::vector<txn_id_t> serial_order;

    // One cycle if the graph is cyclic.
    // Empty if the graph is acyclic.
    std::vector<txn_id_t> cycle;
};


class SerializabilityChecker {
public:

    SerializabilityChecker() = default;

    // Checks whether the supplied history is conflict-serializable.
    SerializabilityResult Check(
        const std::vector<HistoryEvent>& history
    ) const;

    // Convenience function returning only the boolean result.
    bool IsConflictSerializable(
        const std::vector<HistoryEvent>& history
    ) const;
};

} // namespace minidb
