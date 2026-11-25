
#pragma once

#include "minidb/concurrency/transaction.hpp"
#include "minidb/recovery/log_record.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace minidb {

class LogManager;
class LockManager;

class TxnManager {
public:

    using UndoCallback =
        std::function<void(const LogRecord&)>;

    using FlushPagesCallback =
        std::function<void()>;

    explicit TxnManager(
        LogManager& log_manager,
        LockManager* lock_manager = nullptr
    );

    TxnManager(const TxnManager&) = delete;
    TxnManager& operator=(const TxnManager&) = delete;

    std::shared_ptr<Transaction> BeginTransaction();

    bool LockShared(
        Transaction& transaction,
        const RID& rid
    );

    bool LockExclusive(
        Transaction& transaction,
        const RID& rid
    );

    lsn_t AppendLogRecord(
        Transaction& transaction,
        LogRecord record
    );

    void CommitTransaction(Transaction& transaction);

    void AbortTransaction(
        Transaction& transaction,
        const UndoCallback& undo_record,
        const FlushPagesCallback& flush_pages
    );

    std::shared_ptr<Transaction> GetActiveTransaction(
        txn_id_t transaction_id
    ) const;

    std::size_t GetActiveTransactionCount() const;

private:

    // Must be called while mutex_ is locked.
    void EnsureManagedActive(
        const Transaction& transaction
    ) const;

    LogManager& log_manager_;

    LockManager* lock_manager_{nullptr};

    mutable std::mutex mutex_;

    txn_id_t next_txn_id_{1};

    std::unordered_map<
        txn_id_t,
        std::shared_ptr<Transaction>
    > active_transactions_;

    // Transactions currently undergoing rollback.
    // Their new operations must be rejected.
    std::unordered_set<txn_id_t> aborting_transactions_;
};

} // namespace minidb
