
#pragma once

#include "minidb/common/types.hpp"

#include <cstdint>
#include <vector>

namespace minidb {

// Different types of events that can be written to the WAL.
enum class LogRecordType : std::uint8_t {
    INVALID    = 0,
    BEGIN      = 1,
    INSERT     = 2,
    UPDATE     = 3,
    DELETE     = 4,
    COMMIT     = 5,
    ABORT      = 6,
    CHECKPOINT = 7
};

// Represents one database log record in memory.
struct LogRecord {

    lsn_t lsn{kInvalidLSN};
    txn_id_t txn_id{0};
    lsn_t prev_lsn{kInvalidLSN};

    LogRecordType type{LogRecordType::INVALID};

    bool has_page{false};

    page_id_t page_id{0};
    slot_id_t slot_id{0};

    std::vector<std::uint8_t> before_image;
    std::vector<std::uint8_t> after_image;
};

// Helper function declarations.
bool IsValidLogRecordType(LogRecordType type) noexcept;

bool IsDataModificationRecord(LogRecordType type) noexcept;

bool IsUndoableLogRecord(LogRecordType type) noexcept;

bool IsRedoableLogRecord(LogRecordType type) noexcept;

bool IsWellFormedLogRecord(const LogRecord& record) noexcept;

const char* LogRecordTypeToString(LogRecordType type) noexcept;

} // namespace minidb
