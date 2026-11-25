
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/concurrency/transaction.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/common/exception.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace minidb;


// Wait until a concurrent condition becomes true.
template <typename Predicate>
bool WaitUntil(Predicate predicate) {

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(3);

    while (std::chrono::steady_clock::now() < deadline) {

        if (predicate()) {
            return true;
        }

        std::this_thread::yield();
    }

    return predicate();
}


int main() {

    const std::string walFile =
        "data/txn_lock_integration_test.wal";

    std::filesystem::remove(walFile);

    {
        LogManager logManager(walFile);

        LockManager lockManager;

        TxnManager txnManager(
            logManager,
            &lockManager
        );


        // -----------------------------------------
        // TEST 1: COMMIT automatically releases locks.
        // -----------------------------------------

        auto t1 = txnManager.BeginTransaction();

        auto t2 = txnManager.BeginTransaction();

        RID resourceA{1, 1};

        assert(
            txnManager.LockExclusive(*t1, resourceA)
        );

        assert(
            lockManager.GetHeldLockCount(t1->GetId()) == 1
        );


        std::promise<bool> commitWaiterPromise;

        auto commitWaiterFuture =
            commitWaiterPromise.get_future();


        std::thread commitWaiter([&]() {

            try {

                const bool acquired =
                    txnManager.LockExclusive(*t2, resourceA);

                txnManager.CommitTransaction(*t2);

                commitWaiterPromise.set_value(acquired);
            }
            catch (...) {

                commitWaiterPromise.set_exception(
                    std::current_exception()
                );
            }
        });


        assert(
            WaitUntil([&]() {
                return lockManager.IsWaiting(t2->GetId());
            })
        );


        txnManager.CommitTransaction(*t1);


        assert(
            commitWaiterFuture.wait_for(
                std::chrono::seconds(3)
            ) == std::future_status::ready
        );

        assert(commitWaiterFuture.get());

        commitWaiter.join();


        assert(t1->IsCommitted());
        assert(t2->IsCommitted());

        assert(
            lockManager.GetHeldLockCount(t1->GetId()) == 0
        );

        assert(
            lockManager.GetHeldLockCount(t2->GetId()) == 0
        );

        std::cout << "Automatic lock release after COMMIT: PASS\n";

        std::cout << "Waiting transaction resumes: PASS\n";


        // -----------------------------------------
        // TEST 2: ABORT performs undo before releasing locks.
        // -----------------------------------------

        auto t3 = txnManager.BeginTransaction();

        auto t4 = txnManager.BeginTransaction();

        RID resourceB{2, 1};

        assert(
            txnManager.LockExclusive(*t3, resourceB)
        );


        LogRecord updateRecord;

        updateRecord.type = LogRecordType::UPDATE;
        updateRecord.has_page = true;
        updateRecord.page_id = resourceB.page_id;
        updateRecord.slot_id = resourceB.slot_id;

        updateRecord.before_image = {'A'};
        updateRecord.after_image = {'B'};


        txnManager.AppendLogRecord(
            *t3,
            updateRecord
        );


        std::atomic<int> undoCount{0};

        std::atomic<bool> pageFlushCalled{false};


        std::promise<bool> abortWaiterPromise;

        auto abortWaiterFuture =
            abortWaiterPromise.get_future();


        std::thread abortWaiter([&]() {

            try {

                const bool acquired =
                    txnManager.LockExclusive(*t4, resourceB);

                txnManager.CommitTransaction(*t4);

                abortWaiterPromise.set_value(acquired);
            }
            catch (...) {

                abortWaiterPromise.set_exception(
                    std::current_exception()
                );
            }
        });


        assert(
            WaitUntil([&]() {
                return lockManager.IsWaiting(t4->GetId());
            })
        );


        txnManager.AbortTransaction(
            *t3,

            [&](const LogRecord& record) {

                assert(
                    record.type == LogRecordType::UPDATE
                );

                assert(
                    record.before_image ==
                    std::vector<std::uint8_t>{'A'}
                );

                ++undoCount;
            },

            [&]() {
                pageFlushCalled = true;
            }
        );


        assert(t3->IsAborted());

        assert(undoCount.load() == 1);

        assert(pageFlushCalled.load());


        assert(
            abortWaiterFuture.wait_for(
                std::chrono::seconds(3)
            ) == std::future_status::ready
        );

        assert(abortWaiterFuture.get());

        abortWaiter.join();


        assert(t4->IsCommitted());

        assert(
            lockManager.GetHeldLockCount(t3->GetId()) == 0
        );

        std::cout << "Undo before lock release on ABORT: PASS\n";

        std::cout << "Automatic lock release after ABORT: PASS\n";


        // -----------------------------------------
        // TEST 3: Deadlock victim rollback.
        // -----------------------------------------

        auto t5 = txnManager.BeginTransaction();

        auto t6 = txnManager.BeginTransaction();

        RID resourceC{3, 1};

        RID resourceD{3, 2};


        assert(
            txnManager.LockExclusive(*t5, resourceC)
        );

        assert(
            txnManager.LockExclusive(*t6, resourceD)
        );


        std::promise<bool> deadlockWaiterPromise;

        auto deadlockWaiterFuture =
            deadlockWaiterPromise.get_future();


        std::thread deadlockWaiter([&]() {

            try {

                const bool acquired =
                    txnManager.LockExclusive(*t5, resourceD);

                txnManager.CommitTransaction(*t5);

                deadlockWaiterPromise.set_value(acquired);
            }
            catch (...) {

                deadlockWaiterPromise.set_exception(
                    std::current_exception()
                );
            }
        });


        assert(
            WaitUntil([&]() {
                return lockManager.IsWaiting(t5->GetId());
            })
        );


        // T6 requests C and completes the deadlock cycle.
        bool victimExceptionReceived = false;

        try {

            txnManager.LockExclusive(*t6, resourceC);
        }
        catch (const TransactionException&) {

            victimExceptionReceived = true;
        }


        assert(victimExceptionReceived);

        assert(
            lockManager.IsAbortRequested(t6->GetId())
        );


        // Rollback the selected victim before releasing locks.
        txnManager.AbortTransaction(
            *t6,

            [](const LogRecord&) {
                // No data changes in this deadlock test.
            },

            []() {
                // No modified database pages in this test.
            }
        );


        assert(t6->IsAborted());


        assert(
            deadlockWaiterFuture.wait_for(
                std::chrono::seconds(3)
            ) == std::future_status::ready
        );

        assert(deadlockWaiterFuture.get());

        deadlockWaiter.join();


        assert(t5->IsCommitted());

        assert(
            lockManager.GetHeldLockCount(t5->GetId()) == 0
        );

        assert(
            lockManager.GetHeldLockCount(t6->GetId()) == 0
        );

        assert(lockManager.GetWaitingCount() == 0);

        std::cout << "Deadlock victim rollback: PASS\n";

        std::cout << "Deadlock victim lock release: PASS\n";

        std::cout << "Other transaction resumes after victim rollback: PASS\n";


        // -----------------------------------------
        // TEST 4: Active transaction cleanup.
        // -----------------------------------------

        assert(
            txnManager.GetActiveTransactionCount() == 0
        );

        std::cout << "Active transaction cleanup: PASS\n";
    }


    std::filesystem::remove(walFile);

    std::cout << "\nAll TxnManager-LockManager integration tests passed!\n";

    return 0;
}
