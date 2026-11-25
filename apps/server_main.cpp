#include "minidb/common/config.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/catalog/catalog.hpp"
#include "minidb/sql/sql_engine.hpp"
#include "minidb/server/api.hpp"
#include "minidb/server/http_server.hpp"
#include "minidb/server/worker_pool.hpp"
#include "minidb/observability/runtime_metrics.hpp"
#include "minidb/os/scheduler_experiment.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <thread>
#include <vector>

namespace {
volatile std::sig_atomic_t keep_running = 1;
void HandleSignal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) keep_running = 0;
}
} // namespace

int main() {
    try {
        std::filesystem::create_directories("data");
        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);

        std::cout << "====================================\n"
                  << "        MiniDB Database Server       \n"
                  << "====================================\n";

        minidb::DiskManager disk_manager(minidb::Config::kDatabaseFile);
        minidb::LogManager log_manager(minidb::Config::kWalFile);
        minidb::LockManager lock_manager;

        minidb::BufferPool buffer_pool(
            disk_manager,
            minidb::Config::kBufferPoolFrames,
            [&log_manager]() { log_manager.Flush(); }
        );

        minidb::TxnManager txn_manager(log_manager, &lock_manager);
        minidb::Catalog catalog;
        minidb::SqlEngine sql_engine(catalog, buffer_pool, txn_manager);

        minidb::WorkerPool workers(
            minidb::Config::kWorkerThreads,
            minidb::Config::kRequestQueueCapacity
        );

        minidb::RuntimeMetrics metrics(
            disk_manager, buffer_pool, log_manager, lock_manager,
            txn_manager, catalog, sql_engine, workers
        );

        minidb::SqlApi api(sql_engine);
        minidb::SchedulerExperiment scheduler_experiment(workers);
        api.SetMetricsProvider([&metrics]() { return metrics.ToJson(); });
        api.SetSchedulerExperiment(&scheduler_experiment);

        std::uint16_t server_port = 8080;
        if (const char* configured_port = std::getenv("MINIDB_PORT")) {
            try {
                const auto parsed = std::stoul(configured_port);
                if (parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max()) {
                    throw std::invalid_argument("MINIDB_PORT must be between 1 and 65535");
                }
                server_port = static_cast<std::uint16_t>(parsed);
            } catch (const std::exception&) {
                throw std::invalid_argument("MINIDB_PORT must be a valid integer between 1 and 65535");
            }
        }
        minidb::HttpServer server(api, workers, "0.0.0.0", server_port);
        server.Start();

        std::cout << "\nServer started successfully.\n"
                  << "Address: http://127.0.0.1:" << server.GetBoundPort() << '\n'
                  << "Health:   http://127.0.0.1:" << server.GetBoundPort()
                  << "/api/health\n"
                  << "Metrics:  http://127.0.0.1:" << server.GetBoundPort()
                  << "/api/metrics\n\n"
                  << "Worker threads: " << workers.GetWorkerCount() << '\n'
                  << "Queue capacity: " << workers.GetQueueCapacity() << '\n'
                  << "Buffer frames: " << buffer_pool.GetPoolSize() << '\n'
                  << "\nPress Ctrl+C to stop the server.\n";

        while (keep_running != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "\nShutting down MiniDB...\n";
        server.Stop();
        workers.Shutdown();

        const auto session_ids = sql_engine.GetSessionIds();
        for (const auto session_id : session_ids) {
            sql_engine.CloseSession(session_id);
        }

        const auto flushed_pages = buffer_pool.FlushAllPages();
        log_manager.Flush();
        disk_manager.Flush();

        std::cout << "Sessions closed: " << session_ids.size() << '\n'
                  << "Pages flushed: " << flushed_pages << '\n'
                  << "MiniDB stopped successfully.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nMiniDB startup/runtime error: " << error.what() << '\n';
        return 1;
    }
}
