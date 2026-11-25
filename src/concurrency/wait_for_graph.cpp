
#include "minidb/concurrency/wait_for_graph.hpp"

#include "minidb/common/exception.hpp"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace minidb {


// --------------------------------------------------
// AddEdge
// --------------------------------------------------

bool WaitForGraph::AddEdge(
    txn_id_t waiting_txn,
    txn_id_t blocking_txn
) {

    if (waiting_txn == 0 || blocking_txn == 0) {
        throw TransactionException(
            "Transaction ID zero cannot be used in WaitForGraph"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure both transactions exist as graph vertices.
    graph_.try_emplace(waiting_txn);

    graph_.try_emplace(blocking_txn);

    // Add the directed edge.
    const bool inserted =
        graph_[waiting_txn].insert(blocking_txn).second;

    return inserted;
}


// --------------------------------------------------
// RemoveEdge
// --------------------------------------------------

bool WaitForGraph::RemoveEdge(
    txn_id_t waiting_txn,
    txn_id_t blocking_txn
) {

    if (waiting_txn == 0 || blocking_txn == 0) {
        throw TransactionException(
            "Invalid transaction ID in RemoveEdge"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator = graph_.find(waiting_txn);

    if (iterator == graph_.end()) {
        return false;
    }

    // Returns true if the edge was actually removed.
    return iterator->second.erase(blocking_txn) > 0;
}


// --------------------------------------------------
// RemoveTransaction
// --------------------------------------------------

void WaitForGraph::RemoveTransaction(
    txn_id_t transaction_id
) {

    if (transaction_id == 0) {
        throw TransactionException(
            "Invalid transaction ID"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove all outgoing edges.
    graph_.erase(transaction_id);

    // Remove all incoming edges.
    for (auto& entry : graph_) {

        entry.second.erase(transaction_id);
    }
}


// --------------------------------------------------
// FindCycle
// --------------------------------------------------

std::vector<txn_id_t> WaitForGraph::FindCycle() const {

    std::lock_guard<std::mutex> lock(mutex_);

    // DFS states:
    // 0 = not visited
    // 1 = currently exploring
    // 2 = completely explored

    std::map<txn_id_t, int> visited;

    std::vector<txn_id_t> path;

    std::vector<txn_id_t> cycle;

    std::function<bool(txn_id_t)> dfs;


    dfs = [&](txn_id_t current) -> bool {

        visited[current] = 1;

        path.push_back(current);

        const auto iterator = graph_.find(current);

        if (iterator != graph_.end()) {

            // Visit every transaction this transaction is waiting for.
            for (txn_id_t neighbor : iterator->second) {

                const int state = visited[neighbor];


                // Not visited yet: explore it.
                if (state == 0) {

                    if (dfs(neighbor)) {
                        return true;
                    }
                }

                // Currently exploring: a back edge
                // means a cycle exists.
                else if (state == 1) {

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

                    // Close the cycle.
                    cycle.push_back(neighbor);

                    return true;
                }
            }
        }

        // Backtrack.
        path.pop_back();

        visited[current] = 2;

        return false;
    };


    // Check every connected component.
    for (const auto& entry : graph_) {

        const txn_id_t transaction_id = entry.first;

        if (visited[transaction_id] == 0) {

            if (dfs(transaction_id)) {
                return cycle;
            }
        }
    }

    return {};
}


// --------------------------------------------------
// HasCycle
// --------------------------------------------------

bool WaitForGraph::HasCycle() const {

    return !FindCycle().empty();
}


// --------------------------------------------------
// GetEdges
// --------------------------------------------------

std::vector<std::pair<txn_id_t, txn_id_t>>
WaitForGraph::GetEdges() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<txn_id_t, txn_id_t>> edges;

    for (const auto& entry : graph_) {

        const txn_id_t waitingTxn = entry.first;

        for (txn_id_t blockingTxn : entry.second) {

            edges.emplace_back(
                waitingTxn,
                blockingTxn
            );
        }
    }

    return edges;
}


// --------------------------------------------------
// GetEdgeCount
// --------------------------------------------------

std::size_t WaitForGraph::GetEdgeCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t count = 0;

    for (const auto& entry : graph_) {

        count += entry.second.size();
    }

    return count;
}


// --------------------------------------------------
// GetVertexCount
// --------------------------------------------------

std::size_t WaitForGraph::GetVertexCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return graph_.size();
}


} // namespace minidb
