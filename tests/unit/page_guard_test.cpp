
#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/buffer/page_guard.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

using namespace minidb;

int main() {

    const std::string database_file =
        "data/page_guard_test.db";

    std::filesystem::remove(database_file);

    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 3);

        // TEST 1: Default invalid guard
        PageGuard empty;

        assert(!empty.IsValid());
        assert(empty.GetData() == nullptr);

        std::cout << "Default invalid guard: PASS\n";


        // TEST 2: Guard owns a page pin
        page_id_t page_id = 0;

        {
            PageGuard guard = pool.NewPage(&page_id);

            assert(guard.IsValid());
            assert(pool.GetPinCount(page_id) == 1);
        }

        assert(pool.GetPinCount(page_id) == 0);

        std::cout << "Automatic page unpin: PASS\n";


        // TEST 3: Writable access marks dirty
        {
            PageGuard guard = pool.FetchPage(page_id);

            assert(!guard.IsDirty());

            guard.GetData()[0] = 42;

            assert(guard.IsDirty());
        }

        std::cout << "Writable access marks dirty: PASS\n";


        // TEST 4: Const access does not mark dirty
        {
            PageGuard guard = pool.FetchPage(page_id);

            assert(!guard.IsDirty());

            const PageGuard& read_guard = guard;

            (void)read_guard.GetData();

            assert(!guard.IsDirty());
        }

        std::cout << "Const access does not mark dirty: PASS\n";


        // TEST 5: Move constructor transfers ownership
        {
            PageGuard original = pool.FetchPage(page_id);

            assert(pool.GetPinCount(page_id) == 1);

            PageGuard moved(std::move(original));

            assert(!original.IsValid());
            assert(moved.IsValid());
            assert(pool.GetPinCount(page_id) == 1);
        }

        assert(pool.GetPinCount(page_id) == 0);

        std::cout << "Move constructor ownership transfer: PASS\n";


        // TEST 6: Move assignment releases old ownership
        page_id_t second_id = 0;

        PageGuard first = pool.NewPage(&page_id);
        PageGuard second = pool.NewPage(&second_id);

        assert(pool.GetPinCount(page_id) == 1);
        assert(pool.GetPinCount(second_id) == 1);

        first = std::move(second);

        assert(!second.IsValid());
        assert(first.IsValid());
        assert(first.GetPageId() == second_id);

        assert(pool.GetPinCount(page_id) == 0);
        assert(pool.GetPinCount(second_id) == 1);

        first.Drop();

        assert(pool.GetPinCount(second_id) == 0);

        std::cout << "Move assignment ownership handling: PASS\n";


        // TEST 7: Explicit Drop is safe to repeat
        {
            PageGuard guard = pool.FetchPage(page_id);

            assert(pool.GetPinCount(page_id) == 1);

            guard.Drop();
            guard.Drop();

            assert(!guard.IsValid());
            assert(pool.GetPinCount(page_id) == 0);
        }

        std::cout << "Repeated Drop protection: PASS\n";


        // TEST 8: GetPageData access
        {
            PageGuard guard = pool.FetchPage(page_id);

            auto& data = guard.GetPageData();

            data[10] = 99;

            assert(guard.IsDirty());
        }

        std::cout << "PageData access: PASS\n";


        // TEST 9: Invalid guard dirty protection
        assert(
            [&]() {
                try {
                    empty.MarkDirty();
                }
                catch (const StorageException&) {
                    return true;
                }

                return false;
            }()
        );

        std::cout << "Invalid guard dirty rejection: PASS\n";


        // TEST 10: Frame pin count after all guards drop
        assert(pool.GetPinnedFrameCount() == 0);

        std::cout << "Final pin count verification: PASS\n";
    }

    std::filesystem::remove(database_file);

    std::cout << "\nAll PageGuard tests passed!\n";

    return 0;
}
