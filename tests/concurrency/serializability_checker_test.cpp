
#include "minidb/concurrency/serializability_checker.hpp"
#include "minidb/concurrency/history_recorder.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace minidb;


// Helper to verify invalid histories are rejected.
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


// Creates a simple serializable history.
std::vector<HistoryEvent> CreateSerializableHistory() {

    HistoryRecorder recorder;

    RID resourceA{1, 1};

    recorder.RecordBegin(1);

    recorder.RecordBegin(2);

    recorder.RecordWrite(1, resourceA);

    recorder.RecordCommit(1);

    recorder.RecordRead(2, resourceA);

    recorder.RecordCommit(2);

    return recorder.GetHistory();
}


// Creates a cyclic, non-serializable history.
std::vector<HistoryEvent> CreateNonSerializableHistory() {

    HistoryRecorder recorder;

    RID resourceA{1, 1};

    RID resourceB{1, 2};

    recorder.RecordBegin(1);

    recorder.RecordBegin(2);

    recorder.RecordWrite(1, resourceA);

    recorder.RecordWrite(2, resourceB);

    recorder.RecordWrite(1, resourceB);

    recorder.RecordWrite(2, resourceA);

    recorder.RecordCommit(1);

    recorder.RecordCommit(2);

    return recorder.GetHistory();
}


int main() {

    SerializabilityChecker checker;


    // ---------------------------------------------
    // TEST 1: Empty history.
    // ---------------------------------------------

    const std::vector<HistoryEvent> emptyHistory;

    const auto emptyResult =
        checker.Check(emptyHistory);

    assert(emptyResult.conflict_serializable);

    assert(emptyResult.transactions_checked == 0);

    assert(emptyResult.precedence_edges.empty());

    assert(emptyResult.serial_order.empty());

    assert(emptyResult.cycle.empty());

    std::cout << "Empty history: PASS\n";


    // ---------------------------------------------
    // TEST 2: A serializable history.
    // ---------------------------------------------

    const auto serialHistory =
        CreateSerializableHistory();

    const auto serialResult =
        checker.Check(serialHistory);

    assert(serialResult.conflict_serializable);

    assert(serialResult.transactions_checked == 2);

    assert(serialResult.precedence_edges.size() == 1);

    assert(serialResult.precedence_edges[0].first == 1);

    assert(serialResult.precedence_edges[0].second == 2);

    assert(
        (serialResult.serial_order ==
         std::vector<txn_id_t>{1, 2})
    );

    assert(serialResult.cycle.empty());

    std::cout << "Serializable history detection: PASS\n";


    // ---------------------------------------------
    // TEST 3: Different records do not conflict.
    // ---------------------------------------------

    HistoryRecorder differentRecords;

    differentRecords.RecordBegin(1);

    differentRecords.RecordBegin(2);

    differentRecords.RecordWrite(1, RID{1, 1});

    differentRecords.RecordWrite(2, RID{1, 2});

    differentRecords.RecordCommit(1);

    differentRecords.RecordCommit(2);

    const auto differentResult =
        checker.Check(differentRecords.GetHistory());

    assert(differentResult.conflict_serializable);

    assert(differentResult.precedence_edges.empty());

    assert(differentResult.serial_order.size() == 2);

    std::cout << "Different record independence: PASS\n";


    // ---------------------------------------------
    // TEST 4: READ-READ operations do not conflict.
    // ---------------------------------------------

    HistoryRecorder readRead;

    RID ridA{2, 1};

    readRead.RecordBegin(1);

    readRead.RecordBegin(2);

    readRead.RecordRead(1, ridA);

    readRead.RecordRead(2, ridA);

    readRead.RecordCommit(1);

    readRead.RecordCommit(2);

    const auto readReadResult =
        checker.Check(readRead.GetHistory());

    assert(readReadResult.conflict_serializable);

    assert(readReadResult.precedence_edges.empty());

    std::cout << "READ-READ conflict exclusion: PASS\n";


    // ---------------------------------------------
    // TEST 5: WRITE-READ conflict.
    // ---------------------------------------------

    HistoryRecorder writeRead;

    writeRead.RecordBegin(1);

    writeRead.RecordBegin(2);

    writeRead.RecordWrite(1, ridA);

    writeRead.RecordRead(2, ridA);

    writeRead.RecordCommit(1);

    writeRead.RecordCommit(2);

    const auto writeReadResult =
        checker.Check(writeRead.GetHistory());

    assert(writeReadResult.precedence_edges.size() == 1);

    assert(writeReadResult.precedence_edges[0].first == 1);

    assert(writeReadResult.precedence_edges[0].second == 2);

    std::cout << "WRITE-READ conflict detection: PASS\n";


    // ---------------------------------------------
    // TEST 6: READ-WRITE conflict.
    // ---------------------------------------------

    HistoryRecorder readWrite;

    readWrite.RecordBegin(1);

    readWrite.RecordBegin(2);

    readWrite.RecordRead(1, ridA);

    readWrite.RecordWrite(2, ridA);

    readWrite.RecordCommit(1);

    readWrite.RecordCommit(2);

    const auto readWriteResult =
        checker.Check(readWrite.GetHistory());

    assert(readWriteResult.precedence_edges.size() == 1);

    assert(readWriteResult.precedence_edges[0].first == 1);

    assert(readWriteResult.precedence_edges[0].second == 2);

    std::cout << "READ-WRITE conflict detection: PASS\n";


    // ---------------------------------------------
    // TEST 7: WRITE-WRITE conflict.
    // ---------------------------------------------

    HistoryRecorder writeWrite;

    writeWrite.RecordBegin(1);

    writeWrite.RecordBegin(2);

    writeWrite.RecordWrite(1, ridA);

    writeWrite.RecordWrite(2, ridA);

    writeWrite.RecordCommit(1);

    writeWrite.RecordCommit(2);

    const auto writeWriteResult =
        checker.Check(writeWrite.GetHistory());

    assert(writeWriteResult.precedence_edges.size() == 1);

    assert(writeWriteResult.precedence_edges[0].first == 1);

    assert(writeWriteResult.precedence_edges[0].second == 2);

    std::cout << "WRITE-WRITE conflict detection: PASS\n";


    // ---------------------------------------------
    // TEST 8: Cyclic precedence graph.
    // ---------------------------------------------

    const auto nonSerializableHistory =
        CreateNonSerializableHistory();

    const auto cycleResult =
        checker.Check(nonSerializableHistory);

    assert(!cycleResult.conflict_serializable);

    assert(cycleResult.transactions_checked == 2);

    assert(cycleResult.precedence_edges.size() == 2);

    assert(cycleResult.serial_order.empty());

    const std::vector<txn_id_t> expectedCycle = {
        1, 2, 1
    };

    assert(cycleResult.cycle == expectedCycle);

    std::cout << "Non-serializable cycle detection: PASS\n";


    // ---------------------------------------------
    // TEST 9: Duplicate edges must be removed.
    // ---------------------------------------------

    HistoryRecorder repeatedConflicts;

    repeatedConflicts.RecordBegin(1);

    repeatedConflicts.RecordBegin(2);

    repeatedConflicts.RecordWrite(1, ridA);

    repeatedConflicts.RecordWrite(1, ridA);

    repeatedConflicts.RecordWrite(2, ridA);

    repeatedConflicts.RecordWrite(2, ridA);

    repeatedConflicts.RecordCommit(1);

    repeatedConflicts.RecordCommit(2);

    const auto repeatedResult =
        checker.Check(repeatedConflicts.GetHistory());

    assert(repeatedResult.precedence_edges.size() == 1);

    assert(repeatedResult.precedence_edges[0].first == 1);

    assert(repeatedResult.precedence_edges[0].second == 2);

    std::cout << "Duplicate precedence edge elimination: PASS\n";


    // ---------------------------------------------
    // TEST 10: Same transaction operations are ignored.
    // ---------------------------------------------

    HistoryRecorder sameTransaction;

    sameTransaction.RecordBegin(1);

    sameTransaction.RecordWrite(1, ridA);

    sameTransaction.RecordRead(1, ridA);

    sameTransaction.RecordWrite(1, ridA);

    sameTransaction.RecordCommit(1);

    const auto sameTransactionResult =
        checker.Check(sameTransaction.GetHistory());

    assert(sameTransactionResult.conflict_serializable);

    assert(sameTransactionResult.precedence_edges.empty());

    assert(sameTransactionResult.serial_order.size() == 1);

    assert(sameTransactionResult.serial_order[0] == 1);

    std::cout << "Same-transaction conflict exclusion: PASS\n";


    // ---------------------------------------------
    // TEST 11: Aborted transaction history.
    // ---------------------------------------------

    HistoryRecorder abortedHistory;

    abortedHistory.RecordBegin(1);

    abortedHistory.RecordBegin(2);

    abortedHistory.RecordWrite(1, ridA);

    abortedHistory.RecordRead(2, ridA);

    abortedHistory.RecordAbort(1);

    abortedHistory.RecordCommit(2);

    const auto abortedResult =
        checker.Check(abortedHistory.GetHistory());

    // This checker includes operations recorded before ABORT.
    // It analyzes the supplied history, not just the
    // projection of committed transactions.
    assert(abortedResult.precedence_edges.size() == 1);

    assert(abortedResult.precedence_edges[0].first == 1);

    assert(abortedResult.precedence_edges[0].second == 2);

    std::cout << "Aborted transaction history handling: PASS\n";


    // ---------------------------------------------
    // TEST 12: Incomplete active transactions.
    // ---------------------------------------------

    HistoryRecorder activeHistory;

    activeHistory.RecordBegin(1);

    activeHistory.RecordWrite(1, ridA);

    const auto activeResult =
        checker.Check(activeHistory.GetHistory());

    assert(activeResult.conflict_serializable);

    assert(activeResult.transactions_checked == 1);

    assert(activeResult.serial_order.size() == 1);

    assert(activeResult.serial_order[0] == 1);

    std::cout << "Active transaction history: PASS\n";


    // ---------------------------------------------
    // TEST 13: Missing BEGIN must be rejected.
    // ---------------------------------------------

    HistoryEvent invalidRead;

    invalidRead.sequence_number = 1;

    invalidRead.txn_id = 99;

    invalidRead.operation = HistoryOperation::READ;

    invalidRead.rid = ridA;

    const std::vector<HistoryEvent> missingBegin = {
        invalidRead
    };

    assert(
        ThrowsTransactionException([&]() {
            checker.Check(missingBegin);
        })
    );

    std::cout << "Missing BEGIN rejection: PASS\n";


    // ---------------------------------------------
    // TEST 14: Duplicate BEGIN must be rejected.
    // ---------------------------------------------

    HistoryRecorder duplicateSource;

    duplicateSource.RecordBegin(1);

    duplicateSource.RecordCommit(1);

    auto duplicateHistory =
        duplicateSource.GetHistory();

    HistoryEvent duplicateBegin;

    duplicateBegin.sequence_number = 3;

    duplicateBegin.txn_id = 1;

    duplicateBegin.operation = HistoryOperation::BEGIN;

    duplicateHistory.push_back(duplicateBegin);

    assert(
        ThrowsTransactionException([&]() {
            checker.Check(duplicateHistory);
        })
    );

    std::cout << "Duplicate BEGIN rejection: PASS\n";


    // ---------------------------------------------
    // TEST 15: Operation after COMMIT must be rejected.
    // ---------------------------------------------

    HistoryRecorder completedSource;

    completedSource.RecordBegin(1);

    completedSource.RecordCommit(1);

    auto completedHistory =
        completedSource.GetHistory();

    HistoryEvent operationAfterCommit;

    operationAfterCommit.sequence_number = 3;

    operationAfterCommit.txn_id = 1;

    operationAfterCommit.operation = HistoryOperation::WRITE;

    operationAfterCommit.rid = ridA;

    completedHistory.push_back(operationAfterCommit);

    assert(
        ThrowsTransactionException([&]() {
            checker.Check(completedHistory);
        })
    );

    std::cout << "Operation-after-COMMIT rejection: PASS\n";


    // ---------------------------------------------
    // TEST 16: Invalid sequence ordering.
    // ---------------------------------------------

    auto unorderedHistory =
        CreateSerializableHistory();

    unorderedHistory[2].sequence_number =
        unorderedHistory[1].sequence_number;

    assert(
        ThrowsTransactionException([&]() {
            checker.Check(unorderedHistory);
        })
    );

    std::cout << "Invalid sequence ordering rejection: PASS\n";


    // ---------------------------------------------
    // TEST 17: Reserved transaction ID.
    // ---------------------------------------------

    HistoryEvent invalidTransaction;

    invalidTransaction.sequence_number = 1;

    invalidTransaction.txn_id = 0;

    invalidTransaction.operation = HistoryOperation::BEGIN;

    const std::vector<HistoryEvent> reservedIdHistory = {
        invalidTransaction
    };

    assert(
        ThrowsTransactionException([&]() {
            checker.Check(reservedIdHistory);
        })
    );

    std::cout << "Reserved transaction ID rejection: PASS\n";


    // ---------------------------------------------
    // TEST 18: Deterministic serial order.
    // ---------------------------------------------

    HistoryRecorder independentTransactions;

    independentTransactions.RecordBegin(30);

    independentTransactions.RecordBegin(10);

    independentTransactions.RecordBegin(20);

    independentTransactions.RecordCommit(30);

    independentTransactions.RecordCommit(10);

    independentTransactions.RecordCommit(20);

    const auto independentResult =
        checker.Check(independentTransactions.GetHistory());

    const std::vector<txn_id_t> expectedOrder = {
        10, 20, 30
    };

    assert(independentResult.conflict_serializable);

    assert(independentResult.serial_order == expectedOrder);

    std::cout << "Deterministic serial order: PASS\n";


    std::cout << "\nAll SerializabilityChecker tests passed!\n";

    return 0;
}
