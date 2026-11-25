
#pragma once

#include "minidb/common/types.hpp"

#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace minidb {

class WaitForGraph {
public:

    WaitForGraph() = default;

    // Copying is not allowed because the graph owns a mutex.
    WaitForGraph(const WaitForGraph&) = delete;

    WaitForGraph& operator=(const WaitForGraph&) = delete;

    // Adds an edge: waiting_txn -> blocking_txn.
    // Returns true if a new edge was inserted.
    // Returns false if the edge already exists.
    bool AddEdge(
        txn_id_t waiting_txn,
        txn_id_t blocking_txn
    );

    // Removes a particular waiting relationship.
    bool RemoveEdge(
        txn_id_t waiting_txn,
        txn_id_t blocking_txn
    );

    // Removes a transaction and all its incoming/outgoing edges.
    void RemoveTransaction(txn_id_t transaction_id);

    // Returns true if a cycle exists.
    bool HasCycle() const;

    // Returns one cycle.
    // Empty vector means no cycle.
    // Example: {1, 2, 3, 1}.
    std::vector<txn_id_t> FindCycle() const;

    // Returns all edges in the graph.
    std::vector<std::pair<txn_id_t, txn_id_t>>
    GetEdges() const;

    // Returns number of edges.
    std::size_t GetEdgeCount() const;

    // Returns number of vertices.
    std::size_t GetVertexCount() const;

private:

    // Each transaction maps to the transactions it is waiting for.
    std::map<txn_id_t, std::set<txn_id_t>> graph_;

    mutable std::mutex mutex_;
};

} // namespace minidb
