
#include "minidb/concurrency/deadlock_detector.hpp"
#include "minidb/concurrency/wait_for_graph.hpp"

#include <cassert>
#include <iostream>
#include <optional>
#include <vector>

using namespace minidb;

int main() {

    WaitForGraph graph;

    DeadlockDetector detector(graph);


    // ---------------------------------------------
    // TEST 1: Empty graph.
    // ---------------------------------------------

    assert(!detector.HasDeadlock());

    const auto emptyResult =
        detector.DetectDeadlock();

    assert(!emptyResult.has_value());

    std::cout << "No deadlock in empty graph: PASS\n";


    // ---------------------------------------------
    // TEST 2: Acyclic graph.
    // ---------------------------------------------

    const bool edge1 = graph.AddEdge(1, 2);
    const bool edge2 = graph.AddEdge(2, 3);

    assert(edge1);
    assert(edge2);

    assert(!detector.HasDeadlock());

    assert(!detector.DetectDeadlock().has_value());

    std::cout << "Acyclic graph detection: PASS\n";


    // ---------------------------------------------
    // TEST 3: Two-transaction deadlock.
    // ---------------------------------------------

    const bool edge3 = graph.AddEdge(3, 1);

    assert(edge3);

    assert(detector.HasDeadlock());

    const auto result =
        detector.DetectDeadlock();

    assert(result.has_value());

    const std::vector<txn_id_t> expectedCycle = {
        1, 2, 3, 1
    };

    assert(result->cycle == expectedCycle);

    assert(result->victim_txn_id == 3);

    std::cout << "Three-transaction deadlock detection: PASS\n";


    // ---------------------------------------------
    // TEST 4: Victim selection policy.
    // ---------------------------------------------

    assert(
        result->victim_txn_id ==
        3
    );

    // Transaction with the largest ID in this cycle
    // is selected as the victim.

    std::cout << "Youngest transaction victim selection: PASS\n";


    // ---------------------------------------------
    // TEST 5: Detector does not modify the graph.
    // ---------------------------------------------

    const std::size_t edgesBefore =
        graph.GetEdgeCount();

    const auto repeatedResult =
        detector.DetectDeadlock();

    assert(repeatedResult.has_value());

    assert(graph.GetEdgeCount() == edgesBefore);

    assert(detector.HasDeadlock());

    std::cout << "Non-destructive deadlock detection: PASS\n";


    // ---------------------------------------------
    // TEST 6: Removing the victim breaks the cycle.
    // ---------------------------------------------

    graph.RemoveTransaction(
        result->victim_txn_id
    );

    assert(!detector.HasDeadlock());

    assert(!detector.DetectDeadlock().has_value());

    std::cout << "Deadlock removal after victim cleanup: PASS\n";


    // ---------------------------------------------
    // TEST 7: Different transaction IDs.
    // ---------------------------------------------

    WaitForGraph secondGraph;

    DeadlockDetector secondDetector(secondGraph);

    const bool edge4 = secondGraph.AddEdge(4, 19);
    const bool edge5 = secondGraph.AddEdge(19, 12);
    const bool edge6 = secondGraph.AddEdge(12, 4);

    assert(edge4);
    assert(edge5);
    assert(edge6);

    const auto secondResult =
        secondDetector.DetectDeadlock();

    assert(secondResult.has_value());

    assert(secondResult->victim_txn_id == 19);

    std::cout << "Highest transaction ID victim selection: PASS\n";


    // ---------------------------------------------
    // TEST 8: Self-deadlock.
    // ---------------------------------------------

    WaitForGraph thirdGraph;

    DeadlockDetector thirdDetector(thirdGraph);

    const bool selfEdge =
        thirdGraph.AddEdge(44, 44);

    assert(selfEdge);

    const auto selfResult =
        thirdDetector.DetectDeadlock();

    assert(selfResult.has_value());

    const std::vector<txn_id_t> expectedSelfCycle = {
        44, 44
    };

    assert(
        (selfResult->cycle == expectedSelfCycle)
    );

    assert(selfResult->victim_txn_id == 44);

    std::cout << "Self-deadlock detection: PASS\n";


    // ---------------------------------------------
    // TEST 9: Multiple disconnected components.
    // ---------------------------------------------

    WaitForGraph fourthGraph;

    DeadlockDetector fourthDetector(fourthGraph);

    fourthGraph.AddEdge(50, 60);
    fourthGraph.AddEdge(60, 70);

    fourthGraph.AddEdge(80, 90);
    fourthGraph.AddEdge(90, 80);

    const auto fourthResult =
        fourthDetector.DetectDeadlock();

    assert(fourthResult.has_value());

    assert(fourthResult->victim_txn_id == 90);

    std::cout << "Disconnected component detection: PASS\n";


    std::cout << "\nAll DeadlockDetector tests passed!\n";

    return 0;
}
