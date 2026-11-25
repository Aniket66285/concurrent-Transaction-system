
#pragma once

#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"

#include <functional>
#include <mutex>
#include <string>

namespace minidb {

class LogManager;

class CheckpointManager {
public:

    // Function called to flush all dirty database pages.
    using PageFlushCallback = std::function<void()>;

    explicit CheckpointManager(
        LogManager& log_manager,
        std::string master_file = Config::kMasterFile
    );

    // Copying is not allowed.
    CheckpointManager(const CheckpointManager&) = delete;

    CheckpointManager& operator=(
        const CheckpointManager&
    ) = delete;

    // Creates a new durable checkpoint.
    lsn_t CreateCheckpoint(
        const PageFlushCallback& flush_dirty_pages
    );

    // Returns the latest checkpoint LSN.
    lsn_t GetLastCheckpointLSN() const;

    // Checks whether a checkpoint exists.
    bool HasCheckpoint() const;

private:

    // Loads the checkpoint pointer from the master file.
    void LoadMasterRecord();

    // Atomically saves the checkpoint pointer.
    void PersistMasterRecord(lsn_t checkpoint_lsn) const;

    LogManager& log_manager_;

    std::string master_file_;

    mutable std::mutex mutex_;

    lsn_t last_checkpoint_lsn_{kInvalidLSN};

    bool has_checkpoint_{false};
};

} // namespace minidb
