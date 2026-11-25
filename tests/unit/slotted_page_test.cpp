
#include "minidb/storage/slotted_page.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/common/exception.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
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

    const std::string databaseFile =
        "data/slotted_page_test.db";

    std::filesystem::remove(databaseFile);



    // ---------------------------------------------
    // TEST 1: Initialize a page.
    // ---------------------------------------------

    DiskManager::PageData page{};

    SlottedPage::Initialize(page, 42);

    assert(SlottedPage::IsInitialized(page));

    SlottedPage slottedPage(page);

    assert(slottedPage.GetPageId() == 42);

    std::cout << "Page initialization: PASS\n";


    // ---------------------------------------------
    // TEST 2: Empty page metadata.
    // ---------------------------------------------

    assert(slottedPage.GetSlotCount() == 0);

    assert(slottedPage.GetRecordCount() == 0);

    assert(
        slottedPage.GetFreeSpace() ==
        Config::kPageSizeBytes -
        SlottedPage::kHeaderSize
    );

    assert(
        slottedPage.GetTotalFreeSpace() ==
        slottedPage.GetFreeSpace()
    );

    std::cout << "Initial page metadata: PASS\n";


    // ---------------------------------------------
    // TEST 3: Insert first record.
    // ---------------------------------------------

    const std::vector<std::uint8_t> recordA = {
        'A', 'm', 'a', 'n'
    };

    const auto slotA =
        slottedPage.InsertRecord(recordA);

    assert(slotA.has_value());

    assert(*slotA == 0);

    assert(slottedPage.GetRecordCount() == 1);

    assert(slottedPage.IsSlotOccupied(0));

    std::cout << "First record insertion: PASS\n";


    // ---------------------------------------------
    // TEST 4: Insert additional records.
    // ---------------------------------------------

    const std::vector<std::uint8_t> recordB = {
        'A', 'n', 'i', 'k', 'e', 't'
    };

    const std::vector<std::uint8_t> recordC = {
        'D', 'B', 'M', 'S'
    };

    const auto slotB =
        slottedPage.InsertRecord(recordB);

    const auto slotC =
        slottedPage.InsertRecord(recordC);

    assert(slotB.has_value());

    assert(slotC.has_value());

    assert(*slotB == 1);

    assert(*slotC == 2);

    assert(slottedPage.GetSlotCount() == 3);

    assert(slottedPage.GetRecordCount() == 3);

    std::cout << "Multiple record insertion: PASS\n";


    // ---------------------------------------------
    // TEST 5: Read records from slots.
    // ---------------------------------------------

    assert(slottedPage.GetRecord(0) == recordA);

    assert(slottedPage.GetRecord(1) == recordB);

    assert(slottedPage.GetRecord(2) == recordC);

    std::cout << "Record retrieval: PASS\n";


    // ---------------------------------------------
    // TEST 6: Stable slot identification.
    // ---------------------------------------------

    assert(slottedPage.GetPageId() == 42);

    assert(*slotA == 0);

    assert(*slotB == 1);

    assert(*slotC == 2);

    std::cout << "Stable slot IDs: PASS\n";


    // ---------------------------------------------
    // TEST 7: Update a record.
    // ---------------------------------------------

    const std::vector<std::uint8_t> updatedA = {
        'N', 'e', 'w', 'A', 'm', 'a', 'n'
    };

    const bool updateSuccessful =
        slottedPage.UpdateRecord(0, updatedA);

    assert(updateSuccessful);

    assert(slottedPage.GetRecord(0) == updatedA);

    assert(slottedPage.GetRecord(1) == recordB);

    assert(slottedPage.GetRecord(2) == recordC);

    std::cout << "Record update: PASS\n";


    // ---------------------------------------------
    // TEST 8: Failed update preserves original data.
    // ---------------------------------------------

    const auto originalA =
        slottedPage.GetRecord(0);

    const std::vector<std::uint8_t> oversizedUpdate(
        Config::kPageSizeBytes,
        0xAA
    );

    const bool failedUpdate =
        slottedPage.UpdateRecord(
            0,
            oversizedUpdate
        );

    assert(!failedUpdate);

    assert(slottedPage.GetRecord(0) == originalA);

    std::cout << "Failed update preserves original data: PASS\n";


    // ---------------------------------------------
    // TEST 9: Delete a record.
    // ---------------------------------------------

    assert(slottedPage.DeleteRecord(1));

    assert(!slottedPage.IsSlotOccupied(1));

    assert(slottedPage.GetRecordCount() == 2);

    // Slot positions remain allocated.
    assert(slottedPage.GetSlotCount() == 3);

    std::cout << "Record deletion: PASS\n";


    // ---------------------------------------------
    // TEST 10: Deleted record cannot be read.
    // ---------------------------------------------

    assert(
        ThrowsStorageException([&]() {
            slottedPage.GetRecord(1);
        })
    );

    std::cout << "Deleted record access rejection: PASS\n";


    // ---------------------------------------------
    // TEST 11: Reuse a deleted slot.
    // ---------------------------------------------

    const std::vector<std::uint8_t> replacement = {
        'Z'
    };

    const auto reusedSlot =
        slottedPage.InsertRecord(replacement);

    assert(reusedSlot.has_value());

    assert(*reusedSlot == 1);

    assert(slottedPage.GetSlotCount() == 3);

    assert(slottedPage.GetRecordCount() == 3);

    assert(slottedPage.GetRecord(1) == replacement);

    std::cout << "Deleted slot reuse: PASS\n";


    // ---------------------------------------------
    // TEST 12: Invalid slot deletion handling.
    // ---------------------------------------------

    assert(!slottedPage.DeleteRecord(20));

    assert(slottedPage.GetRecordCount() == 3);

    std::cout << "Invalid slot deletion handling: PASS\n";


    // ---------------------------------------------
    // TEST 13: Invalid slot access.
    // ---------------------------------------------

    assert(
        ThrowsStorageException([&]() {
            slottedPage.GetRecord(20);
        })
    );

    std::cout << "Invalid slot access rejection: PASS\n";


    // ---------------------------------------------
    // TEST 14: Empty record rejection.
    // ---------------------------------------------

    assert(
        ThrowsStorageException([&]() {
            slottedPage.InsertRecord(
                std::vector<std::uint8_t>{}
            );
        })
    );

    std::cout << "Empty record rejection: PASS\n";


    // ---------------------------------------------
    // TEST 15: Compaction reclaims fragmented space.
    // ---------------------------------------------

    DiskManager::PageData fragmentedPage{};

    SlottedPage::Initialize(fragmentedPage, 50);

    SlottedPage fragmented(fragmentedPage);


    const std::vector<std::uint8_t> largeA(500, 'A');

    const std::vector<std::uint8_t> largeB(500, 'B');

    const std::vector<std::uint8_t> largeC(500, 'C');


    const auto largeSlotA =
        fragmented.InsertRecord(largeA);

    const auto largeSlotB =
        fragmented.InsertRecord(largeB);

    const auto largeSlotC =
        fragmented.InsertRecord(largeC);

    assert(largeSlotA.has_value());

    assert(largeSlotB.has_value());

    assert(largeSlotC.has_value());


    const std::size_t freeBeforeDelete =
        fragmented.GetFreeSpace();

    const std::size_t totalBeforeDelete =
        fragmented.GetTotalFreeSpace();


    assert(fragmented.DeleteRecord(*largeSlotB));


    // Deletion does not change contiguous free space.
    assert(
        fragmented.GetFreeSpace() ==
        freeBeforeDelete
    );


    // Deleted 500-byte record becomes reclaimable.
    const std::size_t totalAfterDelete =
        fragmented.GetTotalFreeSpace();

    assert(
        totalAfterDelete ==
        totalBeforeDelete + largeB.size()
    );

    assert(
        totalAfterDelete >
        fragmented.GetFreeSpace()
    );


    // Compact the page.
    fragmented.Compact();


    // Fragmented space should now be contiguous.
    assert(
        fragmented.GetFreeSpace() ==
        totalAfterDelete
    );


    // Existing records must remain unchanged.
    assert(fragmented.GetRecord(0) == largeA);

    assert(fragmented.GetRecord(2) == largeC);

    // The deleted slot must still be empty.
    assert(!fragmented.IsSlotOccupied(1));

    std::cout << "Page compaction: PASS\n";


    // ---------------------------------------------
    // TEST 16: Insert large record after compaction.
    // ---------------------------------------------

    const std::vector<std::uint8_t> largeReplacement(
        2800,
        'R'
    );

    const auto compactedSlot =
        fragmented.InsertRecord(largeReplacement);

    assert(compactedSlot.has_value());

    // Deleted slot 1 must be reused.
    assert(*compactedSlot == 1);

    assert(fragmented.GetRecord(1) == largeReplacement);

    assert(fragmented.GetRecordCount() == 3);

    std::cout << "Fragmentation recovery and insertion: PASS\n";


    // ---------------------------------------------
    // TEST 17: Full page capacity handling.
    // ---------------------------------------------

    DiskManager::PageData fullPageData{};

    SlottedPage::Initialize(fullPageData, 60);

    SlottedPage fullPage(fullPageData);

    const std::vector<std::uint8_t> payload(200, 0xAB);

    std::size_t insertedCount = 0;

    while (true) {

        const auto inserted =
            fullPage.InsertRecord(payload);

        if (!inserted.has_value()) {
            break;
        }

        ++insertedCount;
    }

    assert(insertedCount > 0);

    assert(
        fullPage.GetRecordCount() == insertedCount
    );

    assert(
        fullPage.GetTotalFreeSpace() <
        payload.size() + SlottedPage::kSlotEntrySize
    );

    const auto noSpace =
        fullPage.InsertRecord(
            std::vector<std::uint8_t>(
                Config::kPageSizeBytes,
                0xFF
            )
        );

    assert(!noSpace.has_value());

    std::cout << "Full page capacity handling: PASS\n";


    // ---------------------------------------------
    // TEST 18: Actual DiskManager persistence.
    // ---------------------------------------------

    {
        DiskManager diskManager(databaseFile);

        const page_id_t pageId =
            diskManager.AllocatePage();

        assert(pageId == 0);

        DiskManager::PageData diskPage{};

        SlottedPage::Initialize(
            diskPage,
            pageId
        );

        SlottedPage diskSlottedPage(diskPage);

        const std::vector<std::uint8_t> persistedRecord = {
            'D', 'A', 'T', 'A', 'B', 'A', 'S', 'E'
        };

        const auto persistedSlot =
            diskSlottedPage.InsertRecord(
                persistedRecord
            );

        assert(persistedSlot.has_value());

        diskManager.WritePage(
            pageId,
            diskPage
        );

        diskManager.Flush();
    }


    // Reopen physical database file.
    {
        DiskManager reopenedManager(databaseFile);

        assert(reopenedManager.GetPageCount() == 1);

        DiskManager::PageData recoveredPage{};

        reopenedManager.ReadPage(
            0,
            recoveredPage
        );

        SlottedPage recoveredSlottedPage(
            recoveredPage
        );

        assert(recoveredSlottedPage.GetPageId() == 0);

        assert(recoveredSlottedPage.GetRecordCount() == 1);

        const std::vector<std::uint8_t> expectedRecord = {
            'D', 'A', 'T', 'A', 'B', 'A', 'S', 'E'
        };

        assert(
            recoveredSlottedPage.GetRecord(0) ==
            expectedRecord
        );

        std::cout << "SlottedPage disk persistence: PASS\n";
    }


    // ---------------------------------------------
    // TEST 19: Invalid page signature.
    // ---------------------------------------------

    DiskManager::PageData corruptedPage{};

    SlottedPage::Initialize(
        corruptedPage,
        70
    );

    corruptedPage[0] = 0x00;

    assert(!SlottedPage::IsInitialized(corruptedPage));

    assert(
        ThrowsStorageException([&]() {
            SlottedPage invalidPage(corruptedPage);
        })
    );

    std::cout << "Invalid page signature rejection: PASS\n";


    // ---------------------------------------------
    // TEST 20: Invalid header boundary.
    // ---------------------------------------------

    DiskManager::PageData invalidBoundary{};

    SlottedPage::Initialize(
        invalidBoundary,
        71
    );

    invalidBoundary[8] = 0;
    invalidBoundary[9] = 0;

    assert(
        ThrowsStorageException([&]() {
            SlottedPage invalidPage(invalidBoundary);
        })
    );

    std::cout << "Invalid page boundary rejection: PASS\n";


    // ---------------------------------------------
    // TEST 21: Uninitialized page.
    // ---------------------------------------------

    DiskManager::PageData emptyPage{};

    assert(!SlottedPage::IsInitialized(emptyPage));

    assert(
        ThrowsStorageException([&]() {
            SlottedPage invalidPage(emptyPage);
        })
    );

    std::cout << "Uninitialized page rejection: PASS\n";

        // ---------------------------------------------
    // TEST 22: Exact-slot restoration
    // ---------------------------------------------

    DiskManager::PageData restoreData{};

    SlottedPage::Initialize(restoreData, 78);

    SlottedPage restorePage(restoreData);

    const std::vector<std::uint8_t> restoreA = {'A'};
    const std::vector<std::uint8_t> restoreB = {'B'};
    const std::vector<std::uint8_t> restoreC = {'C'};

    const auto restoreSlotA = restorePage.InsertRecord(restoreA);
    const auto restoreSlotB = restorePage.InsertRecord(restoreB);
    const auto restoreSlotC = restorePage.InsertRecord(restoreC);

    assert(restoreSlotA.has_value());
    assert(restoreSlotB.has_value());
    assert(restoreSlotC.has_value());

    assert(*restoreSlotB == 1);

    std::cout << "Restoration test setup: PASS\n";

    // Delete slot 1.
    assert(restorePage.DeleteRecord(1));

    std::cout << "Record deletion for restoration: PASS\n";

    // Restore original record into slot 1.
    assert(restorePage.RestoreRecord(1, restoreB));

    assert(restorePage.GetRecord(1) == restoreB);
    assert(restorePage.IsSlotOccupied(1));

    std::cout << "Exact-slot restoration: PASS\n";

    // Other records must remain unchanged.
    assert(restorePage.GetRecord(0) == restoreA);
    assert(restorePage.GetRecord(2) == restoreC);

    std::cout << "Other records preserved: PASS\n";

    // Slot count must remain unchanged.
    assert(restorePage.GetSlotCount() == 3);
    assert(restorePage.GetRecordCount() == 3);

    std::cout << "Slot metadata preserved: PASS\n";

    // Reject restoration into occupied or invalid slots.
    assert(!restorePage.RestoreRecord(1, restoreA));
    assert(!restorePage.RestoreRecord(99, restoreA));
    assert(!restorePage.RestoreRecord(1, {}));

    std::cout << "Invalid restoration rejection: PASS\n";


    // Cleanup.
    std::filesystem::remove(databaseFile);

    std::cout << "\nAll SlottedPage tests passed!\n";

    return 0;
}
