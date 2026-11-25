
#include "minidb/concurrency/transaction.hpp"

#include "minidb/common/exception.hpp"

#include <mutex>

namespace minidb {


// --------------------------------------------------
// Constructor
// --------------------------------------------------

Transaction::Transaction(txn_id_t transaction_id)
    : transaction_id_(transaction_id) {

    // Transaction ID zero is reserved for system records.
    if (transaction_id_ == 0) {
        throw TransactionException(
            "Transaction ID zero is reserved"
        );
    }
}


// --------------------------------------------------
// GetId
// --------------------------------------------------

txn_id_t Transaction::GetId() const noexcept {

    return transaction_id_;
}


// --------------------------------------------------
// GetState
// --------------------------------------------------

TransactionState Transaction::GetState() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return state_;
}


// --------------------------------------------------
// GetLastLSN
// --------------------------------------------------

lsn_t Transaction::GetLastLSN() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return last_lsn_;
}


// --------------------------------------------------
// SetLastLSN
// --------------------------------------------------

void Transaction::SetLastLSN(lsn_t lsn) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != TransactionState::ACTIVE) {
        throw TransactionException(
            "Cannot update LSN of an inactive transaction"
        );
    }

    if (lsn == kInvalidLSN) {
        throw TransactionException(
            "Cannot assign an invalid LSN"
        );
    }

    // Every new record belonging to a transaction
    // must have a greater LSN than its previous record.
    if (last_lsn_ != kInvalidLSN && lsn <= last_lsn_) {
        throw TransactionException(
            "Transaction LSN must increase monotonically"
        );
    }

    last_lsn_ = lsn;
}


// --------------------------------------------------
// IsActive
// --------------------------------------------------

bool Transaction::IsActive() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return state_ == TransactionState::ACTIVE;
}


// --------------------------------------------------
// IsCommitted
// --------------------------------------------------

bool Transaction::IsCommitted() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return state_ == TransactionState::COMMITTED;
}


// --------------------------------------------------
// IsAborted
// --------------------------------------------------

bool Transaction::IsAborted() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return state_ == TransactionState::ABORTED;
}


// --------------------------------------------------
// Commit
// --------------------------------------------------

void Transaction::Commit() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != TransactionState::ACTIVE) {
        throw TransactionException(
            "Only an ACTIVE transaction can commit"
        );
    }

    state_ = TransactionState::COMMITTED;
}


// --------------------------------------------------
// Abort
// --------------------------------------------------

void Transaction::Abort() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != TransactionState::ACTIVE) {
        throw TransactionException(
            "Only an ACTIVE transaction can abort"
        );
    }

    state_ = TransactionState::ABORTED;
}


// --------------------------------------------------
// EnsureActive
// --------------------------------------------------

void Transaction::EnsureActive() const {

    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != TransactionState::ACTIVE) {
        throw TransactionException(
            "Transaction is not ACTIVE"
        );
    }
}

std::unique_lock<std::recursive_mutex>
Transaction::AcquireOperationLock() {

    return std::unique_lock<std::recursive_mutex>(
        operation_mutex_
    );
}

} // namespace minidb
