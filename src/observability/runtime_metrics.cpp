#include "minidb/observability/runtime_metrics.hpp"
#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"
#include "minidb/os/procfs_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace minidb {
namespace {

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned int>(ch)
                        << std::dec;
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
}

std::uintmax_t FileSizeOrZero(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

} // namespace

RuntimeMetrics::RuntimeMetrics(
    DiskManager& disk_manager,
    BufferPool& buffer_pool,
    LogManager& log_manager,
    LockManager& lock_manager,
    TxnManager& txn_manager,
    Catalog& catalog,
    SqlEngine& sql_engine,
    WorkerPool& worker_pool
)
    : disk_manager_(disk_manager),
      buffer_pool_(buffer_pool),
      log_manager_(log_manager),
      lock_manager_(lock_manager),
      txn_manager_(txn_manager),
      catalog_(catalog),
      sql_engine_(sql_engine),
      worker_pool_(worker_pool),
      started_at_(std::chrono::steady_clock::now()) {}

std::string RuntimeMetrics::ToJson() const {
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started_at_
    ).count();

    const auto tables = catalog_.GetTableNames();
    const auto wait_edges = lock_manager_.GetWaitForEdges();
    const auto worker_tids = worker_pool_.GetWorkerLinuxThreadIds();
    const auto worker_busy = worker_pool_.GetWorkerBusyStates();
    const auto lock_table = lock_manager_.GetLockTableSnapshot();
    const auto database_path = disk_manager_.GetFilePath();
    const auto wal_path = std::string(Config::kWalFile);
    const auto next_lsn = log_manager_.GetNextLSN();
    const auto persistent_lsn = log_manager_.GetPersistentLSN();

    std::ostringstream out;
    out << '{';
    out << "\"uptime_seconds\":" << uptime << ',';
    out << "\"process_id\":" << static_cast<long long>(getpid()) << ',';

    out << "\"config\":{";
    out << "\"page_size_bytes\":" << Config::kPageSizeBytes << ',';
    out << "\"buffer_pool_frames\":" << Config::kBufferPoolFrames << ',';
    out << "\"worker_threads\":" << Config::kWorkerThreads << ',';
    out << "\"request_queue_capacity\":" << Config::kRequestQueueCapacity;
    out << "},";

    out << "\"sql\":{";
    out << "\"sessions\":" << sql_engine_.GetSessionCount() << ',';
    out << "\"active_transactions\":" << txn_manager_.GetActiveTransactionCount();
    out << "},";

    out << "\"buffer_pool\":{";
    out << "\"frame_capacity\":" << buffer_pool_.GetPoolSize() << ',';
    out << "\"capacity_bytes\":"
        << (buffer_pool_.GetPoolSize() * Config::kPageSizeBytes) << ',';
    out << "\"resident_pages\":" << buffer_pool_.GetResidentPageCount() << ',';
    out << "\"pinned_frames\":" << buffer_pool_.GetPinnedFrameCount();
    out << "},";

    out << "\"workers\":{";
    out << "\"count\":" << worker_pool_.GetWorkerCount() << ',';
    out << "\"active_tasks\":" << worker_pool_.GetActiveTaskCount() << ',';
    out << "\"completed_tasks\":" << worker_pool_.GetCompletedTaskCount() << ',';
    out << "\"queue_capacity\":" << worker_pool_.GetQueueCapacity() << ',';
    out << "\"queue_pending\":" << worker_pool_.GetQueueSize() << ',';
    out << "\"threads\":[";
    for (std::size_t i = 0; i < worker_tids.size(); ++i) {
        if (i) out << ',';
        const auto thread = ProcfsReader::ReadThread(worker_tids[i]);
        out << '{'
            << "\"tid\":" << worker_tids[i] << ','
            << "\"last_cpu_core\":" << thread.last_cpu_core << ','
            << "\"cpu_time_ms\":" << thread.cpu_time_ms << ','
            << "\"voluntary_context_switches\":"
            << thread.voluntary_context_switches << ','
            << "\"nonvoluntary_context_switches\":"
            << thread.nonvoluntary_context_switches << ','
            << "\"readable\":" << (thread.readable ? "true" : "false") << ','
            << "\"busy\":" << ((i < worker_busy.size() && worker_busy[i]) ? "true" : "false")
            << '}';
    }
    out << "]},";

    out << "\"storage\":{";
    out << "\"database_path\":\"" << EscapeJson(database_path) << "\",";
    out << "\"database_pages\":" << disk_manager_.GetPageCount() << ',';
    out << "\"database_bytes\":" << FileSizeOrZero(database_path) << ',';
    out << "\"table_count\":" << tables.size() << ',';
    out << "\"tables\":[";
    for (std::size_t i = 0; i < tables.size(); ++i) {
        if (i) out << ',';
        out << '"' << EscapeJson(tables[i]) << '"';
    }
    out << "],\"table_schemas\":[";
    for (std::size_t i = 0; i < tables.size(); ++i) {
        if (i) out << ',';
        const auto metadata = catalog_.GetTable(tables[i]);
        out << "{\"name\":\"" << EscapeJson(tables[i]) << "\",\"columns\":[";
        if (metadata) {
            const auto& columns = metadata->GetSchema().GetColumns();
            for (std::size_t c = 0; c < columns.size(); ++c) {
                if (c) out << ',';
                out << "{\"name\":\"" << EscapeJson(columns[c].name)
                    << "\",\"type\":\"" << ColumnTypeToString(columns[c].type)
                    << "\",\"nullable\":" << (columns[c].nullable ? "true" : "false")
                    << '}';
            }
        }
        out << "]}";
    }
    out << "],\"page_inventory\":[";
    const std::uint64_t total_pages = disk_manager_.GetPageCount();
    const std::uint64_t pages_to_report = std::min<std::uint64_t>(total_pages, 256);
    for (std::uint64_t page_id = 0; page_id < pages_to_report; ++page_id) {
        if (page_id) out << ',';
        DiskManager::PageData page{};
        std::string magic = "----";
        std::string kind = "Unknown / system page";
        try {
            disk_manager_.ReadPage(static_cast<page_id_t>(page_id), page);
            magic.assign(reinterpret_cast<const char*>(page.data()), 4);
            if (magic == "THDR") kind = "Table directory page";
            else if (magic == "SLPG") kind = "Slotted data page";
            else if (page[0] == 0 && page[1] == 0 && page[2] == 0 && page[3] == 0)
                kind = "Zero / uninitialized page";
        } catch (const std::exception&) {
            kind = "Unreadable page";
        }
        out << "{\"page_id\":" << page_id
            << ",\"magic\":\"" << EscapeJson(magic)
            << "\",\"kind\":\"" << kind
            << "\",\"bytes\":" << Config::kPageSizeBytes << '}';
    }
    out << "],\"page_inventory_truncated\":"
        << (total_pages > pages_to_report ? "true" : "false") << "},";

    out << "\"concurrency\":{";
    out << "\"wait_for_edge_count\":" << wait_edges.size() << ',';
    out << "\"wait_for_edges\":[";
    for (std::size_t i = 0; i < wait_edges.size(); ++i) {
        if (i) out << ',';
        out << "{\"from_txn\":" << wait_edges[i].first
            << ",\"to_txn\":" << wait_edges[i].second << '}';
    }
    out << "],\"waiting_count\":" << lock_manager_.GetWaitingCount() << ',';
    out << "\"lock_table\":[";
    for (std::size_t i = 0; i < lock_table.size(); ++i) {
        if (i) out << ',';
        const auto& entry = lock_table[i];
        out << "{\"page_id\":" << entry.page_id
            << ",\"slot_id\":" << entry.slot_id
            << ",\"exclusive_holder\":" << entry.exclusive_holder
            << ",\"shared_holders\":[";
        for (std::size_t h = 0; h < entry.shared_holders.size(); ++h) {
            if (h) out << ',';
            out << entry.shared_holders[h];
        }
        out << "],\"waiters\":[";
        for (std::size_t w = 0; w < entry.waiters.size(); ++w) {
            if (w) out << ',';
            out << "{\"transaction_id\":" << entry.waiters[w].transaction_id
                << ",\"mode\":\""
                << (entry.waiters[w].mode == LockMode::EXCLUSIVE ? "X" : "S")
                << "\"}";
        }
        out << "]}";
    }
    out << "]},";

    out << "\"wal\":{";
    out << "\"path\":\"" << EscapeJson(wal_path) << "\",";
    out << "\"bytes\":" << FileSizeOrZero(wal_path) << ',';
    out << "\"next_lsn\":" << next_lsn << ',';
    out << "\"persistent_lsn\":";
    if (persistent_lsn == kInvalidLSN) out << "null";
    else out << persistent_lsn;
    out << "},";

    // Startup recovery isn't called in the current server entry point.
    // This explicit state prevents the dashboard from presenting fake success.
    out << "\"recovery\":{\"startup_recovery\":\"not_wired\"}";
    out << '}';
    return out.str();
}

} // namespace minidb
