#include "minidb/os/scheduler_experiment.hpp"

#include "minidb/os/procfs_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <chrono>
#include <atomic>

namespace minidb {
namespace {

struct SchedStat {
    std::uint64_t runtime_ns{0};
    std::uint64_t runqueue_wait_ns{0};
    std::uint64_t timeslices{0};
    bool readable{false};
};

SchedStat ReadSchedStat(pid_t tid) {
    SchedStat result;
    std::ifstream input("/proc/self/task/" + std::to_string(tid) + "/schedstat");
    if (!(input >> result.runtime_ns >> result.runqueue_wait_ns >> result.timeslices)) {
        return result;
    }
    result.readable = true;
    return result;
}

std::string PolicyName(int policy) {
    switch (policy) {
        case SCHED_OTHER: return "SCHED_OTHER";
#ifdef SCHED_BATCH
        case SCHED_BATCH: return "SCHED_BATCH";
#endif
#ifdef SCHED_IDLE
        case SCHED_IDLE: return "SCHED_IDLE";
#endif
#ifdef SCHED_FIFO
        case SCHED_FIFO: return "SCHED_FIFO";
#endif
#ifdef SCHED_RR
        case SCHED_RR: return "SCHED_RR";
#endif
#ifdef SCHED_DEADLINE
        case SCHED_DEADLINE: return "SCHED_DEADLINE";
#endif
        default: return "UNKNOWN(" + std::to_string(policy) + ")";
    }
}

std::uint64_t Delta(std::uint64_t after, std::uint64_t before) {
    return after >= before ? after - before : 0;
}

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) out << "?";
                else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

} // namespace

struct SchedulerExperiment::Job {
    SchedulerExperimentSnapshot snapshot;
};

SchedulerExperiment::SchedulerExperiment(WorkerPool& worker_pool)
    : worker_pool_(worker_pool) {}

SchedulerExperiment::~SchedulerExperiment() {
    std::lock_guard<std::mutex> start_lock(start_mutex_);
    if (coordinator_.joinable()) coordinator_.join();
}

std::uint64_t SchedulerExperiment::Start(
    std::size_t thread_count,
    std::uint64_t duration_ms
) {
    if (thread_count == 0 || thread_count > worker_pool_.GetWorkerCount()) {
        throw std::invalid_argument("Experiment thread count must be between 1 and the WorkerPool size");
    }
    if (duration_ms < 250 || duration_ms > 10000) {
        throw std::invalid_argument("Experiment duration must be between 250 and 10000 milliseconds");
    }

    std::lock_guard<std::mutex> start_lock(start_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (current_job_ && current_job_->snapshot.running) {
            throw std::runtime_error("A scheduler experiment is already running");
        }
    }
    if (coordinator_.joinable()) coordinator_.join();

    auto job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->snapshot.job_id = next_job_id_++;
        job->snapshot.running = true;
        job->snapshot.requested_threads = thread_count;
        job->snapshot.duration_ms = duration_ms;
        job->snapshot.status = "running";
        current_job_ = job;
    }

    coordinator_ = std::thread(&SchedulerExperiment::RunJob, this, job);
    return job->snapshot.job_id;
}

void SchedulerExperiment::RunJob(const std::shared_ptr<Job>& job) {
    struct Gate {
        std::mutex mutex;
        std::condition_variable condition;
        bool open{false};
    };
    auto gate = std::make_shared<Gate>();
    std::vector<std::future<SchedulerThreadResult>> futures;
    futures.reserve(job->snapshot.requested_threads);

    try {
        for (std::size_t slot = 0; slot < job->snapshot.requested_threads; ++slot) {
            futures.push_back(worker_pool_.Submit([gate, slot, duration = job->snapshot.duration_ms]() {
                SchedulerThreadResult result;
                result.worker_slot = slot;
                result.tid = static_cast<pid_t>(syscall(SYS_gettid));

                int policy = SCHED_OTHER;
                sched_param parameters{};
                if (pthread_getschedparam(pthread_self(), &policy, &parameters) == 0) {
                    result.policy = PolicyName(policy);
                    result.priority = parameters.sched_priority;
                }
                errno = 0;
                const int nice_value = getpriority(PRIO_PROCESS, 0);
                if (errno == 0) result.priority = nice_value;

                const auto before = ProcfsReader::ReadThread(result.tid);
                const auto sched_before = ReadSchedStat(result.tid);

                {
                    std::unique_lock<std::mutex> gate_lock(gate->mutex);
                    gate->condition.wait(gate_lock, [&]() { return gate->open; });
                }

                const auto start = std::chrono::steady_clock::now();
                volatile std::uint64_t checksum = 0x9E3779B97F4A7C15ULL + slot;
                while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(duration)) {
                    // Deliberately CPU-bound deterministic work; no sleeps/yields.
                    for (std::uint64_t i = 1; i <= 4096; ++i) {
                        checksum = (checksum ^ (i + checksum)) * 0xBF58476D1CE4E5B9ULL;
                        checksum ^= (checksum >> 27);
                    }
                }
                const auto finish = std::chrono::steady_clock::now();
                result.elapsed_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count()
                );
                result.checksum = checksum;

                const auto after = ProcfsReader::ReadThread(result.tid);
                const auto sched_after = ReadSchedStat(result.tid);
                result.last_cpu_before = before.last_cpu_core;
                result.last_cpu_after = after.last_cpu_core;
                result.cpu_time_delta_ms = Delta(after.cpu_time_ms, before.cpu_time_ms);
                result.voluntary_context_switch_delta = Delta(
                    after.voluntary_context_switches, before.voluntary_context_switches
                );
                result.involuntary_context_switch_delta = Delta(
                    after.nonvoluntary_context_switches, before.nonvoluntary_context_switches
                );
                result.schedstat_readable = sched_before.readable && sched_after.readable;
                if (result.schedstat_readable) {
                    result.scheduler_runtime_delta_ms = Delta(
                        sched_after.runtime_ns, sched_before.runtime_ns
                    ) / 1000000ULL;
                    result.runqueue_wait_delta_ms = Delta(
                        sched_after.runqueue_wait_ns, sched_before.runqueue_wait_ns
                    ) / 1000000ULL;
                    result.timeslice_delta = Delta(
                        sched_after.timeslices, sched_before.timeslices
                    );
                }
                return result;
            }));
        }

        // All worker tasks are now queued or waiting at the gate. Release together.
        {
            std::lock_guard<std::mutex> gate_lock(gate->mutex);
            gate->open = true;
        }
        gate->condition.notify_all();

        std::vector<SchedulerThreadResult> results;
        results.reserve(futures.size());
        for (auto& future : futures) results.push_back(future.get());
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
            return a.worker_slot < b.worker_slot;
        });

        std::lock_guard<std::mutex> lock(mutex_);
        job->snapshot.results = std::move(results);
        job->snapshot.completed_threads = job->snapshot.results.size();
        job->snapshot.running = false;
        job->snapshot.status = "completed";
    } catch (const std::exception& error) {
        {
            std::lock_guard<std::mutex> gate_lock(gate->mutex);
            gate->open = true;
        }
        gate->condition.notify_all();
        for (auto& future : futures) {
            try { (void)future.get(); } catch (...) {}
        }
        std::lock_guard<std::mutex> lock(mutex_);
        job->snapshot.running = false;
        job->snapshot.status = "failed";
        job->snapshot.error = error.what();
    } catch (...) {
        {
            std::lock_guard<std::mutex> gate_lock(gate->mutex);
            gate->open = true;
        }
        gate->condition.notify_all();
        for (auto& future : futures) {
            try { (void)future.get(); } catch (...) {}
        }
        std::lock_guard<std::mutex> lock(mutex_);
        job->snapshot.running = false;
        job->snapshot.status = "failed";
        job->snapshot.error = "Unknown scheduler experiment error";
    }
}

SchedulerExperimentSnapshot SchedulerExperiment::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_job_) return {};
    return current_job_->snapshot;
}

std::string SchedulerExperiment::ToJson() const {
    const auto snapshot = GetSnapshot();
    std::ostringstream out;
    out << "{\"job_id\":" << snapshot.job_id
        << ",\"running\":" << (snapshot.running ? "true" : "false")
        << ",\"status\":\"" << EscapeJson(snapshot.status) << "\""
        << ",\"requested_threads\":" << snapshot.requested_threads
        << ",\"completed_threads\":" << snapshot.completed_threads
        << ",\"duration_ms\":" << snapshot.duration_ms
        << ",\"error\":\"" << EscapeJson(snapshot.error) << "\",\"results\":[";
    for (std::size_t i = 0; i < snapshot.results.size(); ++i) {
        if (i) out << ',';
        const auto& r = snapshot.results[i];
        out << "{\"worker_slot\":" << r.worker_slot
            << ",\"tid\":" << r.tid
            << ",\"policy\":\"" << EscapeJson(r.policy) << "\""
            << ",\"priority\":" << r.priority
            << ",\"last_cpu_before\":" << r.last_cpu_before
            << ",\"last_cpu_after\":" << r.last_cpu_after
            << ",\"elapsed_ms\":" << r.elapsed_ms
            << ",\"cpu_time_delta_ms\":" << r.cpu_time_delta_ms
            << ",\"voluntary_context_switch_delta\":" << r.voluntary_context_switch_delta
            << ",\"involuntary_context_switch_delta\":" << r.involuntary_context_switch_delta
            << ",\"runqueue_wait_delta_ms\":" << r.runqueue_wait_delta_ms
            << ",\"scheduler_runtime_delta_ms\":" << r.scheduler_runtime_delta_ms
            << ",\"timeslice_delta\":" << r.timeslice_delta
            << ",\"schedstat_readable\":" << (r.schedstat_readable ? "true" : "false")
            << ",\"checksum\":" << r.checksum << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace minidb
