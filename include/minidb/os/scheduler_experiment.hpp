#pragma once

#include "minidb/server/worker_pool.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace minidb {

struct SchedulerThreadResult {
    std::size_t worker_slot{0};
    pid_t tid{-1};
    std::string policy{"unknown"};
    int priority{0};
    int last_cpu_before{-1};
    int last_cpu_after{-1};
    std::uint64_t elapsed_ms{0};
    std::uint64_t cpu_time_delta_ms{0};
    std::uint64_t voluntary_context_switch_delta{0};
    std::uint64_t involuntary_context_switch_delta{0};
    std::uint64_t runqueue_wait_delta_ms{0};
    std::uint64_t scheduler_runtime_delta_ms{0};
    std::uint64_t timeslice_delta{0};
    bool schedstat_readable{false};
    std::uint64_t checksum{0};
};

struct SchedulerExperimentSnapshot {
    std::uint64_t job_id{0};
    bool running{false};
    std::size_t requested_threads{0};
    std::size_t completed_threads{0};
    std::uint64_t duration_ms{0};
    std::string status{"idle"};
    std::string error;
    std::vector<SchedulerThreadResult> results;
};

// Runs CPU-bound tasks on the real MiniDB WorkerPool threads and measures
// Linux scheduler/procfs counters before and after each task.
class SchedulerExperiment {
public:
    explicit SchedulerExperiment(WorkerPool& worker_pool);
    ~SchedulerExperiment();

    SchedulerExperiment(const SchedulerExperiment&) = delete;
    SchedulerExperiment& operator=(const SchedulerExperiment&) = delete;

    std::uint64_t Start(std::size_t thread_count, std::uint64_t duration_ms);
    SchedulerExperimentSnapshot GetSnapshot() const;
    std::string ToJson() const;

private:
    struct Job;
    void RunJob(const std::shared_ptr<Job>& job);

    WorkerPool& worker_pool_;
    mutable std::mutex mutex_;
    std::mutex start_mutex_;
    std::shared_ptr<Job> current_job_;
    std::thread coordinator_;
    std::uint64_t next_job_id_{1};
};

} // namespace minidb
