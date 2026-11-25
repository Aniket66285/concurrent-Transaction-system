#pragma once

#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/catalog/catalog.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/sql/sql_engine.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/server/worker_pool.hpp"

#include <chrono>
#include <string>

namespace minidb {

class RuntimeMetrics {
public:
    RuntimeMetrics(
        DiskManager& disk_manager,
        BufferPool& buffer_pool,
        LogManager& log_manager,
        LockManager& lock_manager,
        TxnManager& txn_manager,
        Catalog& catalog,
        SqlEngine& sql_engine,
        WorkerPool& worker_pool
    );

    // Returns a JSON snapshot made only from live engine/kernel counters.
    std::string ToJson() const;

private:
    DiskManager& disk_manager_;
    BufferPool& buffer_pool_;
    LogManager& log_manager_;
    LockManager& lock_manager_;
    TxnManager& txn_manager_;
    Catalog& catalog_;
    SqlEngine& sql_engine_;
    WorkerPool& worker_pool_;
    const std::chrono::steady_clock::time_point started_at_;
};

} // namespace minidb
