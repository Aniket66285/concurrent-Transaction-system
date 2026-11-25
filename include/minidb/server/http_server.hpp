#pragma once

#include "minidb/server/api.hpp"
#include "minidb/server/worker_pool.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace minidb {

class HttpServer {
public:
    HttpServer(
        SqlApi& api,
        WorkerPool& workers,
        const std::string& host = "0.0.0.0",
        std::uint16_t port = 8080
    );

    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void Start();
    void Stop();

    bool IsRunning() const noexcept;
    std::uint16_t GetBoundPort() const;

private:
    void AcceptLoop(int server_socket);
    void HandleClient(int client_socket);

    SqlApi& api_;
    WorkerPool& workers_;

    const std::string host_;
    const std::uint16_t requested_port_;

    mutable std::mutex lifecycle_mutex_;

    std::atomic<bool> running_{false};
    int listen_socket_{-1};
    std::uint16_t bound_port_{0};

    std::thread accept_thread_;
};

} // namespace minidb