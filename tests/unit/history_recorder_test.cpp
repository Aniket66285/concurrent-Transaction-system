
#include "minidb/concurrency/history_recorder.hpp"
#include "minidb/common/exception.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

using namespace minidb;


// Helper to verify expected exceptions.
bool ThrowsTransactionException(
    const std::function<void()>& operation
) {

    try {
        operation();
    }
    catch (const TransactionException&) {
        return true;
    }

    return false;
}


int main() {

    HistoryRecorder recorder;


    // ---------------------------------------------
    // TEST 1: Empty history.
    // ---------------------------------------------

    assert(recorder.GetEventCount() == 0);

    assert(recorder.GetHistory().empty());

    std::cout << "Empty history: PASS\n";


    // ---------------------------------------------
    // TEST 2: Record a complete transaction.
    // ---------------------------------------------

    const RID ridA{1, 1};
    const RID ridB{1, 2};

    assert(recorder.RecordBegin(1) == 1);

    assert(recorder.RecordRead(1, ridA) == 2);

    assert(recorder.RecordWrite(1, ridB) == 3);

    assert(recorder.RecordCommit(1) == 4);

    assert(recorder.GetEventCount() == 4);

    assert(!recorder.IsTransactionActive(1));

    std::cout << "Transaction event recording: PASS\n";


    // ---------------------------------------------
    // TEST 3: Verify operation types and ordering.
    // ---------------------------------------------

    const auto history = recorder.GetHistory();

    assert(history.size() == 4);

    assert(history[0].sequence_number == 1);

    assert(history[0].txn_id == 1);

    assert(history[0].operation == HistoryOperation::BEGIN);


    assert(history[1].sequence_number == 2);

    assert(history[1].operation == HistoryOperation::READ);

    assert(history[1].rid == ridA);


    assert(history[2].sequence_number == 3);

    assert(history[2].operation == HistoryOperation::WRITE);

    assert(history[2].rid == ridB);


    assert(history[3].sequence_number == 4);

    assert(history[3].operation == HistoryOperation::COMMIT);

    std::cout << "Global history ordering: PASS\n";


    // ---------------------------------------------
    // TEST 4: Record an aborted transaction.
    // ---------------------------------------------

    assert(recorder.RecordBegin(2) == 5);

    assert(recorder.RecordRead(2, ridA) == 6);

    assert(recorder.RecordAbort(2) == 7);

    assert(!recorder.IsTransactionActive(2));

    std::cout << "Transaction abort recording: PASS\n";


    // ---------------------------------------------
    // TEST 5: Retrieve history of one transaction.
    // ---------------------------------------------

    const auto txn1History =
        recorder.GetTransactionHistory(1);

    assert(txn1History.size() == 4);

    for (const HistoryEvent& event : txn1History) {
        assert(event.txn_id == 1);
    }

    const auto txn2History =
        recorder.GetTransactionHistory(2);

    assert(txn2History.size() == 3);

    for (const HistoryEvent& event : txn2History) {
        assert(event.txn_id == 2);
    }

    std::cout << "Per-transaction history: PASS\n";


    // ---------------------------------------------
    // TEST 6: Reject operations on unknown transaction.
    // ---------------------------------------------

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordRead(99, ridA);
        })
    );

    std::cout << "Unknown transaction rejection: PASS\n";


    // ---------------------------------------------
    // TEST 7: Duplicate BEGIN rejection.
    // ---------------------------------------------

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordBegin(1);
        })
    );

    std::cout << "Duplicate BEGIN rejection: PASS\n";


    // ---------------------------------------------
    // TEST 8: Reject operations after COMMIT.
    // ---------------------------------------------

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordWrite(1, ridA);
        })
    );

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordCommit(1);
        })
    );

    std::cout << "Completed transaction protection: PASS\n";


    // ---------------------------------------------
    // TEST 9: Reject operations after ABORT.
    // ---------------------------------------------

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordRead(2, ridA);
        })
    );

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordAbort(2);
        })
    );

    std::cout << "Aborted transaction protection: PASS\n";


    // ---------------------------------------------
    // TEST 10: Reject reserved transaction ID.
    // ---------------------------------------------

    assert(
        ThrowsTransactionException([&]() {
            recorder.RecordBegin(0);
        })
    );

    std::cout << "Reserved transaction ID rejection: PASS\n";


    // ---------------------------------------------
    // TEST 11: Clear history.
    // ---------------------------------------------

    recorder.Clear();

    assert(recorder.GetEventCount() == 0);

    assert(recorder.GetHistory().empty());

    assert(!recorder.IsTransactionActive(1));

    // Sequence number resets to 1.
    assert(recorder.RecordBegin(1) == 1);

    assert(recorder.RecordCommit(1) == 2);

    std::cout << "History clear and reset: PASS\n";


    // ---------------------------------------------
    // TEST 12: Concurrent history recording.
    // ---------------------------------------------

    HistoryRecorder concurrentRecorder;

    constexpr int kThreadCount = 4;

    constexpr int kOperationsPerThread = 25;

    std::vector<std::thread> workers;

    for (int i = 0; i < kThreadCount; ++i) {

        workers.emplace_back(
            [&concurrentRecorder, i]() {

                const txn_id_t transactionId =
                    static_cast<txn_id_t>(100 + i);

                const std::uint64_t readRid =
                    static_cast<std::uint64_t>(i + 1);

                RID rid{
                    static_cast<page_id_t>(readRid),
                    static_cast<slot_id_t>(1)
                };

                concurrentRecorder.RecordBegin(
                    transactionId
                );

                for (int j = 0;
                     j < kOperationsPerThread;
                     ++j) {

                    concurrentRecorder.RecordRead(
                        transactionId,
                        rid
                    );

                    concurrentRecorder.RecordWrite(
                        transactionId,
                        rid
                    );
                }

                concurrentRecorder.RecordCommit(
                    transactionId
                );
            }
        );
    }


    for (std::thread& worker : workers) {
        worker.join();
    }


    // Each transaction:
    // 1 BEGIN + 25 READ + 25 WRITE + 1 COMMIT = 52 events.
    constexpr std::size_t kExpectedEventsPerThread = 52;

    constexpr std::size_t kExpectedTotalEvents =
        kThreadCount * kExpectedEventsPerThread;

    const auto concurrentHistory =
        concurrentRecorder.GetHistory();

    assert(
        concurrentHistory.size() == kExpectedTotalEvents
    );

    // The global sequence must be unique and continuous.
    for (std::size_t i = 0;
         i < concurrentHistory.size();
         ++i) {

        assert(
            concurrentHistory[i].sequence_number == i + 1
        );
    }

    // Verify each transaction has its complete history.
    for (int i = 0; i < kThreadCount; ++i) {

        const txn_id_t transactionId =
            static_cast<txn_id_t>(100 + i);

        const auto individualHistory =
            concurrentRecorder.GetTransactionHistory(
                transactionId
            );

        assert(
            individualHistory.size() ==
            kExpectedEventsPerThread
        );

        assert(
            individualHistory.front().operation ==
            HistoryOperation::BEGIN
        );

        assert(
            individualHistory.back().operation ==
            HistoryOperation::COMMIT
        );

        assert(
            !concurrentRecorder.IsTransactionActive(
                transactionId
            )
        );
    }

    std::cout << "Concurrent history recording: PASS\n";

    std::cout << "Unique global sequence numbers: PASS\n";

    std::cout << "Concurrent transaction history: PASS\n";


    // ---------------------------------------------
    // TEST 13: Operation name conversion.
    // ---------------------------------------------

    assert(
        std::string(
            HistoryOperationToString(HistoryOperation::READ)
        ) == "READ"
    );

    assert(
        std::string(
            HistoryOperationToString(HistoryOperation::WRITE)
        ) == "WRITE"
    );

    assert(
        std::string(
            HistoryOperationToString(HistoryOperation::COMMIT)
        ) == "COMMIT"
    );

    std::cout << "Operation name conversion: PASS\n";


    std::cout << "\nAll HistoryRecorder tests passed!\n";

    return 0;
}
