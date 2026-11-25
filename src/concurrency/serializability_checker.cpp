
#include "minidb/concurrency/serializability_checker.hpp"

#include "minidb/common/exception.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace minidb {

namespace {

using ResourceKey =
    std::pair<page_id_t, slot_id_t>;

using AdjacencyList =
    std::map<txn_id_t, std::set<txn_id_t>>;


enum class TxnStatus {
    ACTIVE,
    COMMITTED,
    ABORTED
};


struct PreviousAccess {

    txn_id_t txn_id;

    bool is_write;
};


// --------------------------------------------------
// ValidateHistory
// --------------------------------------------------

void ValidateHistory(
    const std::vector<HistoryEvent>& history
) {

    std::map<txn_id_t, TxnStatus> transactionStatus;

    std::uint64_t previousSequence = 0;

    for (const HistoryEvent& event : history) {

        // Sequence numbers must be nonzero and increasing.
        if (event.sequence_number == 0 ||
            event.sequence_number <= previousSequence) {

            throw TransactionException(
                "History contains invalid or unordered sequence numbers"
            );
        }

        previousSequence = event.sequence_number;


        // Transaction ID zero is reserved.
        if (event.txn_id == 0) {
            throw TransactionException(
                "History contains reserved transaction ID zero"
            );
        }


        switch (event.operation) {

            case HistoryOperation::BEGIN: {

                if (transactionStatus.find(event.txn_id) !=
                    transactionStatus.end()) {

                    throw TransactionException(
                        "Duplicate BEGIN in execution history"
                    );
                }

                transactionStatus.emplace(
                    event.txn_id,
                    TxnStatus::ACTIVE
                );

                break;
            }


            case HistoryOperation::READ:
            case HistoryOperation::WRITE: {

                const auto iterator =
                    transactionStatus.find(event.txn_id);

                if (iterator == transactionStatus.end()) {
                    throw TransactionException(
                        "READ/WRITE found without transaction BEGIN"
                    );
                }

                if (iterator->second != TxnStatus::ACTIVE) {
                    throw TransactionException(
                        "READ/WRITE found after transaction completion"
                    );
                }

                break;
            }


            case HistoryOperation::COMMIT: {

                const auto iterator =
                    transactionStatus.find(event.txn_id);

                if (iterator == transactionStatus.end() ||
                    iterator->second != TxnStatus::ACTIVE) {

                    throw TransactionException(
                        "Invalid COMMIT in execution history"
                    );
                }

                iterator->second = TxnStatus::COMMITTED;

                break;
            }


            case HistoryOperation::ABORT: {

                const auto iterator =
                    transactionStatus.find(event.txn_id);

                if (iterator == transactionStatus.end() ||
                    iterator->second != TxnStatus::ACTIVE) {

                    throw TransactionException(
                        "Invalid ABORT in execution history"
                    );
                }

                iterator->second = TxnStatus::ABORTED;

                break;
            }


            default:

                throw TransactionException(
                    "Unknown operation in execution history"
                );
        }
    }
}


// --------------------------------------------------
// FindCycle
// --------------------------------------------------

std::vector<txn_id_t> FindCycle(
    const AdjacencyList& graph
) {

    // DFS colors:
    // 0 = not visited
    // 1 = currently exploring
    // 2 = completely explored.

    std::map<txn_id_t, int> color;

    std::vector<txn_id_t> path;

    std::vector<txn_id_t> cycle;

    std::function<bool(txn_id_t)> dfs;


    dfs = [&](txn_id_t current) -> bool {

        color[current] = 1;

        path.push_back(current);

        const auto iterator = graph.find(current);

        if (iterator != graph.end()) {

            for (txn_id_t neighbor : iterator->second) {

                const int neighborColor = color[neighbor];


                if (neighborColor == 0) {

                    if (dfs(neighbor)) {
                        return true;
                    }
                }
                else if (neighborColor == 1) {

                    // A back edge means a cycle exists.
                    const auto cycleStart =
                        std::find(
                            path.begin(),
                            path.end(),
                            neighbor
                        );

                    cycle.assign(
                        cycleStart,
                        path.end()
                    );

                    cycle.push_back(neighbor);

                    return true;
                }
            }
        }

        path.pop_back();

        color[current] = 2;

        return false;
    };


    for (const auto& entry : graph) {

        const txn_id_t transactionId = entry.first;

        if (color[transactionId] == 0) {

            if (dfs(transactionId)) {
                return cycle;
            }
        }
    }

    return {};
}


// --------------------------------------------------
// BuildPrecedenceGraph
// --------------------------------------------------

AdjacencyList BuildPrecedenceGraph(
    const std::vector<HistoryEvent>& history
) {

    AdjacencyList graph;

    // Include every transaction, even if it has no
    // READ/WRITE conflicts with another transaction.
    for (const HistoryEvent& event : history) {

        graph.try_emplace(event.txn_id);
    }


    // Previous accesses are maintained separately
    // for each database record.
    std::map<
        ResourceKey,
        std::vector<PreviousAccess>
    > previousAccesses;


    for (const HistoryEvent& event : history) {

        const bool isRead =
            event.operation == HistoryOperation::READ;

        const bool isWrite =
            event.operation == HistoryOperation::WRITE;


        // BEGIN, COMMIT and ABORT do not create
        // precedence edges.
        if (!isRead && !isWrite) {
            continue;
        }


        const ResourceKey resource{
            event.rid.page_id,
            event.rid.slot_id
        };

        auto& previous =
            previousAccesses[resource];


        // Compare with all earlier accesses to the
        // same record.
        for (const PreviousAccess& access : previous) {

            // Operations of the same transaction
            // do not create an inter-transaction edge.
            if (access.txn_id == event.txn_id) {
                continue;
            }


            // READ/READ is not a conflict.
            // At least one operation must be WRITE.
            if (access.is_write || isWrite) {

                // Earlier transaction -> later transaction.
                graph[access.txn_id].insert(
                    event.txn_id
                );
            }
        }


        previous.push_back({
            event.txn_id,
            isWrite
        });
    }

    return graph;
}


// --------------------------------------------------
// TopologicalSort
// --------------------------------------------------

std::vector<txn_id_t> TopologicalSort(
    const AdjacencyList& graph
) {

    std::map<txn_id_t, std::size_t> indegree;

    for (const auto& entry : graph) {

        indegree[entry.first] = 0;
    }


    // Calculate indegree of every transaction.
    for (const auto& entry : graph) {

        for (txn_id_t neighbor : entry.second) {

            ++indegree[neighbor];
        }
    }


    // A min-priority queue makes the result deterministic.
    std::priority_queue<
        txn_id_t,
        std::vector<txn_id_t>,
        std::greater<txn_id_t>
    > ready;


    for (const auto& entry : indegree) {

        if (entry.second == 0) {
            ready.push(entry.first);
        }
    }


    std::vector<txn_id_t> order;

    while (!ready.empty()) {

        const txn_id_t current = ready.top();

        ready.pop();

        order.push_back(current);


        const auto iterator = graph.find(current);

        if (iterator == graph.end()) {
            continue;
        }


        for (txn_id_t neighbor : iterator->second) {

            --indegree[neighbor];

            if (indegree[neighbor] == 0) {
                ready.push(neighbor);
            }
        }
    }

    return order;
}

} // anonymous namespace


// --------------------------------------------------
// Check
// --------------------------------------------------

SerializabilityResult SerializabilityChecker::Check(
    const std::vector<HistoryEvent>& history
) const {

    // STEP 1: Validate the supplied history.
    ValidateHistory(history);


    // STEP 2: Construct the precedence graph.
    const AdjacencyList graph =
        BuildPrecedenceGraph(history);


    SerializabilityResult result;

    result.transactions_checked = graph.size();


    // STEP 3: Export unique precedence edges.
    for (const auto& entry : graph) {

        const txn_id_t from = entry.first;

        for (txn_id_t to : entry.second) {

            result.precedence_edges.emplace_back(
                from,
                to
            );
        }
    }


    // STEP 4: Run topological sorting.
    std::vector<txn_id_t> order =
        TopologicalSort(graph);


    // STEP 5: An acyclic graph has a valid serial order.
    if (order.size() == graph.size()) {

        result.conflict_serializable = true;

        result.serial_order = std::move(order);

        return result;
    }


    // STEP 6: A cycle means the history is not
    // conflict-serializable.
    result.conflict_serializable = false;

    result.cycle = FindCycle(graph);

    if (result.cycle.empty()) {

        throw TransactionException(
            "Cycle could not be reconstructed from precedence graph"
        );
    }

    // Do not expose a partial topological order as
    // if it were a valid serial execution order.
    result.serial_order.clear();

    return result;
}


// --------------------------------------------------
// IsConflictSerializable
// --------------------------------------------------

bool SerializabilityChecker::IsConflictSerializable(
    const std::vector<HistoryEvent>& history
) const {

    return Check(history).conflict_serializable;
}


} // namespace minidb
