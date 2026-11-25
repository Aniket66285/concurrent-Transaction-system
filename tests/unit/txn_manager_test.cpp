
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/common/exception.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace minidb;

int main() {

    const std::string walFile =
        "data/txn_manager_test.wal";

    std::filesystem::remove(walFile);

    std::vector<slot_id_t> undoneSlots;

    bool flushCalled = false;


    // ---------------------------------------------
    // TEST 1: Create Transaction Manager.
    // ---------------------------------------------

    {
        LogManager logManager(walFile);

        TxnManager txnManager(logManager);


        // -----------------------------------------
        // TEST 2: Begin Transaction T1.
        // -----------------------------------------

        auto txn1 = txnManager.BeginTransaction();

        assert(txn1 != nullptr);

        assert(txn1->GetId() == 1);

        assert(txn1->IsActive());

        assert(txnManager.GetActiveTransactionCount() == 1);

        assert(
            txnManager.GetActiveTransaction(1) == txn1
        );

        std::cout << "Begin transaction: PASS\n";


        // -----------------------------------------
        // TEST 3: Append UPDATE record to T1.
        // -----------------------------------------

        LogRecord update1;

        update1.type = LogRecordType::UPDATE;
        update1.has_page = true;
        update1.page_id = 1;
        update1.slot_id = 1;

        update1.before_image = {'A'};
        update1.after_image = {'B'};

        const lsn_t update1LSN =
            txnManager.AppendLogRecord(
                *txn1,
                update1
            );

        assert(update1LSN != kInvalidLSN);

        assert(txn1->GetLastLSN() == update1LSN);

        // WAL must be durable before the caller
        // changes the corresponding database page.
        assert(
            logManager.GetPersistentLSN() == update1LSN
        );

        std::cout << "Transaction WAL append: PASS\n";

        std::cout << "WAL-before-data durability: PASS\n";


        // -----------------------------------------
        // TEST 4: Commit T1.
        // -----------------------------------------

        txnManager.CommitTransaction(*txn1);

        assert(txn1->IsCommitted());

        assert(!txn1->IsActive());

        assert(
            logManager.GetPersistentLSN() ==
            txn1->GetLastLSN()
        );

        assert(txnManager.GetActiveTransactionCount() == 0);

        assert(
            txnManager.GetActiveTransaction(1) == nullptr
        );

        std::cout << "Transaction commit: PASS\n";


        // -----------------------------------------
        // TEST 5: Begin Transaction T2.
        // -----------------------------------------

        auto txn2 = txnManager.BeginTransaction();

        assert(txn2->GetId() == 2);

        assert(txn2->IsActive());

        std::cout << "Second transaction creation: PASS\n";


        // -----------------------------------------
        // TEST 6: Append two UPDATE records.
        // -----------------------------------------

        LogRecord update2a;

        update2a.type = LogRecordType::UPDATE;
        update2a.has_page = true;
        update2a.page_id = 2;
        update2a.slot_id = 2;

        update2a.before_image = {'C'};
        update2a.after_image = {'D'};

        const lsn_t update2aLSN =
            txnManager.AppendLogRecord(
                *txn2,
                update2a
            );


        LogRecord update2b;

        update2b.type = LogRecordType::UPDATE;
        update2b.has_page = true;
        update2b.page_id = 2;
        update2b.slot_id = 3;

        update2b.before_image = {'E'};
        update2b.after_image = {'F'};

        const lsn_t update2bLSN =
            txnManager.AppendLogRecord(
                *txn2,
                update2b
            );

        assert(update2bLSN > update2aLSN);

        assert(txn2->GetLastLSN() == update2bLSN);

        std::cout << "Multiple transaction log records: PASS\n";


        // -----------------------------------------
        // TEST 7: Abort T2.
        // -----------------------------------------

        const auto undoCallback =
            [&](const LogRecord& record) {

                // UNDO callback receives records in reverse order.
                undoneSlots.push_back(record.slot_id);

                // Actual storage integration will restore
                // record.before_image in the database page.
                assert(!record.before_image.empty());
            };


        const auto flushCallback = [&]() {

            flushCalled = true;
        };


        txnManager.AbortTransaction(
            *txn2,
            undoCallback,
            flushCallback
        );


        assert(txn2->IsAborted());

        assert(!txn2->IsActive());

        assert(flushCalled);

        assert(
            txnManager.GetActiveTransactionCount() == 0
        );

        assert(
            txnManager.GetActiveTransaction(2) == nullptr
        );

        std::cout << "Transaction abort: PASS\n";


        // -----------------------------------------
        // TEST 8: Reverse-order undo.
        // -----------------------------------------

        assert(undoneSlots.size() == 2);

        assert(undoneSlots[0] == 3);

        assert(undoneSlots[1] == 2);

        std::cout << "Reverse-order undo: PASS\n";


        // -----------------------------------------
        // TEST 9: Read and verify WAL.
        // -----------------------------------------

        const auto records =
            logManager.ReadAllRecords(false);

        assert(records.size() == 7);

        assert(records[0].type == LogRecordType::BEGIN);

        assert(records[1].type == LogRecordType::UPDATE);

        assert(records[2].type == LogRecordType::COMMIT);

        assert(records[3].type == LogRecordType::BEGIN);

        assert(records[4].type == LogRecordType::UPDATE);

        assert(records[5].type == LogRecordType::UPDATE);

        assert(records[6].type == LogRecordType::ABORT);

        // Verify T1's previous-LSN chain.
        assert(records[1].prev_lsn == records[0].lsn);

        assert(records[2].prev_lsn == records[1].lsn);

        // Verify T2's previous-LSN chain.
        assert(records[4].prev_lsn == records[3].lsn);

        assert(records[5].prev_lsn == records[4].lsn);

        assert(records[6].prev_lsn == records[5].lsn);

        std::cout << "WAL transaction history: PASS\n";


        // -----------------------------------------
        // TEST 10: Restart TxnManager.
        // -----------------------------------------

        TxnManager restartedManager(logManager);

        auto txn3 = restartedManager.BeginTransaction();

        // IDs must not be reused after restart.
        assert(txn3->GetId() == 3);

        restartedManager.CommitTransaction(*txn3);

        assert(txn3->IsCommitted());

        assert(
            restartedManager.GetActiveTransactionCount() == 0
        );

        std::cout << "Transaction ID recovery: PASS\n";
    }


    // Remove the test WAL file.
    std::filesystem::remove(walFile);

    std::cout << "\nAll TxnManager tests passed!\n";

    return 0;
}
