
#pragma once

#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"
#include "minidb/recovery/log_record.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace minidb {

class LogManager {
public:

    // Opens the WAL file.
    explicit LogManager(
        std::string file_path = Config::kWalFile
    );

    ~LogManager() noexcept;

    // Copying is not allowed because this class owns
    // a file descriptor and a mutex.
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    // Writes a record to the WAL and returns its starting offset.
    lsn_t AppendLogRecord(const LogRecord& record);

    // Flushes all appended WAL records.
    void Flush();

    // Ensures the requested record and all previous records
    // are flushed to durable storage.
    void FlushUpTo(lsn_t target_lsn);

    // Reads all records from the WAL file.
    // Optionally repairs an incomplete final record.
    std::vector<LogRecord> ReadAllRecords(
        bool repair_incomplete_tail = false
    );

    // Returns the byte offset where the next record will begin.
    lsn_t GetNextLSN() const;

    // Returns the latest record known to be durable.
    lsn_t GetPersistentLSN() const;

private:

    // Must be called while mutex_ is locked.
    void FlushLocked();

    int fd_{-1};

    std::string file_path_;

    mutable std::mutex mutex_;

    lsn_t next_lsn_{0};

    lsn_t last_record_lsn_{kInvalidLSN};

    lsn_t persistent_lsn_{kInvalidLSN};

    // Prevents further appends after a partial write failure.
    bool write_failed_{false};
};

} // namespace minidb
