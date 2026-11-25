
#pragma once

#include <cstdint>
#include <limits>

namespace minidb {

// Database identifiers.
using table_id_t = std::uint32_t;
using page_id_t = std::uint32_t;
using frame_id_t = std::uint32_t;
using slot_id_t = std::uint16_t;

// Transaction identifier.
using txn_id_t = std::uint64_t;

// Log Sequence Number.
using lsn_t = std::uint64_t;

// Invalid LSN marker.
inline constexpr lsn_t kInvalidLSN =
    std::numeric_limits<lsn_t>::max();


// Record identifier.
struct RID {

    page_id_t page_id{0};

    slot_id_t slot_id{0};

    bool operator==(const RID& other) const {

        return page_id == other.page_id &&
               slot_id == other.slot_id;
    }

    bool operator!=(const RID& other) const {

        return !(*this == other);
    }
};


// Transaction states.
enum class TransactionState {
    ACTIVE,
    COMMITTED,
    ABORTED
};


// Page states.
enum class PageState {
    FREE,
    VALID,
    DIRTY
};


// Lock modes.
enum class LockMode {
    SHARED,
    EXCLUSIVE
};

} // namespace minidb
