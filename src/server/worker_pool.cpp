#include "minidb/server/worker_pool.hpp"

#include <stdexcept>

#include <sys/syscall.h>
#include <unistd.h>

namespace minidb {

WorkerPool::WorkerPool(std::size_t worker_count, std::size_t queue_capacity)
    : queue_(queue_capacity), worker_linux_tids_(worker_count, 0),
      worker_busy_(worker_count, false) {
    if (worker_count == 0) {
        throw std::invalid_argument("WorkerPool requires at least one worker");
    }
    try {
        for (std::size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this, i]() { WorkerLoop(i); });
        }
        // Expose valid Linux TIDs as soon as construction returns.
        std::unique_lock<std::mutex> info_lock(worker_info_mutex_);
        worker_info_cv_.wait(info_lock, [this]() {
            for (const auto tid : worker_linux_tids_) {
                if (tid <= 0) return false;
            }
            return true;
        });
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            stopping_ = true;
        }
        for (std::size_t i = 0; i < workers_.size(); ++i) {
            queue_.Push(std::function<void()>{});
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        throw;
    }
}

WorkerPool::~WorkerPool() {
    try {
        Shutdown();
    } catch (...) {
        // A destructor cannot throw. Calling Shutdown from a worker is unsupported.
    }
}

void WorkerPool::WorkerLoop(std::size_t worker_index) {
    const auto tid = static_cast<pid_t>(syscall(SYS_gettid));
    {
        std::lock_guard<std::mutex> lock(worker_info_mutex_);
        if (worker_index < worker_linux_tids_.size()) worker_linux_tids_[worker_index] = tid;
    }
    worker_info_cv_.notify_all();
    while (true) {
        std::function<void()> task = queue_.Pop();
        if (!task) break;
        {
            std::lock_guard<std::mutex> lock(worker_info_mutex_);
            worker_busy_[worker_index] = true;
        }
        active_tasks_.fetch_add(1, std::memory_order_relaxed);
        try {
            task();
        } catch (...) {
            // A packaged_task normally stores exceptions in its future.
            // Keep the worker alive for unexpected wrapper exceptions too.
        }
        active_tasks_.fetch_sub(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(worker_info_mutex_);
            worker_busy_[worker_index] = false;
        }
    }
}

void WorkerPool::Shutdown() {
    for (const auto& worker : workers_) {
        if (worker.joinable() && worker.get_id() == std::this_thread::get_id()) {
            throw std::logic_error("WorkerPool::Shutdown cannot run inside a worker");
        }
    }
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    // Existing FIFO tasks are processed before the shutdown markers.
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        queue_.Push(std::function<void()>{});
    }
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

std::size_t WorkerPool::GetWorkerCount() const noexcept { return workers_.size(); }
std::size_t WorkerPool::GetQueueSize() const { return queue_.GetSize(); }
std::size_t WorkerPool::GetQueueCapacity() const noexcept { return queue_.GetCapacity(); }
std::size_t WorkerPool::GetActiveTaskCount() const noexcept {
    return active_tasks_.load(std::memory_order_relaxed);
}
std::size_t WorkerPool::GetCompletedTaskCount() const noexcept {
    return completed_tasks_.load(std::memory_order_relaxed);
}

std::vector<std::thread::id> WorkerPool::GetWorkerThreadIds() const {
    std::vector<std::thread::id> ids;
    ids.reserve(workers_.size());
    for (const auto& worker : workers_) ids.push_back(worker.get_id());
    return ids;
}

std::vector<pid_t> WorkerPool::GetWorkerLinuxThreadIds() const {
    std::lock_guard<std::mutex> lock(worker_info_mutex_);
    return worker_linux_tids_;
}

std::vector<bool> WorkerPool::GetWorkerBusyStates() const {
    std::lock_guard<std::mutex> lock(worker_info_mutex_);
    return worker_busy_;
}

} // namespace minidb
