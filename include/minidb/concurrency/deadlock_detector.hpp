
#pragma once

#include "minidb/common/types.hpp"

#include <optional>
#include <vector>

namespace minidb {

class WaitForGraph;

// Information about a detected deadlock.
struct DeadlockInfo {

    // Cycle includes the starting transaction again
    // at the end. Example: {1, 2, 3, 1}.
    std::vector<txn_id_t> cycle;

    // Transaction selected for aborting.
    txn_id_t victim_txn_id{0};
};


class DeadlockDetector {
public:

    // The WaitForGraph must outlive this detector.
    explicit DeadlockDetector(const WaitForGraph& graph);

    // Detects one cycle and selects a victim.
    // Returns std::nullopt if no deadlock exists.
    std::optional<DeadlockInfo> DetectDeadlock() const;

    // Checks whether a deadlock exists.
    bool HasDeadlock() const;

private:

    const WaitForGraph& graph_;
};

} // namespace minidb
