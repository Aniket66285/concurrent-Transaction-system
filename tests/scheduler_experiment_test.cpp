#include "minidb/os/scheduler_experiment.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>

int main() {
    minidb::WorkerPool workers(3, 8);
    minidb::SchedulerExperiment experiment(workers);

    const auto job_id = experiment.Start(3, 300);
    assert(job_id > 0);

    minidb::SchedulerExperimentSnapshot snapshot;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        snapshot = experiment.GetSnapshot();
    } while (snapshot.running && std::chrono::steady_clock::now() < deadline);

    assert(!snapshot.running);
    assert(snapshot.status == "completed");
    assert(snapshot.requested_threads == 3);
    assert(snapshot.completed_threads == 3);
    assert(snapshot.results.size() == 3);

    std::set<pid_t> tids;
    for (const auto& result : snapshot.results) {
        assert(result.tid > 0);
        assert(result.elapsed_ms >= 250);
        assert(!result.policy.empty());
        tids.insert(result.tid);
    }
    assert(tids.size() == 3);

    const std::string json = experiment.ToJson();
    assert(json.find("\"status\":\"completed\"") != std::string::npos);
    assert(json.find("\"schedstat_readable\"") != std::string::npos);

    workers.Shutdown();
    std::cout << "CPU-bound jobs ran on three distinct WorkerPool Linux TIDs: PASS\n";
    std::cout << "Linux scheduling and procfs deltas collected: PASS\n";
    std::cout << "Scheduler experiment JSON snapshot: PASS\n";
    return 0;
}
