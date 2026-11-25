#include "minidb/server/bounded_queue.hpp"
#include "minidb/server/worker_pool.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace minidb;

int main() {
    // Test 1: Bounded queue.
    BoundedQueue<int> queue(8);

    for (int i = 1; i <= 8; ++i) {
        queue.Push(i);
    }

    assert(queue.GetSize() == 8);
    assert(queue.GetCapacity() == 8);

    for (int i = 1; i <= 8; ++i) {
        assert(queue.Pop() == i);
    }

    assert(queue.GetSize() == 0);
    std::cout << "Bounded Queue capacity and FIFO: PASS\n";

    // Test 2: Three worker threads.
    WorkerPool pool(3, 8);

    assert(pool.GetWorkerCount() == 3);

    const auto thread_ids = pool.GetWorkerThreadIds();
    assert(thread_ids.size() == 3);
    assert(thread_ids[0] != thread_ids[1]);
    assert(thread_ids[1] != thread_ids[2]);
    assert(thread_ids[0] != thread_ids[2]);

    std::cout << "Three independent worker threads: PASS\n";

    // Test 3: Submit tasks and get their results.
    std::vector<std::future<int>> futures;

    for (int i = 1; i <= 10; ++i) {
        futures.push_back(pool.Submit([i]() {
            return i * i;
        }));
    }

    int total = 0;

    for (int i = 1; i <= 10; ++i) {
        assert(futures[i - 1].get() == i * i);
        total += i * i;
    }

    assert(total == 385);
    std::cout << "Task execution and future results: PASS\n";

    // Test 4: Task exception reaches its future.
    auto failed_task = pool.Submit([]() -> int {
        throw std::runtime_error("Intentional test exception");
    });

    bool exception_received = false;

    try {
        (void)failed_task.get();
    } catch (const std::runtime_error&) {
        exception_received = true;
    }

    assert(exception_received);
    std::cout << "Task exception propagation: PASS\n";

    // Test 5: Task completion counter.
    assert(pool.GetCompletedTaskCount() == 11);
    std::cout << "Completed task counter: PASS\n";

    // Test 6: Graceful shutdown.
    pool.Shutdown();

    bool rejected = false;

    try {
        auto task = pool.Submit([]() {
            return 1;
        });
        (void)task;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    assert(rejected);
    std::cout << "Graceful shutdown and task rejection: PASS\n";

    std::cout << "\nAll Worker Pool tests passed!\n";
    return 0;
}