
#include "minidb/concurrency/history_recorder.hpp"

#include "minidb/common/exception.hpp"

#include <limits>
#include <utility>

namespace minidb {


// --------------------------------------------------
// EnsureActiveLocked
// --------------------------------------------------

void HistoryRecorder::EnsureActiveLocked(
    txn_id_t txn_id
) const {

    const auto iterator =
        transaction_status_.find(txn_id);

    if (iterator == transaction_status_.end()) {
        throw TransactionException(
            "Transaction does not exist in history"
        );
    }

    if (iterator->second != TransactionStatus::ACTIVE) {
        throw TransactionException(
            "Cannot record operations for a completed transaction"
        );
    }
}


// --------------------------------------------------
// AppendEventLocked
// --------------------------------------------------

std::uint64_t HistoryRecorder::AppendEventLocked(
    txn_id_t txn_id,
    HistoryOperation operation,
    const RID& rid
) {

    if (next_sequence_number_ ==
        std::numeric_limits<std::uint64_t>::max()) {

        throw TransactionException(
            "History sequence number limit reached"
        );
    }

    const std::uint64_t sequence =
        next_sequence_number_;

    HistoryEvent event;

    event.sequence_number = sequence;
    event.txn_id = txn_id;
    event.operation = operation;
    event.rid = rid;

    history_.push_back(event);

    ++next_sequence_number_;

    return sequence;
}


// --------------------------------------------------
// RecordBegin
// --------------------------------------------------

std::uint64_t HistoryRecorder::RecordBegin(
    txn_id_t txn_id
) {

    if (txn_id == 0) {
        throw TransactionException(
            "Transaction ID zero is reserved"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (transaction_status_.find(txn_id) !=
        transaction_status_.end()) {

        throw TransactionException(
            "Transaction already exists in history"
        );
    }

    const std::uint64_t sequence =
        AppendEventLocked(
            txn_id,
            HistoryOperation::BEGIN
        );

    transaction_status_.emplace(
        txn_id,
        TransactionStatus::ACTIVE
    );

    return sequence;
}


// --------------------------------------------------
// RecordRead
// --------------------------------------------------

std::uint64_t HistoryRecorder::RecordRead(
    txn_id_t txn_id,
    const RID& rid
) {

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureActiveLocked(txn_id);

    return AppendEventLocked(
        txn_id,
        HistoryOperation::READ,
        rid
    );
}


// --------------------------------------------------
// RecordWrite
// --------------------------------------------------

std::uint64_t HistoryRecorder::RecordWrite(
    txn_id_t txn_id,
    const RID& rid
) {

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureActiveLocked(txn_id);

    return AppendEventLocked(
        txn_id,
        HistoryOperation::WRITE,
        rid
    );
}


// --------------------------------------------------
// RecordCommit
// --------------------------------------------------

std::uint64_t HistoryRecorder::RecordCommit(
    txn_id_t txn_id
) {

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureActiveLocked(txn_id);

    const std::uint64_t sequence =
        AppendEventLocked(
            txn_id,
            HistoryOperation::COMMIT
        );

    transaction_status_[txn_id] =
        TransactionStatus::COMMITTED;

    return sequence;
}


// --------------------------------------------------
// RecordAbort
// --------------------------------------------------

std::uint64_t HistoryRecorder::RecordAbort(
    txn_id_t txn_id
) {

    std::lock_guard<std::mutex> lock(mutex_);

    EnsureActiveLocked(txn_id);

    const std::uint64_t sequence =
        AppendEventLocked(
            txn_id,
            HistoryOperation::ABORT
        );

    transaction_status_[txn_id] =
        TransactionStatus::ABORTED;

    return sequence;
}


// --------------------------------------------------
// GetHistory
// --------------------------------------------------

std::vector<HistoryEvent> HistoryRecorder::GetHistory() const {

    std::lock_guard<std::mutex> lock(mutex_);

    // Return a snapshot so the caller cannot
    // modify the recorder's internal history.
    return history_;
}


// --------------------------------------------------
// GetTransactionHistory
// --------------------------------------------------

std::vector<HistoryEvent>
HistoryRecorder::GetTransactionHistory(
    txn_id_t txn_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<HistoryEvent> result;

    for (const HistoryEvent& event : history_) {

        if (event.txn_id == txn_id) {
            result.push_back(event);
        }
    }

    return result;
}


// --------------------------------------------------
// GetEventCount
// --------------------------------------------------

std::size_t HistoryRecorder::GetEventCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return history_.size();
}


// --------------------------------------------------
// IsTransactionActive
// --------------------------------------------------

bool HistoryRecorder::IsTransactionActive(
    txn_id_t txn_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        transaction_status_.find(txn_id);

    return iterator != transaction_status_.end() &&
           iterator->second == TransactionStatus::ACTIVE;
}


// --------------------------------------------------
// Clear
// --------------------------------------------------

void HistoryRecorder::Clear() {

    std::lock_guard<std::mutex> lock(mutex_);

    history_.clear();

    transaction_status_.clear();

    next_sequence_number_ = 1;
}


// --------------------------------------------------
// HistoryOperationToString
// --------------------------------------------------

const char* HistoryOperationToString(
    HistoryOperation operation
) noexcept {

    switch (operation) {

        case HistoryOperation::BEGIN:
            return "BEGIN";

        case HistoryOperation::READ:
            return "READ";

        case HistoryOperation::WRITE:
            return "WRITE";

        case HistoryOperation::COMMIT:
            return "COMMIT";

        case HistoryOperation::ABORT:
            return "ABORT";

        default:
            return "UNKNOWN";
    }
}


} // namespace minidb
