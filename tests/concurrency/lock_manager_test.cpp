
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/concurrency/transaction.hpp"
#include "minidb/common/exception.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace minidb;


// Wait until a concurrent operation reaches the expected state.
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

    // ---------------------------------------------
    // TEST 1: Shared lock compatibility.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction t1(1);
        Transaction t2(2);

        RID rid{1, 1};

        assert(manager.LockShared(t1, rid));
        assert(manager.LockShared(t2, rid));

        assert(manager.GetHeldLockCount(1) == 1);
        assert(manager.GetHeldLockCount(2) == 1);

        // Complete and release both transactions.
        t1.Commit();
        manager.ReleaseAll(t1);

        t2.Commit();
        manager.ReleaseAll(t2);

        std::cout << "Shared lock compatibility: PASS\n";
    }


    // ---------------------------------------------
    // TEST 2: Exclusive lock blocks another writer.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction owner(3);
        Transaction waiter(4);

        RID rid{2, 1};

        assert(manager.LockExclusive(owner, rid));

        std::atomic<bool> started{false};

        std::promise<bool> acquiredPromise;

        std::future<bool> acquiredFuture =
            acquiredPromise.get_future();


        std::thread worker([&]() {

            started = true;

            try {

                const bool acquired =
                    manager.LockExclusive(waiter, rid);

                acquiredPromise.set_value(acquired);

                waiter.Commit();

                manager.ReleaseAll(waiter);
            }
            catch (...) {

                acquiredPromise.set_exception(
                    std::current_exception()
                );
            }
        });


        assert(
            WaitUntil([&]() {
                return started.load() &&
                       manager.IsWaiting(4);
            })
        );

        // The second transaction must still be waiting.
        assert(
            acquiredFuture.wait_for(
                std::chrono::milliseconds(0)
            ) == std::future_status::timeout
        );

        assert(manager.GetWaitForEdges().size() == 1);

        owner.Commit();

        manager.ReleaseAll(owner);


        assert(
            acquiredFuture.wait_for(
                std::chrono::seconds(3)
            ) == std::future_status::ready
        );

        assert(acquiredFuture.get());

        worker.join();

        assert(manager.GetHeldLockCount(4) == 0);

        std::cout << "Exclusive lock blocking and wake-up: PASS\n";

        std::cout << "Wait-for edge monitoring: PASS\n";
    }


    // ---------------------------------------------
    // TEST 3: Shared-to-exclusive lock upgrade.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction upgrader(5);
        Transaction reader(6);

        RID rid{3, 1};

        assert(manager.LockShared(upgrader, rid));

        assert(manager.LockShared(reader, rid));

        // Reader completes and releases its shared lock.
        reader.Commit();

        manager.ReleaseAll(reader);

        // The remaining shared holder can upgrade to X.
        assert(manager.LockExclusive(upgrader, rid));

        assert(manager.GetHeldLockCount(5) == 1);

        upgrader.Commit();

        manager.ReleaseAll(upgrader);

        std::cout << "Shared-to-exclusive upgrade: PASS\n";
    }


    // ---------------------------------------------
    // TEST 4: Strict 2PL holds multiple locks.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction txn(7);

        RID first{4, 1};
        RID second{4, 2};

        assert(manager.LockExclusive(txn, first));

        assert(manager.LockExclusive(txn, second));

        assert(manager.GetHeldLockCount(7) == 2);

        // Merely changing the transaction state does not
        // release its locks.
        txn.Commit();

        assert(manager.GetHeldLockCount(7) == 2);

        // The transaction coordinator releases them
        // only after completion.
        manager.ReleaseAll(txn);

        assert(manager.GetHeldLockCount(7) == 0);

        std::cout << "Strict 2PL lock retention: PASS\n";
    }


    // ---------------------------------------------
    // TEST 5: Upgrade conflict is detected.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction first(8);
        Transaction second(9);

        RID rid{5, 1};

        assert(manager.LockShared(first, rid));

        assert(manager.LockShared(second, rid));


        std::atomic<bool> secondVictim{false};

        std::thread firstThread([&]() {

            try {

                // Transaction 8 waits for transaction 9.
                manager.LockExclusive(first, rid);

                first.Commit();

                manager.ReleaseAll(first);
            }
            catch (const TransactionException&) {

                first.Abort();

                manager.ReleaseAll(first);
            }
        });


        assert(
            WaitUntil([&]() {
                return manager.IsWaiting(8);
            })
        );


        try {

            // This upgrade creates the cycle:
            // 8 -> 9 -> 8
            //
            // Largest transaction ID (9) is the victim.
            manager.LockExclusive(second, rid);

            assert(false && "Expected deadlock victim exception");
        }
        catch (const TransactionException&) {

            assert(manager.IsAbortRequested(9));

            second.Abort();

            manager.ReleaseAll(second);

            secondVictim = true;
        }


        firstThread.join();

        assert(secondVictim.load());

        std::cout << "Upgrade deadlock detection: PASS\n";
    }


    // ---------------------------------------------
    // TEST 6: Deadlock with two different resources.
    // Victim is a different thread from the requester.
    // ---------------------------------------------

    {
        LockManager manager;

        // Higher ID transaction is deliberately waiting.
        Transaction victim(20);

        // Lower ID transaction will detect the cycle.
        Transaction requester(10);

        RID resourceA{6, 1};
        RID resourceB{6, 2};

        assert(manager.LockExclusive(victim, resourceA));

        assert(manager.LockExclusive(requester, resourceB));


        std::atomic<bool> victimAborted{false};

        std::atomic<bool> unexpectedAcquisition{false};


        std::thread victimThread([&]() {

            try {

                // Waits for resource B, which is held by requester.
                manager.LockExclusive(victim, resourceB);

                unexpectedAcquisition = true;

                victim.Commit();

                manager.ReleaseAll(victim);
            }
            catch (const TransactionException&) {

                // The selected victim's caller must perform rollback.
                victim.Abort();

                manager.ReleaseAll(victim);

                victimAborted = true;
            }
        });


        assert(
            WaitUntil([&]() {
                return manager.IsWaiting(20);
            })
        );


        // Requester now asks for resource A.
        // This closes the cycle:
        // 10 -> 20 -> 10
        //
        // DeadlockDetector selects transaction 20.
        assert(manager.LockExclusive(requester, resourceA));


        assert(
            WaitUntil([&]() {
                return victimAborted.load();
            })
        );

        assert(!unexpectedAcquisition.load());

        assert(victim.IsAborted());

        requester.Commit();

        manager.ReleaseAll(requester);

        victimThread.join();

        assert(manager.GetHeldLockCount(20) == 0);

        assert(manager.GetHeldLockCount(10) == 0);

        std::cout << "Two-resource deadlock victim handling: PASS\n";

        std::cout << "Victim rollback before lock release: PASS\n";
    }


    // ---------------------------------------------
    // TEST 7: Active transaction cannot release locks.
    // ---------------------------------------------

    {
        LockManager manager;

        Transaction txn(30);

        RID rid{7, 1};

        assert(manager.LockExclusive(txn, rid));

        bool releaseRejected = false;

        try {

            manager.ReleaseAll(txn);
        }
        catch (const TransactionException&) {

            releaseRejected = true;
        }

        assert(releaseRejected);

        assert(manager.GetHeldLockCount(30) == 1);

        txn.Abort();

        manager.ReleaseAll(txn);

        assert(manager.GetHeldLockCount(30) == 0);

        std::cout << "Active transaction lock protection: PASS\n";
    }


    // ---------------------------------------------
    // TEST 8: Invalid transaction is not accepted.
    // ---------------------------------------------

    {
        LockManager manager;

        bool rejected = false;

        try {

            Transaction invalid(0);

            RID rid{8, 1};

            manager.LockShared(invalid, rid);
        }
        catch (const TransactionException&) {

            rejected = true;
        }

        assert(rejected);

        std::cout << "Invalid transaction rejection: PASS\n";
    }


    std::cout << "\nAll LockManager tests passed!\n";

    return 0;
}
