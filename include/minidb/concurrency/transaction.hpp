
#pragma once

#include "minidb/common/types.hpp"

#include <mutex>

namespace minidb {

class Transaction {
public:

    // Creates a new ACTIVE transaction.
    // Transaction ID zero is reserved for system records.
    explicit Transaction(txn_id_t transaction_id);

    // Transaction objects are not copyable or movable.
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    // Returns the transaction ID.
    txn_id_t GetId() const noexcept;

    // Returns the current transaction state.
    TransactionState GetState() const;

    // Returns the latest WAL record LSN.
    lsn_t GetLastLSN() const;

    // Updates the latest WAL record LSN.
    void SetLastLSN(lsn_t lsn);

    // Transaction state checks.
    bool IsActive() const;

    bool IsCommitted() const;

    bool IsAborted() const;

    // Changes transaction state.
    void Commit();

    void Abort();

    // Throws an exception if the transaction is not ACTIVE.
    void EnsureActive() const;

    std::unique_lock<std::recursive_mutex> AcquireOperationLock();

private:

    const txn_id_t transaction_id_;

    mutable std::mutex mutex_;
    std::recursive_mutex operation_mutex_;

    TransactionState state_{TransactionState::ACTIVE};

    lsn_t last_lsn_{kInvalidLSN};
};

} // namespace minidb
