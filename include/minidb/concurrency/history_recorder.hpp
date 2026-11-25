
#pragma once

#include "minidb/common/types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace minidb {

// Types of events recorded in transaction history.
enum class HistoryOperation {
    BEGIN,
    READ,
    WRITE,
    COMMIT,
    ABORT
};


// One event in the global execution history.
struct HistoryEvent {

    // Globally increasing event sequence number.
    std::uint64_t sequence_number{0};

    // Transaction responsible for this operation.
    txn_id_t txn_id{0};

    // Type of operation.
    HistoryOperation operation{HistoryOperation::BEGIN};

    // Database record accessed by READ or WRITE.
    RID rid{};
};


// Thread-safe transaction history recorder.
class HistoryRecorder {
public:

    HistoryRecorder() = default;

    HistoryRecorder(const HistoryRecorder&) = delete;

    HistoryRecorder& operator=(
        const HistoryRecorder&
    ) = delete;

    // Records the beginning of a transaction.
    std::uint64_t RecordBegin(txn_id_t txn_id);

    // Records a read operation.
    std::uint64_t RecordRead(
        txn_id_t txn_id,
        const RID& rid
    );

    // Records a write operation.
    std::uint64_t RecordWrite(
        txn_id_t txn_id,
        const RID& rid
    );

    // Records transaction completion.
    std::uint64_t RecordCommit(txn_id_t txn_id);

    // Records transaction abortion.
    std::uint64_t RecordAbort(txn_id_t txn_id);

    // Returns the complete ordered execution history.
    std::vector<HistoryEvent> GetHistory() const;

    // Returns events belonging to one transaction.
    std::vector<HistoryEvent> GetTransactionHistory(
        txn_id_t txn_id
    ) const;

    // Returns the total number of events.
    std::size_t GetEventCount() const;

    // Checks whether a transaction is currently ACTIVE.
    bool IsTransactionActive(txn_id_t txn_id) const;

    // Clears all history and resets the sequence counter.
    void Clear();

private:

    enum class TransactionStatus {
        ACTIVE,
        COMMITTED,
        ABORTED
    };

    // Must be called while mutex_ is locked.
    void EnsureActiveLocked(txn_id_t txn_id) const;

    // Must be called while mutex_ is locked.
    std::uint64_t AppendEventLocked(
        txn_id_t txn_id,
        HistoryOperation operation,
        const RID& rid = RID{}
    );

    mutable std::mutex mutex_;

    // Global execution order.
    std::vector<HistoryEvent> history_;

    // Tracks the lifecycle of every recorded transaction.
    std::map<txn_id_t, TransactionStatus> transaction_status_;

    // Sequence number of the next event.
    std::uint64_t next_sequence_number_{1};
};


// Converts an operation type into a readable string.
const char* HistoryOperationToString(
    HistoryOperation operation
) noexcept;

} // namespace minidb
