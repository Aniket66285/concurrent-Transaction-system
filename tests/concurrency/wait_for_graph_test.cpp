
#include "minidb/concurrency/wait_for_graph.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace minidb;

int main() {

    WaitForGraph graph;


    // TEST 1: Empty graph.

    assert(graph.GetVertexCount() == 0);
    assert(graph.GetEdgeCount() == 0);
    assert(!graph.HasCycle());

    std::cout << "Empty graph: PASS\n";


    // TEST 2: Add edges without deadlock.

    assert(graph.AddEdge(1, 2));
    assert(graph.AddEdge(2, 3));

    assert(!graph.HasCycle());

    assert(graph.GetEdgeCount() == 2);
    assert(graph.GetVertexCount() == 3);

    std::cout << "Edge insertion: PASS\n";


    // TEST 3: Duplicate edge rejection.

    assert(!graph.AddEdge(1, 2));

    assert(graph.GetEdgeCount() == 2);

    std::cout << "Duplicate edge handling: PASS\n";


    // TEST 4: Create a deadlock cycle.

    assert(graph.AddEdge(3, 1));

    assert(graph.HasCycle());

    const std::vector<txn_id_t> cycle =
        graph.FindCycle();

    const std::vector<txn_id_t> expectedCycle = {
        1, 2, 3, 1
    };

    assert(cycle == expectedCycle);

    std::cout << "Three-transaction deadlock detection: PASS\n";


    // TEST 5: Remove an edge and break the cycle.

    assert(graph.RemoveEdge(3, 1));

    assert(!graph.HasCycle());

    assert(graph.GetEdgeCount() == 2);

    std::cout << "Cycle removal: PASS\n";


    // TEST 6: Self-deadlock detection.

    assert(graph.AddEdge(4, 4));

    assert(graph.HasCycle());

    const auto selfCycle = graph.FindCycle();

    // Extra parentheses are important for assert macro.
    assert(
        (selfCycle == std::vector<txn_id_t>{4, 4})
    );

    std::cout << "Self-cycle detection: PASS\n";


    // TEST 7: Remove a transaction.

    graph.RemoveTransaction(4);

    assert(!graph.HasCycle());

    assert(graph.GetEdgeCount() == 2);

    std::cout << "Transaction removal: PASS\n";


    // TEST 8: Remove incoming and outgoing edges.

    graph.RemoveTransaction(2);

    assert(graph.GetEdgeCount() == 0);

    assert(!graph.HasCycle());

    std::cout << "Incoming/outgoing edge cleanup: PASS\n";


    // TEST 9: Verify graph snapshot.

    WaitForGraph secondGraph;

    secondGraph.AddEdge(10, 20);
    secondGraph.AddEdge(10, 30);
    secondGraph.AddEdge(30, 40);

    const auto edges = secondGraph.GetEdges();

    assert(edges.size() == 3);
    assert(secondGraph.GetVertexCount() == 4);

    std::cout << "Graph snapshot: PASS\n";


    // TEST 10: Invalid transaction ID.

    bool invalidIdRejected = false;

    try {
        graph.AddEdge(0, 1);
    }
    catch (const TransactionException&) {
        invalidIdRejected = true;
    }

    assert(invalidIdRejected);

    std::cout << "Reserved transaction ID rejection: PASS\n";


    std::cout << "\nAll WaitForGraph tests passed!\n";

    return 0;
}
