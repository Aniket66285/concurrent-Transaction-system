#include "minidb/server/worker_pool.hpp"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
int main() {
    minidb::WorkerPool pool(3, 8);
    auto tids = pool.GetWorkerLinuxThreadIds();
    for (int tries=0; tries<50; ++tries) {
        bool ready = true;
        for (auto tid : tids) if (tid <= 0) ready = false;
        if (ready) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        tids = pool.GetWorkerLinuxThreadIds();
    }
    assert(tids.size()==3);
    for (auto tid : tids) assert(tid > 0);
    auto f = pool.Submit([] { return 42; });
    assert(f.get()==42);
    for (int tries=0; tries<100 && pool.GetCompletedTaskCount()!=1; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(pool.GetCompletedTaskCount()==1);
    assert(pool.GetQueueCapacity()==8);
    pool.Shutdown();
    std::cout << "WorkerPool Linux TID and counters: PASS\n";
}
