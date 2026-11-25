
#include "minidb/storage/slotted_page.hpp"

#include "minidb/common/exception.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace minidb {

namespace {

// Page format:
// Bytes 0-3   : Magic "SLPG"
// Byte 4      : Format version
// Byte 5      : Reserved
// Bytes 6-7   : Slot count
// Bytes 8-9   : Free-space lower boundary
// Bytes 10-11 : Free-space upper boundary
// Bytes 12-15 : Page ID
//
// Each slot entry:
// 2 bytes: Record offset
// 2 bytes: Record length

constexpr std::array<std::uint8_t, 4> kMagic{
    {'S', 'L', 'P', 'G'}
};

constexpr std::uint8_t kFormatVersion = 1;

constexpr std::size_t kSlotCountOffset = 6;

constexpr std::size_t kFreeLowerOffset = 8;

constexpr std::size_t kFreeUpperOffset = 10;

constexpr std::size_t kPageIdOffset = 12;


// --------------------------------------------------
// WriteUint16
// --------------------------------------------------

void WriteUint16(
    std::uint8_t* destination,
    std::uint16_t value
) {

    destination[0] =
        static_cast<std::uint8_t>(value & 0xFF);

    destination[1] =
        static_cast<std::uint8_t>((value >> 8) & 0xFF);
}


// --------------------------------------------------
// ReadUint16
// --------------------------------------------------

std::uint16_t ReadUint16(
    const std::uint8_t* source
) {

    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) |
        (static_cast<std::uint16_t>(source[1]) << 8)
    );
}


// --------------------------------------------------
// WriteUint32
// --------------------------------------------------

void WriteUint32(
    std::uint8_t* destination,
    std::uint32_t value
) {

    for (std::size_t i = 0; i < 4; ++i) {

        destination[i] =
            static_cast<std::uint8_t>(
                (value >> (8 * i)) & 0xFF
            );
    }
}


// --------------------------------------------------
// ReadUint32
// --------------------------------------------------

std::uint32_t ReadUint32(
    const std::uint8_t* source
) {

    std::uint32_t value = 0;

    for (std::size_t i = 0; i < 4; ++i) {

        value |=
            static_cast<std::uint32_t>(source[i])
            << (8 * i);
    }

    return value;
}


// --------------------------------------------------
// SlotEntryOffset
// --------------------------------------------------

std::size_t SlotEntryOffset(slot_id_t slot_id) {

    return SlottedPage::kHeaderSize +
           static_cast<std::size_t>(slot_id) *
           SlottedPage::kSlotEntrySize;
}


// --------------------------------------------------
// ReadSlotOffset
// --------------------------------------------------

std::uint16_t ReadSlotOffset(
    const SlottedPage::PageData& page,
    slot_id_t slot_id
) {

    return ReadUint16(
        page.data() + SlotEntryOffset(slot_id)
    );
}


// --------------------------------------------------
// ReadSlotLength
// --------------------------------------------------

std::uint16_t ReadSlotLength(
    const SlottedPage::PageData& page,
    slot_id_t slot_id
) {

    return ReadUint16(
        page.data() + SlotEntryOffset(slot_id) + 2
    );
}


// --------------------------------------------------
// WriteSlot
// --------------------------------------------------

void WriteSlot(
    SlottedPage::PageData& page,
    slot_id_t slot_id,
    std::uint16_t offset,
    std::uint16_t length
) {

    const std::size_t entryOffset =
        SlotEntryOffset(slot_id);

    WriteUint16(
        page.data() + entryOffset,
        offset
    );

    WriteUint16(
        page.data() + entryOffset + 2,
        length
    );
}


// --------------------------------------------------
// ReadHeaderField
// --------------------------------------------------

std::uint16_t ReadHeaderField(
    const SlottedPage::PageData& page,
    std::size_t offset
) {

    return ReadUint16(page.data() + offset);
}


// --------------------------------------------------
// WriteHeaderField
// --------------------------------------------------

void WriteHeaderField(
    SlottedPage::PageData& page,
    std::size_t offset,
    std::uint16_t value
) {

    WriteUint16(
        page.data() + offset,
        value
    );
}

} // anonymous namespace


// --------------------------------------------------
// Initialize
// --------------------------------------------------

void SlottedPage::Initialize(
    PageData& page,
    page_id_t page_id
) {

    // Start with a completely zeroed page.
    page.fill(0);


    // Magic signature.
    std::copy(
        kMagic.begin(),
        kMagic.end(),
        page.begin()
    );


    // Format version.
    page[4] = kFormatVersion;


    // Reserved byte.
    page[5] = 0;


    // No slots initially.
    WriteHeaderField(
        page,
        kSlotCountOffset,
        0
    );


    // Slot directory starts immediately after the header.
    WriteHeaderField(
        page,
        kFreeLowerOffset,
        static_cast<std::uint16_t>(kHeaderSize)
    );


    // Record area initially starts at the end.
    WriteHeaderField(
        page,
        kFreeUpperOffset,
        static_cast<std::uint16_t>(Config::kPageSizeBytes)
    );


    // Store the page ID.
    WriteUint32(
        page.data() + kPageIdOffset,
        page_id
    );
}


// --------------------------------------------------
// IsInitialized
// --------------------------------------------------

bool SlottedPage::IsInitialized(
    const PageData& page
) noexcept {

    return page[0] == 'S' &&
           page[1] == 'L' &&
           page[2] == 'P' &&
           page[3] == 'G' &&
           page[4] == kFormatVersion &&
           page[5] == 0;
}


// --------------------------------------------------
// Constructor
// --------------------------------------------------

SlottedPage::SlottedPage(PageData& page)
    : page_(page) {

    ValidatePage();
}


// --------------------------------------------------
// ValidatePage
// --------------------------------------------------

void SlottedPage::ValidatePage() const {

    if (!IsInitialized(page_)) {

        throw StorageException(
            "Invalid SlottedPage signature or version"
        );
    }


    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    const std::uint16_t lower =
        ReadHeaderField(page_, kFreeLowerOffset);


    const std::uint16_t upper =
        ReadHeaderField(page_, kFreeUpperOffset);


    const std::size_t expectedLower =
        kHeaderSize +
        static_cast<std::size_t>(slotCount) *
        kSlotEntrySize;


    if (lower != expectedLower ||
        lower > upper ||
        upper > Config::kPageSizeBytes) {

        throw StorageException(
            "Invalid SlottedPage free-space boundaries"
        );
    }


    // Collect active record ranges to detect overlap.
    std::vector<std::pair<std::size_t, std::size_t>> ranges;

    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        const std::uint16_t offset =
            ReadSlotOffset(page_, slot);

        const std::uint16_t length =
            ReadSlotLength(page_, slot);


        // Deleted slots have both values equal to zero.
        if (offset == 0 && length == 0) {
            continue;
        }


        if (offset == 0 || length == 0) {

            throw StorageException(
                "Invalid SlottedPage slot entry"
            );
        }


        const std::size_t end =
            static_cast<std::size_t>(offset) + length;


        if (offset < upper ||
            end > Config::kPageSizeBytes) {

            throw StorageException(
                "SlottedPage record lies outside the record area"
            );
        }


        ranges.emplace_back(
            offset,
            end
        );
    }


    // Active records must never overlap.
    std::sort(
        ranges.begin(),
        ranges.end()
    );

    for (std::size_t i = 1; i < ranges.size(); ++i) {

        if (ranges[i].first < ranges[i - 1].second) {

            throw StorageException(
                "Overlapping records detected in SlottedPage"
            );
        }
    }
}


// --------------------------------------------------
// GetPageId
// --------------------------------------------------

page_id_t SlottedPage::GetPageId() const {

    ValidatePage();

    return ReadUint32(
        page_.data() + kPageIdOffset
    );
}


// --------------------------------------------------
// GetSlotCount
// --------------------------------------------------

std::uint16_t SlottedPage::GetSlotCount() const {

    ValidatePage();

    return ReadHeaderField(
        page_,
        kSlotCountOffset
    );
}


// --------------------------------------------------
// GetRecordCount
// --------------------------------------------------

std::size_t SlottedPage::GetRecordCount() const {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);

    std::size_t count = 0;

    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        if (ReadSlotLength(page_, slot) != 0) {
            ++count;
        }
    }

    return count;
}


// --------------------------------------------------
// IsSlotOccupied
// --------------------------------------------------

bool SlottedPage::IsSlotOccupied(
    slot_id_t slot_id
) const {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);

    if (slot_id >= slotCount) {
        return false;
    }

    return ReadSlotLength(page_, slot_id) != 0;
}


// --------------------------------------------------
// GetFreeSpace
// --------------------------------------------------

std::size_t SlottedPage::GetFreeSpace() const {

    ValidatePage();

    const std::uint16_t lower =
        ReadHeaderField(page_, kFreeLowerOffset);

    const std::uint16_t upper =
        ReadHeaderField(page_, kFreeUpperOffset);

    return upper - lower;
}


// --------------------------------------------------
// GetTotalFreeSpace
// --------------------------------------------------

std::size_t SlottedPage::GetTotalFreeSpace() const {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);

    const std::uint16_t lower =
        ReadHeaderField(page_, kFreeLowerOffset);


    std::size_t usedRecordBytes = 0;


    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        usedRecordBytes +=
            ReadSlotLength(page_, slot);
    }


    const std::size_t totalUsed =
        static_cast<std::size_t>(lower) +
        usedRecordBytes;


    if (totalUsed > Config::kPageSizeBytes) {

        throw StorageException(
            "Invalid total page space accounting"
        );
    }

    return Config::kPageSizeBytes - totalUsed;
}


// --------------------------------------------------
// GetRecord
// --------------------------------------------------

std::vector<std::uint8_t> SlottedPage::GetRecord(
    slot_id_t slot_id
) const {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);

    if (slot_id >= slotCount) {

        throw StorageException(
            "SlottedPage slot ID is out of range"
        );
    }


    const std::uint16_t offset =
        ReadSlotOffset(page_, slot_id);

    const std::uint16_t length =
        ReadSlotLength(page_, slot_id);


    if (offset == 0 || length == 0) {

        throw StorageException(
            "SlottedPage slot is empty or deleted"
        );
    }


    return std::vector<std::uint8_t>(
        page_.begin() + offset,
        page_.begin() + offset + length
    );
}


// --------------------------------------------------
// InsertRecord
// --------------------------------------------------

std::optional<slot_id_t> SlottedPage::InsertRecord(
    const std::vector<std::uint8_t>& record
) {

    ValidatePage();


    if (record.empty()) {

        throw StorageException(
            "Cannot insert an empty record"
        );
    }


    if (record.size() >
        std::numeric_limits<std::uint16_t>::max()) {

        return std::nullopt;
    }


    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    // Reuse the first deleted slot if one exists.
    std::optional<slot_id_t> reusableSlot;


    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        if (ReadSlotLength(page_, slot) == 0) {

            reusableSlot = slot;

            break;
        }
    }


    // A new slot needs an additional four bytes.
    const std::size_t descriptorCost =
        reusableSlot.has_value()
            ? 0
            : kSlotEntrySize;


    if (record.size() + descriptorCost >
        GetTotalFreeSpace()) {

        return std::nullopt;
    }


    // Compact before insertion if necessary.
    // This reclaims deleted-record holes.
    Compact();


    std::uint16_t lower =
        ReadHeaderField(page_, kFreeLowerOffset);

    std::uint16_t upper =
        ReadHeaderField(page_, kFreeUpperOffset);


    slot_id_t slotId;


    if (reusableSlot.has_value()) {

        slotId = *reusableSlot;
    }
    else {

        if (slotCount ==
            std::numeric_limits<std::uint16_t>::max()) {

            return std::nullopt;
        }

        slotId = slotCount;

        lower = static_cast<std::uint16_t>(
            lower + kSlotEntrySize
        );

        WriteHeaderField(
            page_,
            kSlotCountOffset,
            static_cast<std::uint16_t>(slotCount + 1)
        );
    }


    const std::uint16_t recordLength =
        static_cast<std::uint16_t>(record.size());


    if (recordLength > upper - lower) {
        return std::nullopt;
    }


    // Allocate data space from the end of the page.
    upper = static_cast<std::uint16_t>(
        upper - recordLength
    );


    std::copy(
        record.begin(),
        record.end(),
        page_.begin() + upper
    );


    // Write the slot descriptor.
    WriteSlot(
        page_,
        slotId,
        upper,
        recordLength
    );


    // Update page boundaries.
    WriteHeaderField(
        page_,
        kFreeLowerOffset,
        lower
    );

    WriteHeaderField(
        page_,
        kFreeUpperOffset,
        upper
    );


    return slotId;
}


// --------------------------------------------------
// RewriteRecords
// --------------------------------------------------

void SlottedPage::RewriteRecords(
    const std::vector<std::vector<std::uint8_t>>& records
) {

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    if (records.size() != slotCount) {

        throw StorageException(
            "Invalid record count during SlottedPage rewrite"
        );
    }


    PageData rebuilt{};

    const page_id_t pageId = GetPageId();


    // Reinitialize the new page.
    Initialize(
        rebuilt,
        pageId
    );


    const std::size_t lower =
        kHeaderSize +
        static_cast<std::size_t>(slotCount) *
        kSlotEntrySize;


    std::size_t upper =
        Config::kPageSizeBytes;


    // Preserve every slot ID, including deleted slots.
    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        const auto& record = records[i];


        // An empty vector means this slot is deleted.
        if (record.empty()) {

            WriteSlot(
                rebuilt,
                slot,
                0,
                0
            );

            continue;
        }


        if (record.size() > upper - lower) {

            throw StorageException(
                "Records do not fit during SlottedPage rewrite"
            );
        }


        upper -= record.size();


        std::copy(
            record.begin(),
            record.end(),
            rebuilt.begin() + upper
        );


        WriteSlot(
            rebuilt,
            slot,
            static_cast<std::uint16_t>(upper),
            static_cast<std::uint16_t>(record.size())
        );
    }


    // Update directory and record boundaries.
    WriteHeaderField(
        rebuilt,
        kSlotCountOffset,
        slotCount
    );

    WriteHeaderField(
        rebuilt,
        kFreeLowerOffset,
        static_cast<std::uint16_t>(lower)
    );

    WriteHeaderField(
        rebuilt,
        kFreeUpperOffset,
        static_cast<std::uint16_t>(upper)
    );


    // Replace the existing page atomically in memory.
    page_ = rebuilt;

    ValidatePage();
}


// --------------------------------------------------
// UpdateRecord
// --------------------------------------------------

bool SlottedPage::UpdateRecord(
    slot_id_t slot_id,
    const std::vector<std::uint8_t>& record
) {

    ValidatePage();


    if (record.empty()) {

        throw StorageException(
            "Cannot update a record with empty data"
        );
    }


    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    if (slot_id >= slotCount ||
        ReadSlotLength(page_, slot_id) == 0) {

        throw StorageException(
            "Cannot update an invalid or deleted slot"
        );
    }


    if (record.size() >
        std::numeric_limits<std::uint16_t>::max()) {

        return false;
    }


    // Copy current records.
    // Replace only the requested slot.
    std::vector<std::vector<std::uint8_t>> records(
        slotCount
    );


    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        if (ReadSlotLength(page_, slot) != 0) {

            records[i] = GetRecord(slot);
        }
    }


    records[slot_id] = record;


    const std::size_t lower =
        kHeaderSize +
        static_cast<std::size_t>(slotCount) *
        kSlotEntrySize;


    std::size_t totalRecordBytes = 0;

    for (const auto& existing : records) {

        totalRecordBytes += existing.size();
    }


    // Check capacity before changing the original page.
    if (totalRecordBytes >
        Config::kPageSizeBytes - lower) {

        return false;
    }


    // Rebuild the page while preserving slot numbers.
    RewriteRecords(records);

    return true;
}


// --------------------------------------------------
// DeleteRecord
// --------------------------------------------------

bool SlottedPage::DeleteRecord(
    slot_id_t slot_id
) {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    if (slot_id >= slotCount) {
        return false;
    }


    if (ReadSlotLength(page_, slot_id) == 0) {
        return false;
    }


    // Mark the slot as deleted.
    // Its old bytes are reclaimed during compaction.
    WriteSlot(
        page_,
        slot_id,
        0,
        0
    );

    return true;
}


// --------------------------------------------------
// Compact
// --------------------------------------------------

void SlottedPage::Compact() {

    ValidatePage();

    const std::uint16_t slotCount =
        ReadHeaderField(page_, kSlotCountOffset);


    std::vector<std::vector<std::uint8_t>> records(
        slotCount
    );


    // Copy every live record before rewriting.
    for (std::uint32_t i = 0; i < slotCount; ++i) {

        const auto slot =
            static_cast<slot_id_t>(i);

        if (ReadSlotLength(page_, slot) != 0) {

            records[i] = GetRecord(slot);
        }
    }


    // RewriteRecords preserves all slot IDs.
    RewriteRecords(records);
}


} // namespace minidb
