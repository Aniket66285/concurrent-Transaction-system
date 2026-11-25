
#pragma once

#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace minidb {

class Transaction;
class TxnManager;
struct LogRecord;

class TableHeap {
public:

    explicit TableHeap(BufferPool& buffer_pool);

    TableHeap(
        BufferPool& buffer_pool,
        page_id_t root_page_id
    );

    ~TableHeap() = default;

    TableHeap(const TableHeap&) = delete;
    TableHeap& operator=(const TableHeap&) = delete;


    // ==============================================
    // NON-TRANSACTIONAL OPERATIONS
    // ==============================================

    RID InsertRecord(
        const std::vector<std::uint8_t>& record
    );

    std::vector<std::uint8_t> GetRecord(
        const RID& rid
    ) const;

    bool UpdateRecord(
        const RID& rid,
        const std::vector<std::uint8_t>& record
    );

    bool DeleteRecord(const RID& rid);


    // ==============================================
    // TRANSACTIONAL OPERATIONS
    // ==============================================

    // INSERT:
    // Acquires insertion lock, writes durable WAL,
    // then applies the record modification.
    RID InsertRecord(
        Transaction& transaction,
        TxnManager& txn_manager,
        const std::vector<std::uint8_t>& record
    );

    // READ:
    // Acquires shared lock before reading.
    std::vector<std::uint8_t> GetRecord(
        Transaction& transaction,
        TxnManager& txn_manager,
        const RID& rid
    ) const;

    // UPDATE:
    // Acquires exclusive lock, logs before/after
    // images, then applies the modification.
    bool UpdateRecord(
        Transaction& transaction,
        TxnManager& txn_manager,
        const RID& rid,
        const std::vector<std::uint8_t>& record
    );

    // DELETE:
    // Acquires exclusive lock and logs the
    // before image before deleting the record.
    bool DeleteRecord(
        Transaction& transaction,
        TxnManager& txn_manager,
        const RID& rid
    );

    // Reverses one data modification WAL record.
    // Used by TxnManager::AbortTransaction().
    void UndoRecord(const LogRecord& record);
    void RedoRecord(const LogRecord& record);


    // ==============================================
    // METADATA
    // ==============================================

    page_id_t GetRootPageId() const noexcept;

    std::size_t GetDataPageCount() const;

    std::vector<RID> GetAllRIDs() const;

    bool OwnsPage(page_id_t page_id) const;


private:

    struct DirectorySnapshot {

        std::vector<page_id_t> data_page_ids;

        bool has_next{false};

        page_id_t next_page_id{0};
    };


    static constexpr std::size_t kDirectoryHeaderSize = 16;

    static constexpr std::size_t kDirectoryEntrySize = 4;

    static constexpr std::size_t kDirectoryCapacity =
        (Config::kPageSizeBytes - kDirectoryHeaderSize) /
        kDirectoryEntrySize;


    DirectorySnapshot ReadDirectoryPage(
        page_id_t directory_page_id
    ) const;

    bool ContainsDataPageLocked(
        page_id_t page_id
    ) const;

    void AppendDataPageToDirectoryLocked(
        page_id_t data_page_id
    );

    void AppendDirectoryPageLocked(
        page_id_t current_directory_page_id
    );


    BufferPool& buffer_pool_;

    page_id_t root_page_id_{0};

    mutable std::mutex mutex_;
};

} // namespace minidb
