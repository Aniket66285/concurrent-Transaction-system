
#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace minidb;

int main() {

    const std::string database_file =
        "data/buffer_pool_test.db";

    std::filesystem::remove(database_file);

    // TEST 1: BufferPool creation
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 4);

        assert(pool.GetPoolSize() == 4);
        assert(pool.GetResidentPageCount() == 0);
        assert(pool.GetPinnedFrameCount() == 0);

        std::cout << "BufferPool creation: PASS\n";
    }

    // TEST 2: NewPage allocation
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 2);

        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);

            assert(guard.IsValid());
            assert(guard.GetPageId() == page_id);
            assert(pool.GetPinCount(page_id) == 1);

            const PageGuard& read_guard = guard;

            for (std::size_t i = 0;
                 i < Config::kPageSizeBytes;
                 ++i) {

                assert(read_guard.GetData()[i] == 0);
            }
        }

        assert(pool.GetPinCount(page_id) == 0);

        std::cout << "New page allocation and pinning: PASS\n";
    }

    // TEST 3: All frames pinned
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 1);

        PageGuard first = pool.NewPage();

        assert(first.IsValid());

        PageGuard second = pool.NewPage();

        assert(!second.IsValid());

        assert(pool.GetPinnedFrameCount() == 1);

        first.Drop();

        second = pool.NewPage();

        assert(second.IsValid());

        std::cout << "Pinned frame eviction protection: PASS\n";
    }

    // TEST 4: Fetch existing page
    {
        DiskManager disk(database_file);

        BufferPool pool(disk, 2);

        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);

            assert(guard.IsValid());

            guard.GetData()[0] = 123;
            guard.GetData()[1] = 45;
        }

        {
            PageGuard fetched = pool.FetchPage(page_id);

            assert(fetched.IsValid());
            assert(fetched.GetPageId() == page_id);

            const PageGuard& read_guard = fetched;

            assert(read_guard.GetData()[0] == 123);
            assert(read_guard.GetData()[1] == 45);
        }

        std::cout << "Existing page fetch: PASS\n";
    }

    // TEST 5: Dirty page flushing and WAL callback
    {
        DiskManager disk(database_file);

        std::size_t wal_flush_count = 0;

        BufferPool pool(
            disk,
            2,
            [&wal_flush_count]() {
                ++wal_flush_count;
            }
        );

        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);

            guard.GetData()[0] = 77;
        }

        assert(pool.FlushPage(page_id));
        assert(wal_flush_count == 1);

        DiskManager::PageData persisted{};

        disk.ReadPage(page_id, persisted);

        assert(persisted[0] == 77);

        std::cout << "Dirty page flush and WAL callback: PASS\n";
    }

    // TEST 6: LRU eviction
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 2);

        page_id_t page_a = 0;
        page_id_t page_b = 0;
        page_id_t page_c = 0;

        {
            PageGuard a = pool.NewPage(&page_a);
            a.GetData()[0] = 10;
        }

        {
            PageGuard b = pool.NewPage(&page_b);
            b.GetData()[0] = 20;
        }

        // Refresh A. B should now be the least recently used page.
        {
            PageGuard a = pool.FetchPage(page_a);
            assert(a.IsValid());
        }

        {
            PageGuard c = pool.NewPage(&page_c);

            assert(c.IsValid());
            c.GetData()[0] = 30;
        }

        assert(pool.IsPageResident(page_a));
        assert(!pool.IsPageResident(page_b));
        assert(pool.IsPageResident(page_c));

        // B was dirty when evicted, so its content must be on disk.
        {
            PageGuard b = pool.FetchPage(page_b);

            assert(b.IsValid());

            const PageGuard& read_guard = b;

            assert(read_guard.GetData()[0] == 20);
        }

        std::cout << "LRU eviction and dirty victim persistence: PASS\n";
    }

    // TEST 7: FlushAllPages
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 2);

        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);

            guard.GetData()[0] = 88;
        }

        const std::size_t flushed = pool.FlushAllPages();

        assert(flushed == 1);

        DiskManager::PageData persisted{};

        disk.ReadPage(page_id, persisted);

        assert(persisted[0] == 88);

        std::cout << "FlushAllPages durability: PASS\n";
    }

    // TEST 8: Concurrent access to one page
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 2);

        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);
            guard.GetData()[0] = 0;
        }

        constexpr int kThreadCount = 8;
        constexpr int kIncrementsPerThread = 25;

        std::vector<std::thread> workers;

        for (int i = 0; i < kThreadCount; ++i) {

            workers.emplace_back([&pool, page_id]() {

                for (int j = 0; j < kIncrementsPerThread; ++j) {

                    PageGuard guard = pool.FetchPage(page_id);

                    assert(guard.IsValid());

                    auto& data = guard.GetPageData();

                    ++data[0];
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        {
            PageGuard guard = pool.FetchPage(page_id);

            const PageGuard& read_guard = guard;

            assert(
                read_guard.GetData()[0] ==
                kThreadCount * kIncrementsPerThread
            );
        }

        std::cout << "Concurrent page access: PASS\n";
    }

    // TEST 9: Resident page statistics
    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 3);

        page_id_t first_id = 0;
        page_id_t second_id = 0;

        PageGuard first = pool.NewPage(&first_id);
        PageGuard second = pool.NewPage(&second_id);

        assert(pool.GetResidentPageCount() == 2);
        assert(pool.GetPinnedFrameCount() == 2);

        first.Drop();

        assert(pool.GetPinnedFrameCount() == 1);

        second.Drop();

        assert(pool.GetPinnedFrameCount() == 0);

        std::cout << "BufferPool statistics: PASS\n";
    }

    std::filesystem::remove(database_file);

    std::cout << "\nAll BufferPool tests passed!\n";

    return 0;
}
