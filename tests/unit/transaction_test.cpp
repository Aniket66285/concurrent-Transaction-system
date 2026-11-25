
#include "minidb/concurrency/transaction.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <functional>
#include <iostream>

using namespace minidb;

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

    // TEST 1: Transaction creation.
    Transaction txn1(1);

    assert(txn1.GetId() == 1);
    assert(txn1.IsActive());
    assert(txn1.GetState() == TransactionState::ACTIVE);
    assert(txn1.GetLastLSN() == kInvalidLSN);

    std::cout << "Transaction creation: PASS\n";


    // TEST 2: LSN management.
    txn1.SetLastLSN(10);
    assert(txn1.GetLastLSN() == 10);

    txn1.SetLastLSN(20);
    assert(txn1.GetLastLSN() == 20);

    std::cout << "LSN management: PASS\n";


    // TEST 3: Invalid LSN rejection.
    assert(
        ThrowsTransactionException([&]() {
            txn1.SetLastLSN(kInvalidLSN);
        })
    );

    std::cout << "Invalid LSN rejection: PASS\n";


    // TEST 4: LSN must increase.
    assert(
        ThrowsTransactionException([&]() {
            txn1.SetLastLSN(15);
        })
    );

    assert(
        ThrowsTransactionException([&]() {
            txn1.SetLastLSN(20);
        })
    );

    std::cout << "Monotonic LSN validation: PASS\n";


    // TEST 5: Commit transaction.
    txn1.Commit();

    assert(txn1.IsCommitted());
    assert(!txn1.IsActive());

    assert(
        txn1.GetState() == TransactionState::COMMITTED
    );

    std::cout << "Transaction commit: PASS\n";


    // TEST 6: Completed transaction protection.
    assert(
        ThrowsTransactionException([&]() {
            txn1.SetLastLSN(30);
        })
    );

    assert(
        ThrowsTransactionException([&]() {
            txn1.EnsureActive();
        })
    );

    std::cout << "Completed transaction protection: PASS\n";


    // TEST 7: Abort transaction.
    Transaction txn2(2);

    assert(txn2.IsActive());

    txn2.SetLastLSN(25);

    txn2.Abort();

    assert(txn2.IsAborted());
    assert(!txn2.IsActive());

    assert(
        txn2.GetState() == TransactionState::ABORTED
    );

    std::cout << "Transaction abort: PASS\n";


    // TEST 8: Invalid state transitions.
    assert(
        ThrowsTransactionException([&]() {
            txn2.Abort();
        })
    );

    assert(
        ThrowsTransactionException([&]() {
            txn2.Commit();
        })
    );

    std::cout << "Invalid state transition rejection: PASS\n";


    // TEST 9: Transaction ID zero rejection.
    assert(
        ThrowsTransactionException([]() {
            (void)Transaction(0);
        })
    );

    std::cout << "Reserved transaction ID rejection: PASS\n";


    std::cout << "\nAll Transaction tests passed!\n";

    return 0;
}
