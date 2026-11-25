
#pragma once

#include "minidb/recovery/log_record.hpp"

#include <cstddef>
#include <functional>

namespace minidb {

class LogManager;

// Recovery can either reapply a committed change
// or reverse an incomplete transaction's change.
enum class RecoveryAction {
    REDO,
    UNDO
};


// Summary of one recovery execution.
struct RecoveryResult {

    std::size_t records_scanned{0};

    std::size_t records_redone{0};

    std::size_t records_undone{0};

    std::size_t committed_transactions{0};

    std::size_t aborted_transactions{0};

    std::size_t incomplete_transactions{0};
};


class RecoveryManager {
public:

    // The storage engine will implement this callback.
    // REDO means apply after_image.
    // UNDO means restore before_image.
    using ApplyRecordCallback =
        std::function<void(
            const LogRecord&,
            RecoveryAction
        )>;

    // Called after all redo and undo operations are applied.
    using FlushPagesCallback = std::function<void()>;

    explicit RecoveryManager(LogManager& log_manager);

    // Copying is not allowed.
    RecoveryManager(const RecoveryManager&) = delete;

    RecoveryManager& operator=(
        const RecoveryManager&
    ) = delete;

    // Performs WAL analysis, redo and undo.
    RecoveryResult Recover(
        const ApplyRecordCallback& apply_record,
        const FlushPagesCallback& flush_pages
    );

private:

    LogManager& log_manager_;
};

} // namespace minidb
