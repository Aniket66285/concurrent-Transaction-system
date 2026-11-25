
#include "minidb/recovery/recovery_manager.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/recovery/log_manager.hpp"

#include <unordered_map>
#include <vector>

namespace minidb {

namespace {

enum class TxnStatus {
    ACTIVE,
    COMMITTED,
    ABORTED
};

struct TransactionInfo {
    TxnStatus status{TxnStatus::ACTIVE};
    lsn_t last_lsn{kInvalidLSN};
};

} // namespace


RecoveryManager::RecoveryManager(LogManager& log_manager)
    : log_manager_(log_manager) {
}


RecoveryResult RecoveryManager::Recover(
    const ApplyRecordCallback& apply_record,
    const FlushPagesCallback& flush_pages
) {

    if (!apply_record) {
        throw StorageException(
            "Recovery apply callback is missing"
        );
    }

    if (!flush_pages) {
        throw StorageException(
            "Recovery flush callback is missing"
        );
    }

    // STEP 1: Read the complete WAL.
    const std::vector<LogRecord> records =
        log_manager_.ReadAllRecords(false);

    RecoveryResult result;

    result.records_scanned = records.size();

    std::unordered_map<txn_id_t, TransactionInfo> transactions;

    std::vector<const LogRecord*> modifications;

    lsn_t previous_global_lsn = kInvalidLSN;

    // ==================================================
    // STEP 2: ANALYSIS
    // ==================================================

    for (const LogRecord& record : records) {

        if (!IsWellFormedLogRecord(record)) {
            throw StorageException(
                "Recovery found an invalid WAL record"
            );
        }

        if (record.lsn == kInvalidLSN) {
            throw StorageException(
                "Recovery found a WAL record without an LSN"
            );
        }

        if (previous_global_lsn != kInvalidLSN &&
            record.lsn <= previous_global_lsn) {

            throw StorageException(
                "WAL records are not in increasing LSN order"
            );
        }

        previous_global_lsn = record.lsn;

        // Checkpoints are system records.
        if (record.type == LogRecordType::CHECKPOINT) {

            if (record.txn_id != 0) {
                throw StorageException(
                    "Checkpoint record must use system transaction ID zero"
                );
            }

            continue;
        }

        if (record.txn_id == 0) {
            throw StorageException(
                "Transaction record uses reserved transaction ID zero"
            );
        }

        // BEGIN establishes the transaction chain.
        if (record.type == LogRecordType::BEGIN) {

            if (transactions.find(record.txn_id) !=
                transactions.end()) {

                throw StorageException(
                    "Duplicate transaction BEGIN record"
                );
            }

            if (record.prev_lsn != kInvalidLSN) {
                throw StorageException(
                    "First transaction record has an invalid PrevLSN"
                );
            }

            TransactionInfo info;

            info.status = TxnStatus::ACTIVE;
            info.last_lsn = record.lsn;

            transactions.emplace(record.txn_id, info);

            continue;
        }

        auto transaction_iterator =
            transactions.find(record.txn_id);

        if (transaction_iterator == transactions.end()) {
            throw StorageException(
                "WAL record exists without a preceding BEGIN"
            );
        }

        TransactionInfo& transaction =
            transaction_iterator->second;

        if (transaction.status != TxnStatus::ACTIVE) {
            throw StorageException(
                "WAL record found after transaction completion"
            );
        }

        // Validate transaction's previous LSN chain.
        if (record.prev_lsn != transaction.last_lsn) {
            throw StorageException(
                "Transaction WAL PrevLSN chain is corrupted"
            );
        }

        transaction.last_lsn = record.lsn;

        if (IsDataModificationRecord(record.type)) {

            modifications.push_back(&record);

            continue;
        }

        if (record.type == LogRecordType::COMMIT) {

            transaction.status = TxnStatus::COMMITTED;

            continue;
        }

        if (record.type == LogRecordType::ABORT) {

            transaction.status = TxnStatus::ABORTED;

            continue;
        }

        throw StorageException(
            "Unexpected WAL record type during analysis"
        );
    }


    // Count transaction outcomes.
    for (const auto& entry : transactions) {

        switch (entry.second.status) {

            case TxnStatus::COMMITTED:
                ++result.committed_transactions;
                break;

            case TxnStatus::ABORTED:
                ++result.aborted_transactions;
                break;

            case TxnStatus::ACTIVE:
                ++result.incomplete_transactions;
                break;
        }
    }


    // ==================================================
    // STEP 3: REDO
    // ==================================================
    //
    // Redo modifications of committed and incomplete
    // transactions in WAL order.
    //
    // Incomplete transactions may have written dirty
    // pages to disk before crashing.
    //
    // Completed ABORT transactions are skipped because
    // TxnManager flushes their restored pages before
    // writing the durable ABORT record.

    for (const LogRecord* record : modifications) {

        const auto iterator =
            transactions.find(record->txn_id);

        if (iterator == transactions.end()) {
            throw StorageException(
                "Missing transaction during redo"
            );
        }

        if (iterator->second.status == TxnStatus::ABORTED) {
            continue;
        }

        apply_record(*record, RecoveryAction::REDO);

        ++result.records_redone;
    }


    // ==================================================
    // STEP 4: UNDO
    // ==================================================
    //
    // Incomplete transactions are rolled back in
    // reverse WAL order.

    for (auto iterator = modifications.rbegin();
         iterator != modifications.rend();
         ++iterator) {

        const LogRecord& record = **iterator;

        const auto transaction_iterator =
            transactions.find(record.txn_id);

        if (transaction_iterator == transactions.end()) {
            throw StorageException(
                "Missing transaction during undo"
            );
        }

        if (transaction_iterator->second.status !=
            TxnStatus::ACTIVE) {

            continue;
        }

        apply_record(record, RecoveryAction::UNDO);

        ++result.records_undone;
    }


    // ==================================================
    // STEP 5: FLUSH
    // ==================================================

    flush_pages();

    return result;
}

} // namespace minidb
