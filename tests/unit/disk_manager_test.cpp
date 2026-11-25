
#include "minidb/storage/disk_manager.hpp"
#include "minidb/common/exception.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace minidb;


// Helper to verify StorageException.
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

    const std::string testFile =
        "data/disk_manager_test.db";

    const std::string concurrentFile =
        "data/disk_manager_concurrent_test.db";

    const std::string invalidFile =
        "data/disk_manager_invalid_test.db";


    // Remove old test files.
    std::filesystem::remove(testFile);

    std::filesystem::remove(concurrentFile);

    std::filesystem::remove(invalidFile);


    DiskManager::PageData expectedPage{};


    // ---------------------------------------------
    // TEST 1: Create an empty database file.
    // ---------------------------------------------

    {
        DiskManager manager(testFile);

        assert(std::filesystem::exists(testFile));

        assert(manager.GetPageCount() == 0);

        assert(manager.GetFilePath() == testFile);

        std::cout << "Database file creation: PASS\n";


        // -----------------------------------------
        // TEST 2: Allocate the first page.
        // -----------------------------------------

        const page_id_t page0 =
            manager.AllocatePage();

        assert(page0 == 0);

        assert(manager.GetPageCount() == 1);

        assert(manager.IsPageAllocated(0));

        assert(
            std::filesystem::file_size(testFile) ==
            Config::kPageSizeBytes
        );

        std::cout << "First page allocation: PASS\n";


        // -----------------------------------------
        // TEST 3: Newly allocated page is zeroed.
        // -----------------------------------------

        DiskManager::PageData zeroPage{};

        zeroPage.fill(0xFF);

        manager.ReadPage(page0, zeroPage);

        assert(
            std::all_of(
                zeroPage.begin(),
                zeroPage.end(),
                [](std::uint8_t byte) {
                    return byte == 0;
                }
            )
        );

        std::cout << "Zero-initialized page: PASS\n";


        // -----------------------------------------
        // TEST 4: Write and read an actual 4KB page.
        // -----------------------------------------

        for (std::size_t i = 0;
             i < expectedPage.size();
             ++i) {

            expectedPage[i] =
                static_cast<std::uint8_t>((i * 31) % 256);
        }

        manager.WritePage(page0, expectedPage);

        DiskManager::PageData actualPage{};

        manager.ReadPage(page0, actualPage);

        assert(actualPage == expectedPage);

        std::cout << "4KB page write and read: PASS\n";


        // -----------------------------------------
        // TEST 5: Allocate a second independent page.
        // -----------------------------------------

        const page_id_t page1 =
            manager.AllocatePage();

        assert(page1 == 1);

        assert(manager.GetPageCount() == 2);

        assert(
            std::filesystem::file_size(testFile) ==
            2 * Config::kPageSizeBytes
        );

        std::cout << "Second page allocation: PASS\n";


        // -----------------------------------------
        // TEST 6: Pages are independent.
        // -----------------------------------------

        DiskManager::PageData secondPage{};

        secondPage.fill(0xA5);

        manager.WritePage(page1, secondPage);


        DiskManager::PageData verifyFirstPage{};

        manager.ReadPage(page0, verifyFirstPage);

        assert(verifyFirstPage == expectedPage);


        DiskManager::PageData verifySecondPage{};

        manager.ReadPage(page1, verifySecondPage);

        assert(verifySecondPage == secondPage);

        std::cout << "Independent page storage: PASS\n";


        // -----------------------------------------
        // TEST 7: Reject unallocated page access.
        // -----------------------------------------

        assert(
            ThrowsStorageException([&]() {

                DiskManager::PageData invalidPage{};

                manager.ReadPage(100, invalidPage);
            })
        );


        assert(
            ThrowsStorageException([&]() {

                DiskManager::PageData invalidPage{};

                manager.WritePage(100, invalidPage);
            })
        );

        std::cout << "Unallocated page rejection: PASS\n";


        // -----------------------------------------
        // TEST 8: Flush database changes.
        // -----------------------------------------

        manager.Flush();

        std::cout << "Database fsync: PASS\n";
    }


    // ---------------------------------------------
    // TEST 9: Reopen database and verify persistence.
    // ---------------------------------------------

    {
        DiskManager reopenedManager(testFile);

        assert(reopenedManager.GetPageCount() == 2);

        DiskManager::PageData recoveredPage{};

        reopenedManager.ReadPage(0, recoveredPage);

        assert(recoveredPage == expectedPage);


        DiskManager::PageData recoveredSecondPage{};

        reopenedManager.ReadPage(1, recoveredSecondPage);

        assert(
            std::all_of(
                recoveredSecondPage.begin(),
                recoveredSecondPage.end(),
                [](std::uint8_t byte) {
                    return byte == 0xA5;
                }
            )
        );

        std::cout << "Database reopen and persistence: PASS\n";
    }


    // ---------------------------------------------
    // TEST 10: Concurrent page allocation.
    // ---------------------------------------------

    {
        DiskManager manager(concurrentFile);

        constexpr int kThreadCount = 4;

        constexpr int kPagesPerThread = 25;

        std::vector<std::thread> workers;

        std::vector<page_id_t> allocatedIds;

        std::mutex idsMutex;


        for (int i = 0; i < kThreadCount; ++i) {

            workers.emplace_back([&]() {

                std::vector<page_id_t> localIds;

                for (int j = 0;
                     j < kPagesPerThread;
                     ++j) {

                    localIds.push_back(
                        manager.AllocatePage()
                    );
                }

                std::lock_guard<std::mutex> lock(idsMutex);

                allocatedIds.insert(
                    allocatedIds.end(),
                    localIds.begin(),
                    localIds.end()
                );
            });
        }


        for (std::thread& worker : workers) {
            worker.join();
        }


        constexpr std::size_t expectedCount =
            kThreadCount * kPagesPerThread;

        assert(allocatedIds.size() == expectedCount);

        assert(manager.GetPageCount() == expectedCount);


        // All allocated page IDs must be unique.
        const std::set<page_id_t> uniqueIds(
            allocatedIds.begin(),
            allocatedIds.end()
        );

        assert(uniqueIds.size() == expectedCount);


        // Every ID from 0 to 99 must exist.
        for (page_id_t id = 0;
             id < expectedCount;
             ++id) {

            assert(manager.IsPageAllocated(id));
        }

        std::cout << "Concurrent page allocation: PASS\n";

        std::cout << "Unique page ID allocation: PASS\n";


        // Verify the final file has exactly 100 pages.
        assert(
            std::filesystem::file_size(concurrentFile) ==
            expectedCount * Config::kPageSizeBytes
        );

        std::cout << "Concurrent database file size: PASS\n";
    }


    // ---------------------------------------------
    // TEST 11: Reject a misaligned database file.
    // ---------------------------------------------

    {
        std::ofstream badFile(
            invalidFile,
            std::ios::binary
        );

        assert(badFile.is_open());

        const char invalidBytes[] = {
            'B', 'A', 'D'
        };

        badFile.write(
            invalidBytes,
            sizeof(invalidBytes)
        );

        assert(badFile.good());

        badFile.close();


        assert(
            ThrowsStorageException([&]() {

                DiskManager invalidManager(invalidFile);
            })
        );

        std::cout << "Invalid file alignment rejection: PASS\n";
    }


    // ---------------------------------------------
    // TEST 12: Reject an empty file path.
    // ---------------------------------------------

    assert(
        ThrowsStorageException([]() {

            DiskManager invalidManager("");
        })
    );

    std::cout << "Empty database path rejection: PASS\n";


    // Remove test databases.
    std::filesystem::remove(testFile);

    std::filesystem::remove(concurrentFile);

    std::filesystem::remove(invalidFile);


    std::cout << "\nAll DiskManager tests passed!\n";

    return 0;
}
