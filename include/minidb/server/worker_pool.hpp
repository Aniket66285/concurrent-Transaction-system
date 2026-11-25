#pragma once

#include "minidb/common/config.hpp"
#include "minidb/server/bounded_queue.hpp"

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/types.h>

namespace minidb {

class WorkerPool {
public:
    explicit WorkerPool(
        std::size_t worker_count = Config::kWorkerThreads,
        std::size_t queue_capacity = Config::kRequestQueueCapacity
    );
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    template <typename F, typename... Args>
    auto Submit(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using Result = std::invoke_result_t<F, Args...>;
        auto bound = std::bind(
            std::forward<F>(function), std::forward<Args>(args)...
        );
        auto task = std::make_shared<std::packaged_task<Result()>>(
            [this, bound = std::move(bound)]() mutable -> Result {
                try {
                    if constexpr (std::is_void_v<Result>) {
                        std::invoke(bound);
                        completed_tasks_.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        Result value = std::invoke(bound);
                        completed_tasks_.fetch_add(1, std::memory_order_relaxed);
                        return value;
                    }
                } catch (...) {
                    completed_tasks_.fetch_add(1, std::memory_order_relaxed);
                    throw;
                }
            }
        );
        std::future<Result> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (stopping_) {
                throw std::runtime_error("Cannot submit task: WorkerPool is shutting down");
            }
            queue_.Push([task]() { (*task)(); });
        }
        return result;
    }

    void Shutdown();
    std::size_t GetWorkerCount() const noexcept;
    std::size_t GetQueueSize() const;
    std::size_t GetQueueCapacity() const noexcept;
    std::size_t GetActiveTaskCount() const noexcept;
    std::size_t GetCompletedTaskCount() const noexcept;
    std::vector<std::thread::id> GetWorkerThreadIds() const;
    std::vector<pid_t> GetWorkerLinuxThreadIds() const;
    std::vector<bool> GetWorkerBusyStates() const;

private:
    void WorkerLoop(std::size_t worker_index);

    BoundedQueue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::vector<pid_t> worker_linux_tids_;
    std::vector<bool> worker_busy_;
    std::atomic<std::size_t> active_tasks_{0};
    std::atomic<std::size_t> completed_tasks_{0};
    mutable std::mutex state_mutex_;
    mutable std::mutex worker_info_mutex_;
    std::condition_variable worker_info_cv_;
    std::mutex shutdown_mutex_;
    bool stopping_{false};
};

} // namespace minidb
