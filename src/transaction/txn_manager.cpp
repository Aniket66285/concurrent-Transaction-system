
#include "minidb/transaction/txn_manager.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/recovery/log_manager.hpp"

#include <limits>
#include <vector>

namespace minidb {

// --------------------------------------------------
// Constructor
// --------------------------------------------------

TxnManager::TxnManager(
    LogManager& log_manager,
    LockManager* lock_manager
)
    : log_manager_(log_manager),
      lock_manager_(lock_manager) {

    const std::vector<LogRecord> records =
        log_manager_.ReadAllRecords(true);

    txn_id_t maximum_transaction_id = 0;

    for (const LogRecord& record : records) {

        if (record.txn_id > maximum_transaction_id) {
            maximum_transaction_id = record.txn_id;
        }
    }

    if (maximum_transaction_id ==
        std::numeric_limits<txn_id_t>::max()) {

        throw TransactionException(
            "No transaction IDs are available"
        );
    }

    next_txn_id_ = maximum_transaction_id + 1;

    if (next_txn_id_ == 0) {
        throw TransactionException(
            "Invalid next transaction ID"
        );
    }
}


// --------------------------------------------------
// EnsureManagedActive
// --------------------------------------------------

void TxnManager::EnsureManagedActive(
    const Transaction& transaction
) const {

    const txn_id_t transaction_id = transaction.GetId();

    const auto iterator =
        active_transactions_.find(transaction_id);

    if (iterator == active_transactions_.end() ||
        iterator->second.get() != &transaction) {

        throw TransactionException(
            "Transaction is not managed by this TxnManager"
        );
    }

    if (aborting_transactions_.count(transaction_id) != 0) {

        throw TransactionException(
            "Transaction is currently being aborted"
        );
    }

    transaction.EnsureActive();
}


// --------------------------------------------------
// BeginTransaction
// --------------------------------------------------

std::shared_ptr<Transaction>
TxnManager::BeginTransaction() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (next_txn_id_ == 0 ||
        next_txn_id_ ==
            std::numeric_limits<txn_id_t>::max()) {

        throw TransactionException(
            "Transaction ID limit reached"
        );
    }

    const txn_id_t transaction_id = next_txn_id_++;

    auto transaction =
        std::make_shared<Transaction>(transaction_id);

    LogRecord begin_record;

    begin_record.txn_id = transaction_id;
    begin_record.prev_lsn = kInvalidLSN;
    begin_record.type = LogRecordType::BEGIN;

    const lsn_t begin_lsn =
        log_manager_.AppendLogRecord(begin_record);

    transaction->SetLastLSN(begin_lsn);

    active_transactions_.emplace(
        transaction_id,
        transaction
    );

    return transaction;
}


// --------------------------------------------------
// LockShared
// --------------------------------------------------

bool TxnManager::LockShared(
    Transaction& transaction,
    const RID& rid
) {
    auto operation_lock = transaction.AcquireOperationLock();

    if (lock_manager_ == nullptr) {
        throw TransactionException(
            "LockManager is not connected to TxnManager"
        );
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        EnsureManagedActive(transaction);
    }

    const bool acquired =
        lock_manager_->LockShared(transaction, rid);

    // Check again because the transaction might have
    // entered ABORT while waiting for its lock.
    bool still_active = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto iterator =
            active_transactions_.find(transaction.GetId());

        still_active =
            iterator != active_transactions_.end() &&
            iterator->second.get() == &transaction &&
            aborting_transactions_.count(
                transaction.GetId()
            ) == 0 &&
            transaction.IsActive();
    }

    if (!still_active) {

        // If rollback has already completed, clean up
        // any lock acquired by this previously waiting call.
        if (!transaction.IsActive()) {
            lock_manager_->ReleaseAll(transaction);
        }

        throw TransactionException(
            "Transaction became inactive while acquiring shared lock"
        );
    }

    return acquired;
}


// --------------------------------------------------
// LockExclusive
// --------------------------------------------------

bool TxnManager::LockExclusive(
    Transaction& transaction,
    const RID& rid
) {
    auto operation_lock = transaction.AcquireOperationLock();

    if (lock_manager_ == nullptr) {
        throw TransactionException(
            "LockManager is not connected to TxnManager"
        );
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        EnsureManagedActive(transaction);
    }

    const bool acquired =
        lock_manager_->LockExclusive(transaction, rid);

    bool still_active = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto iterator =
            active_transactions_.find(transaction.GetId());

        still_active =
            iterator != active_transactions_.end() &&
            iterator->second.get() == &transaction &&
            aborting_transactions_.count(
                transaction.GetId()
            ) == 0 &&
            transaction.IsActive();
    }

    if (!still_active) {

        if (!transaction.IsActive()) {
            lock_manager_->ReleaseAll(transaction);
        }

        throw TransactionException(
            "Transaction became inactive while acquiring exclusive lock"
        );
    }

    return acquired;
}


// --------------------------------------------------
// AppendLogRecord
// --------------------------------------------------

lsn_t TxnManager::AppendLogRecord(
    Transaction& transaction,
    LogRecord record
) {
    auto operation_lock = transaction.AcquireOperationLock();

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureManagedActive(transaction);

    if (!IsDataModificationRecord(record.type)) {

        throw TransactionException(
            "Only INSERT, UPDATE and DELETE records "
            "can be appended through this method"
        );
    }

    record.txn_id = transaction.GetId();

    record.prev_lsn = transaction.GetLastLSN();

    record.lsn = kInvalidLSN;

    const lsn_t record_lsn =
        log_manager_.AppendLogRecord(record);

    transaction.SetLastLSN(record_lsn);

    // WAL-before-data:
    // The page modification must happen only after
    // this record becomes durable.
    log_manager_.FlushUpTo(record_lsn);

    return record_lsn;
}


// --------------------------------------------------
// CommitTransaction
// --------------------------------------------------

void TxnManager::CommitTransaction(
    Transaction& transaction
) {
    auto operation_lock = transaction.AcquireOperationLock();

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureManagedActive(transaction);

    LogRecord commit_record;

    commit_record.txn_id = transaction.GetId();

    commit_record.prev_lsn = transaction.GetLastLSN();

    commit_record.type = LogRecordType::COMMIT;

    const lsn_t commit_lsn =
        log_manager_.AppendLogRecord(commit_record);

    transaction.SetLastLSN(commit_lsn);

    // Commit succeeds only after durable WAL flush.
    log_manager_.FlushUpTo(commit_lsn);

    transaction.Commit();

    // Strict 2PL locks are released after COMMIT.
    if (lock_manager_ != nullptr) {
        lock_manager_->ReleaseAll(transaction);
    }

    active_transactions_.erase(transaction.GetId());
}


// --------------------------------------------------
// AbortTransaction
// --------------------------------------------------

void TxnManager::AbortTransaction(
    Transaction& transaction,
    const UndoCallback& undo_record,
    const FlushPagesCallback& flush_pages
) {
    auto operation_lock = transaction.AcquireOperationLock();

    const txn_id_t transaction_id = transaction.GetId();

    // STEP 1: Validate and mark transaction as aborting.
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto iterator =
            active_transactions_.find(transaction_id);

        if (iterator == active_transactions_.end() ||
            iterator->second.get() != &transaction) {

            throw TransactionException(
                "Transaction is not managed by this TxnManager"
            );
        }

        transaction.EnsureActive();

        if (!undo_record) {
            throw TransactionException(
                "Undo callback is not provided"
            );
        }

        if (!flush_pages) {
            throw TransactionException(
                "Page flush callback is not provided"
            );
        }

        if (!aborting_transactions_.insert(
                transaction_id
            ).second) {

            throw TransactionException(
                "Transaction is already being aborted"
            );
        }
    }

    // IMPORTANT:
    // From here onward, the global TxnManager mutex
    // is NOT held while executing callbacks.
    //
    // New operations are blocked by aborting_transactions_.

    try {

        // STEP 2: Make existing transaction WAL durable.
        log_manager_.Flush();

        // STEP 3: Read WAL history.
        const std::vector<LogRecord> records =
            log_manager_.ReadAllRecords(false);

        // STEP 4: Undo modifications in reverse WAL order.
        for (auto iterator = records.rbegin();
             iterator != records.rend();
             ++iterator) {

            const LogRecord& record = *iterator;

            if (record.txn_id != transaction_id) {
                continue;
            }

            if (!IsDataModificationRecord(record.type)) {
                continue;
            }

            undo_record(record);
        }

        // STEP 5: Flush restored database pages.
        flush_pages();
    }
    catch (...) {

        // Rollback failed before ABORT was recorded.
        // Remove the temporary fence so the caller can
        // handle the failure and retry the abort.
        std::lock_guard<std::mutex> lock(mutex_);

        aborting_transactions_.erase(transaction_id);

        throw;
    }

    // STEP 6: Write and persist ABORT.
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto iterator =
            active_transactions_.find(transaction_id);

        if (iterator == active_transactions_.end() ||
            iterator->second.get() != &transaction ||
            !transaction.IsActive()) {

            throw TransactionException(
                "Transaction became invalid during rollback"
            );
        }

        LogRecord abort_record;

        abort_record.txn_id = transaction_id;

        abort_record.prev_lsn = transaction.GetLastLSN();

        abort_record.type = LogRecordType::ABORT;

        const lsn_t abort_lsn =
            log_manager_.AppendLogRecord(abort_record);

        transaction.SetLastLSN(abort_lsn);

        log_manager_.FlushUpTo(abort_lsn);

        // STEP 7: Mark transaction as aborted.
        transaction.Abort();

        // STEP 8: Release Strict 2PL locks.
        if (lock_manager_ != nullptr) {
            lock_manager_->ReleaseAll(transaction);
        }

        active_transactions_.erase(transaction_id);

        aborting_transactions_.erase(transaction_id);
    }
}


// --------------------------------------------------
// GetActiveTransaction
// --------------------------------------------------

std::shared_ptr<Transaction>
TxnManager::GetActiveTransaction(
    txn_id_t transaction_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        active_transactions_.find(transaction_id);

    if (iterator == active_transactions_.end()) {
        return nullptr;
    }

    return iterator->second;
}


// --------------------------------------------------
// GetActiveTransactionCount
// --------------------------------------------------

std::size_t TxnManager::GetActiveTransactionCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return active_transactions_.size();
}

} // namespace minidb
