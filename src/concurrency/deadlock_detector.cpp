
#include "minidb/concurrency/deadlock_detector.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/concurrency/wait_for_graph.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace minidb {


// --------------------------------------------------
// Constructor
// --------------------------------------------------

DeadlockDetector::DeadlockDetector(
    const WaitForGraph& graph
)
    : graph_(graph) {
}


// --------------------------------------------------
// DetectDeadlock
// --------------------------------------------------

std::optional<DeadlockInfo>
DeadlockDetector::DetectDeadlock() const {

    // STEP 1:
    // Ask WaitForGraph to find a cycle.
    std::vector<txn_id_t> cycle =
        graph_.FindCycle();


    // No cycle means no deadlock.
    if (cycle.empty()) {
        return std::nullopt;
    }


    // STEP 2:
    // Validate the cycle format.
    //
    // Example: {1, 2, 3, 1}
    //
    // The first and last transaction must be identical.

    if (cycle.size() < 2 ||
        cycle.front() != cycle.back()) {

        throw TransactionException(
            "WaitForGraph returned an invalid cycle"
        );
    }


    // STEP 3:
    // Find the youngest transaction.
    //
    // The last element is a repeated starting vertex,
    // so do not include it in victim selection.

    const auto cycleEnd =
        std::prev(cycle.end());

    const auto victimIterator =
        std::max_element(
            cycle.begin(),
            cycleEnd
        );


    if (victimIterator == cycleEnd ||
        *victimIterator == 0) {

        throw TransactionException(
            "Unable to select a valid deadlock victim"
        );
    }


    // STEP 4:
    // Prepare the deadlock report.
    DeadlockInfo info;

    info.cycle = std::move(cycle);

    info.victim_txn_id = *victimIterator;

    return info;
}


// --------------------------------------------------
// HasDeadlock
// --------------------------------------------------

bool DeadlockDetector::HasDeadlock() const {

    return graph_.HasCycle();
}


} // namespace minidb
