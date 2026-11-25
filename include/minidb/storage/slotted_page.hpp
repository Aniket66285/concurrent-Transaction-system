
#pragma once

#include "minidb/storage/disk_manager.hpp"
#include "minidb/common/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace minidb {

class SlottedPage {
public:

    using PageData = DiskManager::PageData;

    static constexpr std::size_t kHeaderSize = 16;
    static constexpr std::size_t kSlotEntrySize = 4;

    // Initializes an empty 4096-byte page.
    static void Initialize(
        PageData& page,
        page_id_t page_id
    );

    static bool IsInitialized(
        const PageData& page
    ) noexcept;

    explicit SlottedPage(PageData& page);

    page_id_t GetPageId() const;

    std::uint16_t GetSlotCount() const;

    std::size_t GetRecordCount() const;

    bool IsSlotOccupied(slot_id_t slot_id) const;

    std::size_t GetFreeSpace() const;

    std::size_t GetTotalFreeSpace() const;

    // Inserts a record into an available slot.
    std::optional<slot_id_t> InsertRecord(
        const std::vector<std::uint8_t>& record
    );

    // Reads a record.
    std::vector<std::uint8_t> GetRecord(
        slot_id_t slot_id
    ) const;

    // Updates an existing record.
    bool UpdateRecord(
        slot_id_t slot_id,
        const std::vector<std::uint8_t>& record
    );

    // Deletes an existing record.
    bool DeleteRecord(slot_id_t slot_id);

    // Restores a deleted record into its original slot.
    // Does not allocate a new slot ID.
    // Returns false if the slot is occupied, invalid,
    // or insufficient space is available.
    bool RestoreRecord(
        slot_id_t slot_id,
        const std::vector<std::uint8_t>& record
    ) {

        ValidatePage();

        if (record.empty()) {
            return false;
        }

        const std::uint16_t slot_count = GetSlotCount();

        if (slot_id >= slot_count ||
            IsSlotOccupied(slot_id)) {
            return false;
        }

        std::vector<std::vector<std::uint8_t>> records(
            slot_count
        );

        std::size_t total_record_bytes = record.size();

        // Preserve all existing records.
        for (std::uint32_t i = 0; i < slot_count; ++i) {

            const auto current_slot =
                static_cast<slot_id_t>(i);

            if (IsSlotOccupied(current_slot)) {

                records[i] = GetRecord(current_slot);

                total_record_bytes += records[i].size();
            }
        }

        // Restore into the exact original slot.
        records[slot_id] = record;

        const std::size_t metadata_size =
            kHeaderSize +
            static_cast<std::size_t>(slot_count) *
            kSlotEntrySize;

        if (metadata_size > Config::kPageSizeBytes ||
            total_record_bytes >
                Config::kPageSizeBytes - metadata_size) {
            return false;
        }

        // Rebuild while preserving every slot ID.
        RewriteRecords(records);

        return true;
    }

    // Compacts fragmented page space.
    void Compact();

private:

    void ValidatePage() const;

    void RewriteRecords(
        const std::vector<std::vector<std::uint8_t>>& records
    );

    PageData& page_;
};

} // namespace minidb
