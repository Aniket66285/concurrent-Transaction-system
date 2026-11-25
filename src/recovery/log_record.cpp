
#include "minidb/recovery/log_record.hpp"

namespace minidb {

bool IsValidLogRecordType(LogRecordType type) noexcept {

    switch (type) {
        case LogRecordType::BEGIN:
        case LogRecordType::INSERT:
        case LogRecordType::UPDATE:
        case LogRecordType::DELETE:
        case LogRecordType::COMMIT:
        case LogRecordType::ABORT:
        case LogRecordType::CHECKPOINT:
            return true;

        default:
            return false;
    }
}


bool IsDataModificationRecord(LogRecordType type) noexcept {

    switch (type) {
        case LogRecordType::INSERT:
        case LogRecordType::UPDATE:
        case LogRecordType::DELETE:
            return true;

        default:
            return false;
    }
}


bool IsUndoableLogRecord(LogRecordType type) noexcept {

    return IsDataModificationRecord(type);
}


bool IsRedoableLogRecord(LogRecordType type) noexcept {

    return IsDataModificationRecord(type);
}


bool IsWellFormedLogRecord(const LogRecord& record) noexcept {

    if (!IsValidLogRecordType(record.type)) {
        return false;
    }

    // Data modification records must identify a page.
    if (IsDataModificationRecord(record.type) && !record.has_page) {
        return false;
    }

    // Control records must not refer to a data page.
    if (!IsDataModificationRecord(record.type) && record.has_page) {
        return false;
    }

    switch (record.type) {

        case LogRecordType::INSERT:
            return record.before_image.empty() &&
                   !record.after_image.empty();

        case LogRecordType::UPDATE:
            return !record.before_image.empty() &&
                   !record.after_image.empty();

        case LogRecordType::DELETE:
            return !record.before_image.empty() &&
                   record.after_image.empty();

        case LogRecordType::BEGIN:
        case LogRecordType::COMMIT:
        case LogRecordType::ABORT:
        case LogRecordType::CHECKPOINT:
            return record.before_image.empty() &&
                   record.after_image.empty();

        default:
            return false;
    }
}


const char* LogRecordTypeToString(LogRecordType type) noexcept {

    switch (type) {

        case LogRecordType::BEGIN:
            return "BEGIN";

        case LogRecordType::INSERT:
            return "INSERT";

        case LogRecordType::UPDATE:
            return "UPDATE";

        case LogRecordType::DELETE:
            return "DELETE";

        case LogRecordType::COMMIT:
            return "COMMIT";

        case LogRecordType::ABORT:
            return "ABORT";

        case LogRecordType::CHECKPOINT:
            return "CHECKPOINT";

        default:
            return "INVALID";
    }
}

} // namespace minidb
