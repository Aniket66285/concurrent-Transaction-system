
#include "minidb/buffer/lru_replacer.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

using namespace minidb;

bool ThrowsStorageException(
    const std::function<void()>& operation
) {
    try {
        operation();
    }
    catch (const StorageException&) {
        return true;
    }

    return false;
}

int main() {

    // TEST 1: Empty replacer
    LRUReplacer emptyReplacer(4);

    assert(emptyReplacer.Size() == 0);

    std::cout << "Empty replacer: PASS\n";


    // TEST 2: Unpin adds frame
    LRUReplacer replacer1(4);

    replacer1.Unpin(2);

    assert(replacer1.Size() == 1);
    assert(replacer1.Contains(2));

    std::cout << "Unpin adds frame: PASS\n";


    // TEST 3: LRU ordering
    LRUReplacer replacer2(4);

    replacer2.Unpin(0);
    replacer2.Unpin(1);
    replacer2.Unpin(2);

    frame_id_t victim = 99;

    assert(replacer2.Victim(&victim));
    assert(victim == 0);

    assert(replacer2.Victim(&victim));
    assert(victim == 1);

    assert(replacer2.Victim(&victim));
    assert(victim == 2);

    assert(replacer2.Size() == 0);

    std::cout << "Least recently used ordering: PASS\n";


    // TEST 4: Pin removes frame from candidates
    LRUReplacer replacer3(4);

    replacer3.Unpin(0);
    replacer3.Unpin(1);

    replacer3.Pin(0);

    assert(!replacer3.Contains(0));
    assert(replacer3.Size() == 1);

    assert(replacer3.Victim(&victim));
    assert(victim == 1);

    std::cout << "Pin removes eviction candidate: PASS\n";


    // TEST 5: Re-accessed frame becomes MRU
    LRUReplacer replacer4(4);

    replacer4.Unpin(0);
    replacer4.Unpin(1);

    // Simulate access to frame 0.
    replacer4.Pin(0);
    replacer4.Unpin(0);

    // Frame 1 is now older than frame 0.
    assert(replacer4.Victim(&victim));
    assert(victim == 1);

    assert(replacer4.Victim(&victim));
    assert(victim == 0);

    std::cout << "Recently used frame becomes MRU: PASS\n";


    // TEST 6: Duplicate Unpin
    LRUReplacer replacer5(4);

    replacer5.Unpin(0);
    replacer5.Unpin(1);

    // Duplicate Unpin must not refresh frame 0.
    replacer5.Unpin(0);

    assert(replacer5.Size() == 2);

    assert(replacer5.Victim(&victim));
    assert(victim == 0);

    std::cout << "Duplicate Unpin protection: PASS\n";


    // TEST 7: Victim removes selected frame
    LRUReplacer replacer6(4);

    replacer6.Unpin(3);

    assert(replacer6.Victim(&victim));
    assert(victim == 3);

    assert(!replacer6.Contains(3));
    assert(replacer6.Size() == 0);

    std::cout << "Victim removes selected frame: PASS\n";


    // TEST 8: Pin on absent frame
    LRUReplacer replacer7(4);

    replacer7.Pin(2);

    assert(replacer7.Size() == 0);

    std::cout << "Pin on non-candidate frame: PASS\n";


    // TEST 9: Empty victim and null pointer
    LRUReplacer replacer8(4);

    assert(!replacer8.Victim(&victim));
    assert(!replacer8.Victim(nullptr));

    replacer8.Unpin(1);

    assert(!replacer8.Victim(nullptr));
    assert(replacer8.Size() == 1);

    std::cout << "Empty and null victim handling: PASS\n";


    // TEST 10: Invalid frame ID
    LRUReplacer replacer9(4);

    assert(ThrowsStorageException([&]() {
        replacer9.Unpin(4);
    }));

    assert(ThrowsStorageException([&]() {
        replacer9.Pin(4);
    }));

    assert(ThrowsStorageException([&]() {
        replacer9.Contains(4);
    }));

    std::cout << "Invalid frame ID rejection: PASS\n";


    // TEST 11: Zero capacity
    assert(ThrowsStorageException([]() {
        LRUReplacer invalidReplacer(0);
    }));

    std::cout << "Zero capacity rejection: PASS\n";


    // TEST 12: Last valid frame ID
    LRUReplacer replacer10(4);

    replacer10.Unpin(3);

    assert(replacer10.Contains(3));
    assert(replacer10.Victim(&victim));
    assert(victim == 3);

    std::cout << "Last valid frame ID handling: PASS\n";


    // TEST 13: Concurrent Unpin operations
    constexpr frame_id_t kThreadCount = 64;

    LRUReplacer concurrentReplacer(kThreadCount);

    std::vector<std::thread> threads;

    for (frame_id_t i = 0; i < kThreadCount; ++i) {

        threads.emplace_back([&concurrentReplacer, i]() {
            concurrentReplacer.Unpin(i);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    assert(concurrentReplacer.Size() == kThreadCount);

    std::set<frame_id_t> selectedFrames;

    while (concurrentReplacer.Victim(&victim)) {
        selectedFrames.insert(victim);
    }

    assert(selectedFrames.size() == kThreadCount);

    for (frame_id_t i = 0; i < kThreadCount; ++i) {
        assert(selectedFrames.find(i) != selectedFrames.end());
    }

    assert(concurrentReplacer.Size() == 0);

    std::cout << "Concurrent Unpin and thread safety: PASS\n";


    std::cout << "\nAll LRUReplacer tests passed!\n";

    return 0;
}
