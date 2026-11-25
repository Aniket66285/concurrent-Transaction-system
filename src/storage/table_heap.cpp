
#include "minidb/storage/table_heap.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/storage/slotted_page.hpp"
#include "minidb/concurrency/transaction.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/recovery/log_record.hpp"

#include <array>
#include <unordered_set>

namespace minidb {

namespace {

constexpr std::array<std::uint8_t, 4> kDirectoryMagic{
    {'T', 'H', 'D', 'R'}
};

constexpr std::uint8_t kDirectoryVersion = 1;
constexpr std::uint8_t kHasNextDirectoryPage = 0x01;

constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kFlagsOffset = 5;
constexpr std::size_t kEntryCountOffset = 6;
constexpr std::size_t kNextPageOffset = 8;
constexpr std::size_t kRootPageOffset = 12;
constexpr std::size_t kEntriesOffset = 16;

constexpr std::size_t kDirectoryCapacity =
    (Config::kPageSizeBytes - kEntriesOffset) / 4;


// Little-endian helpers.

void WriteUint16(std::uint8_t* destination, std::uint16_t value) {

    destination[0] = static_cast<std::uint8_t>(value & 0xFF);
    destination[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

std::uint16_t ReadUint16(const std::uint8_t* source) {

    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) |
        (static_cast<std::uint16_t>(source[1]) << 8)
    );
}

void WriteUint32(std::uint8_t* destination, std::uint32_t value) {

    for (std::size_t i = 0; i < 4; ++i) {

        destination[i] = static_cast<std::uint8_t>(
            (value >> (8 * i)) & 0xFF
        );
    }
}

std::uint32_t ReadUint32(const std::uint8_t* source) {

    std::uint32_t value = 0;

    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(source[i]) << (8 * i);
    }

    return value;
}


// Initialize a directory page.

void InitializeDirectoryPage(
    DiskManager::PageData& page,
    page_id_t root_page_id
) {

    page.fill(0);

    for (std::size_t i = 0; i < kDirectoryMagic.size(); ++i) {
        page[i] = kDirectoryMagic[i];
    }

    page[kVersionOffset] = kDirectoryVersion;
    page[kFlagsOffset] = 0;

    WriteUint16(page.data() + kEntryCountOffset, 0);
    WriteUint32(page.data() + kNextPageOffset, 0);
    WriteUint32(page.data() + kRootPageOffset, root_page_id);
}


// Validate directory metadata.

void ValidateDirectoryPage(
    const DiskManager::PageData& page,
    page_id_t expected_root_page_id
) {

    for (std::size_t i = 0; i < kDirectoryMagic.size(); ++i) {

        if (page[i] != kDirectoryMagic[i]) {
            throw StorageException(
                "Invalid TableHeap directory signature"
            );
        }
    }

    if (page[kVersionOffset] != kDirectoryVersion) {
        throw StorageException(
            "Unsupported TableHeap directory version"
        );
    }

    if ((page[kFlagsOffset] & ~kHasNextDirectoryPage) != 0) {
        throw StorageException(
            "Invalid TableHeap directory flags"
        );
    }

    const std::uint16_t count =
        ReadUint16(page.data() + kEntryCountOffset);

    const page_id_t stored_root =
        ReadUint32(page.data() + kRootPageOffset);

    if (stored_root != expected_root_page_id) {
        throw StorageException(
            "Directory page belongs to another TableHeap"
        );
    }

    if (count > kDirectoryCapacity) {
        throw StorageException(
            "Invalid TableHeap directory entry count"
        );
    }
}


// Read/write data page IDs.

std::size_t DirectoryEntryOffset(std::size_t index) {
    return kEntriesOffset + index * 4;
}

page_id_t ReadDirectoryEntry(
    const DiskManager::PageData& page,
    std::size_t index
) {

    return ReadUint32(
        page.data() + DirectoryEntryOffset(index)
    );
}

void WriteDirectoryEntry(
    DiskManager::PageData& page,
    std::size_t index,
    page_id_t page_id
) {

    WriteUint32(
        page.data() + DirectoryEntryOffset(index),
        page_id
    );
}

void AddDirectoryEntry(
    DiskManager::PageData& page,
    page_id_t data_page_id
) {

    const std::uint16_t count =
        ReadUint16(page.data() + kEntryCountOffset);

    if (count >= kDirectoryCapacity) {
        throw StorageException(
            "TableHeap directory page is full"
        );
    }

    WriteDirectoryEntry(page, count, data_page_id);

    WriteUint16(
        page.data() + kEntryCountOffset,
        static_cast<std::uint16_t>(count + 1)
    );
}

} // namespace


// Create a new heap.

TableHeap::TableHeap(BufferPool& buffer_pool)
    : buffer_pool_(buffer_pool) {

    PageGuard root_guard =
        buffer_pool_.NewPage(&root_page_id_);

    if (!root_guard.IsValid()) {
        throw StorageException(
            "Unable to allocate TableHeap root page"
        );
    }

    InitializeDirectoryPage(
        root_guard.GetPageData(),
        root_page_id_
    );

    root_guard.Drop();

    if (!buffer_pool_.FlushPage(root_page_id_)) {
        throw StorageException(
            "Unable to persist TableHeap root page"
        );
    }
}


// Open an existing heap.

TableHeap::TableHeap(
    BufferPool& buffer_pool,
    page_id_t root_page_id
)
    : buffer_pool_(buffer_pool),
      root_page_id_(root_page_id) {

    ReadDirectoryPage(root_page_id_);
}


// Read and validate one directory page.

TableHeap::DirectorySnapshot TableHeap::ReadDirectoryPage(
    page_id_t directory_page_id
) const {

    PageGuard guard =
        buffer_pool_.FetchPage(directory_page_id);

    if (!guard.IsValid()) {
        throw StorageException(
            "Unable to fetch TableHeap directory page"
        );
    }

    const PageGuard& read_guard = guard;

    const DiskManager::PageData& page =
        read_guard.GetPageData();

    ValidateDirectoryPage(page, root_page_id_);

    DirectorySnapshot snapshot;

    const std::uint16_t count =
        ReadUint16(page.data() + kEntryCountOffset);

    snapshot.data_page_ids.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        snapshot.data_page_ids.push_back(
            ReadDirectoryEntry(page, i)
        );
    }

    snapshot.has_next =
        (page[kFlagsOffset] & kHasNextDirectoryPage) != 0;

    if (snapshot.has_next) {

        snapshot.next_page_id =
            ReadUint32(page.data() + kNextPageOffset);

        if (snapshot.next_page_id == directory_page_id) {
            throw StorageException(
                "TableHeap directory contains a self-reference"
            );
        }
    }

    return snapshot;
}


// Check if a data page belongs to this heap.

bool TableHeap::ContainsDataPageLocked(page_id_t page_id) const {

    std::unordered_set<page_id_t> visited;

    page_id_t current = root_page_id_;

    while (true) {

        if (!visited.insert(current).second) {
            throw StorageException(
                "Cycle detected in TableHeap directory"
            );
        }

        const DirectorySnapshot snapshot =
            ReadDirectoryPage(current);

        for (page_id_t stored_page_id : snapshot.data_page_ids) {

            if (stored_page_id == page_id) {
                return true;
            }
        }

        if (!snapshot.has_next) {
            return false;
        }

        current = snapshot.next_page_id;
    }
}


// Create another directory page when the current one is full.

void TableHeap::AppendDirectoryPageLocked(
    page_id_t current_directory_page_id
) {

    // Keep the previous page ID unchanged.
    const page_id_t previous_directory_page_id =
        current_directory_page_id;

    page_id_t new_directory_page_id = 0;

    PageGuard new_guard =
        buffer_pool_.NewPage(&new_directory_page_id);

    if (!new_guard.IsValid()) {
        throw StorageException(
            "Unable to allocate another TableHeap directory page"
        );
    }

    InitializeDirectoryPage(
        new_guard.GetPageData(),
        root_page_id_
    );

    new_guard.Drop();

    if (!buffer_pool_.FlushPage(new_directory_page_id)) {
        throw StorageException(
            "Unable to persist new directory page"
        );
    }

    // Link the new page from the previous last page.
    PageGuard previous_guard =
        buffer_pool_.FetchPage(previous_directory_page_id);

    if (!previous_guard.IsValid()) {
        throw StorageException(
            "Unable to fetch previous directory page"
        );
    }

    auto& previous_page = previous_guard.GetPageData();

    ValidateDirectoryPage(previous_page, root_page_id_);

    if ((previous_page[kFlagsOffset] &
         kHasNextDirectoryPage) != 0) {

        throw StorageException(
            "Directory page already has a successor"
        );
    }

    WriteUint32(
        previous_page.data() + kNextPageOffset,
        new_directory_page_id
    );

    previous_page[kFlagsOffset] |= kHasNextDirectoryPage;

    previous_guard.Drop();

    if (!buffer_pool_.FlushPage(previous_directory_page_id)) {
        throw StorageException(
            "Unable to persist directory page link"
        );
    }
}


// Register a data page in the directory chain.

void TableHeap::AppendDataPageToDirectoryLocked(
    page_id_t data_page_id
) {

    if (ContainsDataPageLocked(data_page_id)) {
        throw StorageException(
            "Data page is already registered"
        );
    }

    std::unordered_set<page_id_t> visited;

    page_id_t current = root_page_id_;

    while (true) {

        if (!visited.insert(current).second) {
            throw StorageException(
                "Cycle detected while extending directory"
            );
        }

        const DirectorySnapshot snapshot =
            ReadDirectoryPage(current);

        // This directory page has room.
        if (snapshot.data_page_ids.size() < kDirectoryCapacity) {

            PageGuard guard =
                buffer_pool_.FetchPage(current);

            if (!guard.IsValid()) {
                throw StorageException(
                    "Unable to fetch directory page"
                );
            }

            auto& page = guard.GetPageData();

            ValidateDirectoryPage(page, root_page_id_);

            AddDirectoryEntry(page, data_page_id);

            guard.Drop();

            if (!buffer_pool_.FlushPage(current)) {
                throw StorageException(
                    "Unable to persist directory entry"
                );
            }

            return;
        }

        if (snapshot.has_next) {
            current = snapshot.next_page_id;
            continue;
        }

        // Current directory page is full.
        AppendDirectoryPageLocked(current);

        // Read the newly installed link and continue there.
        const DirectorySnapshot linked =
            ReadDirectoryPage(current);

        if (!linked.has_next) {
            throw StorageException(
                "New directory page link was not persisted"
            );
        }

        current = linked.next_page_id;
    }
}


// Insert a record.

RID TableHeap::InsertRecord(
    const std::vector<std::uint8_t>& record
) {
    

    if (record.empty()) {
        throw StorageException(
            "Cannot insert an empty record"
        );
    }

    constexpr std::size_t kMaximumRecordSize =
        Config::kPageSizeBytes -
        SlottedPage::kHeaderSize -
        SlottedPage::kSlotEntrySize;

    if (record.size() > kMaximumRecordSize) {
        throw StorageException(
            "Record is too large for an empty SlottedPage"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Search existing data pages first.
    std::unordered_set<page_id_t> visited;

    page_id_t current_directory = root_page_id_;

    while (true) {

        if (!visited.insert(current_directory).second) {
            throw StorageException(
                "Cycle detected in TableHeap directory"
            );
        }

        const DirectorySnapshot snapshot =
            ReadDirectoryPage(current_directory);

        for (page_id_t data_page_id : snapshot.data_page_ids) {

            PageGuard guard =
                buffer_pool_.FetchPage(data_page_id);

            if (!guard.IsValid()) {
                throw StorageException(
                    "Unable to fetch TableHeap data page"
                );
            }

            // Try insertion on a local copy first.
            // Failed attempts will not mark the resident page dirty.
            const PageGuard& read_guard = guard;

            DiskManager::PageData page_copy =
                read_guard.GetPageData();

            SlottedPage candidate(page_copy);

            if (candidate.GetPageId() != data_page_id) {
                throw StorageException(
                    "SlottedPage ID does not match directory entry"
                );
            }

            const auto slot_id =
                candidate.InsertRecord(record);

            if (slot_id.has_value()) {

                // Copy the modified page back into the buffer.
                guard.GetPageData() = page_copy;

                return RID{
                    data_page_id,
                    *slot_id
                };
            }
        }

        if (!snapshot.has_next) {
            break;
        }

        current_directory = snapshot.next_page_id;
    }

    // No existing data page can fit the record.
    // Allocate a new page.

    page_id_t new_data_page_id = 0;

    PageGuard new_guard =
        buffer_pool_.NewPage(&new_data_page_id);

    if (!new_guard.IsValid()) {
        throw StorageException(
            "Unable to allocate a new TableHeap data page"
        );
    }

    auto& new_page_data = new_guard.GetPageData();

    SlottedPage::Initialize(
        new_page_data,
        new_data_page_id
    );

    SlottedPage new_page(new_page_data);

    const auto slot_id =
        new_page.InsertRecord(record);

    if (!slot_id.has_value()) {
        throw StorageException(
            "Record could not fit into newly initialized page"
        );
    }

    new_guard.Drop();

    // Data page first, directory metadata second.
    if (!buffer_pool_.FlushPage(new_data_page_id)) {
        throw StorageException(
            "Unable to persist newly created data page"
        );
    }

    AppendDataPageToDirectoryLocked(new_data_page_id);

    return RID{
        new_data_page_id,
        *slot_id
    };
}


// Read a record.

std::vector<std::uint8_t> TableHeap::GetRecord(
    const RID& rid
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(rid.page_id)) {
        throw StorageException(
            "RID references a page outside this TableHeap"
        );
    }

    PageGuard guard =
        buffer_pool_.FetchPage(rid.page_id);

    if (!guard.IsValid()) {
        throw StorageException(
            "Unable to fetch record page"
        );
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage page(page_copy);

    if (page.GetPageId() != rid.page_id) {
        throw StorageException(
            "RID page ID does not match SlottedPage ID"
        );
    }

    return page.GetRecord(rid.slot_id);
}


// Update a record.

bool TableHeap::UpdateRecord(
    const RID& rid,
    const std::vector<std::uint8_t>& record
) {

    if (record.empty()) {
        throw StorageException(
            "Cannot update a record with empty data"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(rid.page_id)) {
        return false;
    }

    PageGuard guard =
        buffer_pool_.FetchPage(rid.page_id);

    if (!guard.IsValid()) {
        return false;
    }

    auto& page_data = guard.GetPageData();

    SlottedPage page(page_data);

    if (page.GetPageId() != rid.page_id) {
        throw StorageException(
            "RID page ID does not match SlottedPage ID"
        );
    }

    if (!page.IsSlotOccupied(rid.slot_id)) {
        return false;
    }

    return page.UpdateRecord(rid.slot_id, record);
}


// Delete a record.

bool TableHeap::DeleteRecord(const RID& rid) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(rid.page_id)) {
        return false;
    }

    PageGuard guard =
        buffer_pool_.FetchPage(rid.page_id);

    if (!guard.IsValid()) {
        return false;
    }

    auto& page_data = guard.GetPageData();

    SlottedPage page(page_data);

    if (page.GetPageId() != rid.page_id) {
        throw StorageException(
            "RID page ID does not match SlottedPage ID"
        );
    }

    return page.DeleteRecord(rid.slot_id);
}


// Root directory ID.

page_id_t TableHeap::GetRootPageId() const noexcept {
    return root_page_id_;
}


// Number of data pages.

std::size_t TableHeap::GetDataPageCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_set<page_id_t> visited;

    std::size_t count = 0;

    page_id_t current = root_page_id_;

    while (true) {

        if (!visited.insert(current).second) {
            throw StorageException(
                "Cycle detected while counting TableHeap pages"
            );
        }

        const DirectorySnapshot snapshot =
            ReadDirectoryPage(current);

        count += snapshot.data_page_ids.size();

        if (!snapshot.has_next) {
            break;
        }

        current = snapshot.next_page_id;
    }

    return count;
}


// ==================================================
// TRANSACTIONAL TABLE HEAP OPERATIONS
// ==================================================


// --------------------------------------------------
// Transactional INSERT
// --------------------------------------------------

RID TableHeap::InsertRecord(
    Transaction& transaction,
    TxnManager& txn_manager,
    const std::vector<std::uint8_t>& record
) {
        auto operation_lock = transaction.AcquireOperationLock();

    if (record.empty()) {
        throw StorageException(
            "Cannot insert an empty record"
        );
    }

    constexpr std::size_t kMaximumRecordSize =
        Config::kPageSizeBytes -
        SlottedPage::kHeaderSize -
        SlottedPage::kSlotEntrySize;

    if (record.size() > kMaximumRecordSize) {
        throw StorageException(
            "Record is too large for an empty SlottedPage"
        );
    }

    page_id_t new_page_id = 0;

    // STEP 1: Allocate a fresh page.
    // Keep it outside the directory until its RID lock
    // has been acquired. This prevents ordinary inserts
    // from consuming its first slot.

    {
        std::lock_guard<std::mutex> lock(mutex_);

        PageGuard new_guard =
            buffer_pool_.NewPage(&new_page_id);

        if (!new_guard.IsValid()) {
            throw StorageException(
                "Unable to allocate transactional data page"
            );
        }

        SlottedPage::Initialize(
            new_guard.GetPageData(),
            new_page_id
        );

        new_guard.Drop();

        // Persist an initialized empty page before
        // creating a WAL record referring to it.
        if (!buffer_pool_.FlushPage(new_page_id)) {
            throw StorageException(
                "Unable to persist transactional data page"
            );
        }
    }

    const RID rid{
        new_page_id,
        0
    };

    // STEP 2: Acquire the exclusive record lock.
    // No TableHeap mutex is held while acquiring the lock.

    txn_manager.LockExclusive(transaction, rid);

    // STEP 3: Prepare the page and append its directory entry.

    std::lock_guard<std::mutex> lock(mutex_);

    PageGuard guard =
        buffer_pool_.FetchPage(new_page_id);

    if (!guard.IsValid()) {
        throw StorageException(
            "Unable to fetch transactional data page"
        );
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage candidate(page_copy);

    if (candidate.GetPageId() != new_page_id) {
        throw StorageException(
            "Transactional page ID mismatch"
        );
    }

    const auto slot_id =
        candidate.InsertRecord(record);

    if (!slot_id.has_value() || *slot_id != rid.slot_id) {
        throw StorageException(
            "Unable to reserve the expected transactional slot"
        );
    }

    // Register the initialized page before logging.
    // If a crash happens before INSERT is logged,
    // the result is merely an empty data page.

    AppendDataPageToDirectoryLocked(new_page_id);

    // STEP 4: Prepare the INSERT WAL record.

    LogRecord log_record;

    log_record.type = LogRecordType::INSERT;

    log_record.has_page = true;

    log_record.page_id = rid.page_id;

    log_record.slot_id = rid.slot_id;

    log_record.after_image = record;

    // STEP 5: WAL must become durable BEFORE changing
    // the actual buffer page.

    txn_manager.AppendLogRecord(
        transaction,
        std::move(log_record)
    );

    // STEP 6: Apply the prepared page modification.

    guard.GetPageData() = page_copy;

    return rid;
}


// --------------------------------------------------
// Transactional READ
// --------------------------------------------------

std::vector<std::uint8_t> TableHeap::GetRecord(
    Transaction& transaction,
    TxnManager& txn_manager,
    const RID& rid
) const {
        auto operation_lock = transaction.AcquireOperationLock();

    // Shared lock allows concurrent readers.
    txn_manager.LockShared(transaction, rid);

    return GetRecord(rid);
}


// --------------------------------------------------
// Transactional UPDATE
// --------------------------------------------------

bool TableHeap::UpdateRecord(
    Transaction& transaction,
    TxnManager& txn_manager,
    const RID& rid,
    const std::vector<std::uint8_t>& record
) {
        auto operation_lock = transaction.AcquireOperationLock();

    if (record.empty()) {
        throw StorageException(
            "Cannot update a record with empty data"
        );
    }

    // STEP 1: Acquire exclusive lock BEFORE
    // accessing the record.

    txn_manager.LockExclusive(transaction, rid);

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(rid.page_id)) {
        return false;
    }

    PageGuard guard =
        buffer_pool_.FetchPage(rid.page_id);

    if (!guard.IsValid()) {
        return false;
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage candidate(page_copy);

    if (candidate.GetPageId() != rid.page_id) {
        throw StorageException(
            "RID page ID does not match SlottedPage ID"
        );
    }

    if (!candidate.IsSlotOccupied(rid.slot_id)) {
        return false;
    }

    // Capture the original value.
    const std::vector<std::uint8_t> before_image =
        candidate.GetRecord(rid.slot_id);

    // Prepare update on a temporary page copy.
    if (!candidate.UpdateRecord(rid.slot_id, record)) {
        return false;
    }

    // STEP 2: Create UPDATE WAL record.

    LogRecord log_record;

    log_record.type = LogRecordType::UPDATE;

    log_record.has_page = true;

    log_record.page_id = rid.page_id;

    log_record.slot_id = rid.slot_id;

    log_record.before_image = before_image;

    log_record.after_image = record;

    // STEP 3: Force WAL before modifying actual page.

    txn_manager.AppendLogRecord(
        transaction,
        std::move(log_record)
    );

    // STEP 4: Apply the update.

    guard.GetPageData() = page_copy;

    return true;
}


// --------------------------------------------------
// Transactional DELETE
// --------------------------------------------------

bool TableHeap::DeleteRecord(
    Transaction& transaction,
    TxnManager& txn_manager,
    const RID& rid
) {
        auto operation_lock = transaction.AcquireOperationLock();

    // STEP 1: Exclusive lock.
    txn_manager.LockExclusive(transaction, rid);

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(rid.page_id)) {
        return false;
    }

    PageGuard guard =
        buffer_pool_.FetchPage(rid.page_id);

    if (!guard.IsValid()) {
        return false;
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage candidate(page_copy);

    if (candidate.GetPageId() != rid.page_id) {
        throw StorageException(
            "RID page ID does not match SlottedPage ID"
        );
    }

    if (!candidate.IsSlotOccupied(rid.slot_id)) {
        return false;
    }

    // Capture the complete record before deleting.
    const std::vector<std::uint8_t> before_image =
        candidate.GetRecord(rid.slot_id);

    if (!candidate.DeleteRecord(rid.slot_id)) {
        return false;
    }

    // STEP 2: Prepare DELETE WAL record.

    LogRecord log_record;

    log_record.type = LogRecordType::DELETE;

    log_record.has_page = true;

    log_record.page_id = rid.page_id;

    log_record.slot_id = rid.slot_id;

    log_record.before_image = before_image;

    // DELETE after_image is intentionally empty.

    // STEP 3: Persist WAL first.
    txn_manager.AppendLogRecord(
        transaction,
        std::move(log_record)
    );

    // STEP 4: Apply deletion.

    guard.GetPageData() = page_copy;

    return true;
}


// --------------------------------------------------
// UndoRecord
// --------------------------------------------------

void TableHeap::UndoRecord(const LogRecord& record) {

    if (!IsWellFormedLogRecord(record) ||
        !IsDataModificationRecord(record.type) ||
        !record.has_page) {

        throw StorageException(
            "Invalid WAL record supplied to TableHeap::UndoRecord"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(record.page_id)) {
        throw StorageException(
            "Undo WAL record references a page outside this TableHeap"
        );
    }

    PageGuard guard =
        buffer_pool_.FetchPage(record.page_id);

    if (!guard.IsValid()) {
        throw StorageException(
            "Unable to fetch page during transaction rollback"
        );
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage candidate(page_copy);

    if (candidate.GetPageId() != record.page_id) {
        throw StorageException(
            "Undo page ID mismatch"
        );
    }

    const slot_id_t slot_id = record.slot_id;

    bool changed = false;

    // ----------------------------------------------
    // Undo INSERT
    // ----------------------------------------------

    if (record.type == LogRecordType::INSERT) {

        if (candidate.IsSlotOccupied(slot_id)) {

            const auto current =
                candidate.GetRecord(slot_id);

            if (current != record.after_image) {
                throw StorageException(
                    "INSERT undo found unexpected record contents"
                );
            }

            changed = candidate.DeleteRecord(slot_id);
        }
        // If already deleted, undo is idempotent.
    }

    // ----------------------------------------------
    // Undo UPDATE
    // ----------------------------------------------

    else if (record.type == LogRecordType::UPDATE) {

        if (candidate.IsSlotOccupied(slot_id)) {

            const auto current =
                candidate.GetRecord(slot_id);

            // Already undone.
            if (current == record.before_image) {
                return;
            }

            // Do not overwrite an unexpected version.
            if (current != record.after_image) {
                throw StorageException(
                    "UPDATE undo found unexpected record contents"
                );
            }

            if (!candidate.UpdateRecord(
                    slot_id,
                    record.before_image
                )) {

                throw StorageException(
                    "Unable to restore UPDATE before-image"
                );
            }

            changed = true;
        }
        else {

            if (!candidate.RestoreRecord(
                    slot_id,
                    record.before_image
                )) {

                throw StorageException(
                    "Unable to restore deleted UPDATE slot"
                );
            }

            changed = true;
        }
    }

    // ----------------------------------------------
    // Undo DELETE
    // ----------------------------------------------

    else if (record.type == LogRecordType::DELETE) {

        if (candidate.IsSlotOccupied(slot_id)) {

            const auto current =
                candidate.GetRecord(slot_id);

            // Already restored.
            if (current == record.before_image) {
                return;
            }

            throw StorageException(
                "DELETE undo found unexpected record contents"
            );
        }

        if (!candidate.RestoreRecord(
                slot_id,
                record.before_image
            )) {

            throw StorageException(
                "Unable to restore deleted record"
            );
        }

        changed = true;
    }

    // Write back only if undo changed the page.
    if (changed) {
        guard.GetPageData() = page_copy;
    }
}


// --------------------------------------------------
// RedoRecord
// --------------------------------------------------

void TableHeap::RedoRecord(const LogRecord& record) {

    if (!IsWellFormedLogRecord(record) ||
        !IsDataModificationRecord(record.type) ||
        !record.has_page) {

        throw StorageException(
            "Invalid WAL record supplied to TableHeap::RedoRecord"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!ContainsDataPageLocked(record.page_id)) {
        throw StorageException(
            "Redo WAL record references a page outside this TableHeap"
        );
    }

    PageGuard guard =
        buffer_pool_.FetchPage(record.page_id);

    if (!guard.IsValid()) {
        throw StorageException(
            "Unable to fetch page during redo"
        );
    }

    const PageGuard& read_guard = guard;

    DiskManager::PageData page_copy =
        read_guard.GetPageData();

    SlottedPage candidate(page_copy);

    if (candidate.GetPageId() != record.page_id) {
        throw StorageException(
            "Redo page ID mismatch"
        );
    }

    const slot_id_t slot_id = record.slot_id;

    bool changed = false;

    // ----------------------------------------------
    // REDO INSERT
    // ----------------------------------------------

    if (record.type == LogRecordType::INSERT) {

        if (candidate.IsSlotOccupied(slot_id)) {

            const auto current =
                candidate.GetRecord(slot_id);

            // Already applied: idempotent redo.
            if (current == record.after_image) {
                return;
            }

            throw StorageException(
                "INSERT redo found unexpected record contents"
            );
        }

        const auto inserted =
            candidate.InsertRecord(record.after_image);

        if (!inserted.has_value() || *inserted != slot_id) {
            throw StorageException(
                "Unable to redo INSERT at the original slot"
            );
        }

        changed = true;
    }

    // ----------------------------------------------
    // REDO UPDATE
    // ----------------------------------------------

    else if (record.type == LogRecordType::UPDATE) {

        if (!candidate.IsSlotOccupied(slot_id)) {
            throw StorageException(
                "UPDATE redo references an empty slot"
            );
        }

        const auto current =
            candidate.GetRecord(slot_id);

        // Already applied.
        if (current == record.after_image) {
            return;
        }

        if (current != record.before_image) {
            throw StorageException(
                "UPDATE redo found unexpected record contents"
            );
        }

        if (!candidate.UpdateRecord(
                slot_id,
                record.after_image
            )) {

            throw StorageException(
                "Unable to redo UPDATE"
            );
        }

        changed = true;
    }

    // ----------------------------------------------
    // REDO DELETE
    // ----------------------------------------------

    else if (record.type == LogRecordType::DELETE) {

        if (!candidate.IsSlotOccupied(slot_id)) {
            return;
        }

        const auto current =
            candidate.GetRecord(slot_id);

        if (current != record.before_image) {
            throw StorageException(
                "DELETE redo found unexpected record contents"
            );
        }

        changed = candidate.DeleteRecord(slot_id);
    }

    if (changed) {
        guard.GetPageData() = page_copy;
    }
}


bool TableHeap::OwnsPage(page_id_t page_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return ContainsDataPageLocked(page_id);
}


std::vector<RID> TableHeap::GetAllRIDs() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<RID> result;

    std::unordered_set<page_id_t> visited;

    page_id_t current_directory = root_page_id_;

    while (true) {

        if (!visited.insert(current_directory).second) {
            throw StorageException(
                "Cycle detected while scanning TableHeap"
            );
        }

        const DirectorySnapshot snapshot =
            ReadDirectoryPage(current_directory);

        for (page_id_t data_page_id : snapshot.data_page_ids) {

            PageGuard guard =
                buffer_pool_.FetchPage(data_page_id);

            if (!guard.IsValid()) {
                throw StorageException(
                    "Unable to fetch data page during scan"
                );
            }

            const PageGuard& read_guard = guard;

            DiskManager::PageData page_copy =
                read_guard.GetPageData();

            SlottedPage page(page_copy);

            if (page.GetPageId() != data_page_id) {
                throw StorageException(
                    "Data page ID mismatch during scan"
                );
            }

            const std::uint16_t slot_count =
                page.GetSlotCount();

            for (std::uint32_t i = 0; i < slot_count; ++i) {

                const auto slot_id =
                    static_cast<slot_id_t>(i);

                if (page.IsSlotOccupied(slot_id)) {
                    result.push_back({
                        data_page_id,
                        slot_id
                    });
                }
            }
        }

        if (!snapshot.has_next) {
            break;
        }

        current_directory = snapshot.next_page_id;
    }

    return result;
}


} // namespace minidb
