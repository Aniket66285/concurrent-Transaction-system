
#include "minidb/storage/table_heap.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/recovery/recovery_manager.hpp"

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

    const std::string database_file =
        "data/table_heap_test.db";

    std::filesystem::remove(database_file);

    page_id_t root_page_id = 0;

    std::vector<RID> record_ids;

    std::vector<std::vector<std::uint8_t>> records;

    RID reused_rid{};

    const std::vector<std::uint8_t> updated_record(
        100, 0xE1
    );

    const std::vector<std::uint8_t> replacement_record(
        1000, 0xAA
    );

    // ==============================================
    // TEST 1: Create a new TableHeap
    // ==============================================

    {
        DiskManager disk(database_file);
        BufferPool pool(disk, 4);

        TableHeap heap(pool);

        root_page_id = heap.GetRootPageId();

        assert(heap.GetDataPageCount() == 0);
        assert(pool.IsPageResident(root_page_id));

        std::cout << "TableHeap creation: PASS\n";


        // ==========================================
        // TEST 2: Empty record rejection
        // ==========================================

        assert(ThrowsStorageException([&]() {
            heap.InsertRecord({});
        }));

        std::cout << "Empty record rejection: PASS\n";


        // ==========================================
        // TEST 3: Oversized record rejection
        // ==========================================

        const std::vector<std::uint8_t> oversized(
            4077, 0xFF
        );

        assert(ThrowsStorageException([&]() {
            heap.InsertRecord(oversized);
        }));

        std::cout << "Oversized record rejection: PASS\n";


        // ==========================================
        // TEST 4: Insert multiple records
        // ==========================================

        constexpr std::size_t kRecordCount = 11;
        constexpr std::size_t kRecordSize = 2000;

        for (std::size_t i = 0; i < kRecordCount; ++i) {

            const std::uint8_t value =
                static_cast<std::uint8_t>(i + 1);

            std::vector<std::uint8_t> record(
                kRecordSize,
                value
            );

            const RID rid = heap.InsertRecord(record);

            record_ids.push_back(rid);
            records.push_back(record);
        }

        assert(record_ids.size() == kRecordCount);

        std::cout << "Multiple record insertion: PASS\n";


        // ==========================================
        // TEST 5: Multiple data page allocation
        // ==========================================

        assert(heap.GetDataPageCount() == 6);

        for (const RID& rid : record_ids) {
            assert(rid.page_id != root_page_id);
        }

        std::cout << "Multiple data page allocation: PASS\n";


        // ==========================================
        // TEST 6: Record retrieval
        // ==========================================

        for (std::size_t i = 0; i < records.size(); ++i) {

            const auto actual =
                heap.GetRecord(record_ids[i]);

            assert(actual == records[i]);
        }

        std::cout << "Record retrieval: PASS\n";


        // ==========================================
        // TEST 7: Record update
        // ==========================================

        assert(
            heap.UpdateRecord(
                record_ids[0],
                updated_record
            )
        );

        assert(
            heap.GetRecord(record_ids[0]) ==
            updated_record
        );

        std::cout << "Record update: PASS\n";


        // ==========================================
        // TEST 8: Record deletion
        // ==========================================

        assert(heap.DeleteRecord(record_ids[1]));

        assert(!heap.DeleteRecord(record_ids[1]));

        assert(ThrowsStorageException([&]() {
            heap.GetRecord(record_ids[1]);
        }));

        std::cout << "Record deletion: PASS\n";


        // ==========================================
        // TEST 9: Deleted slot reuse
        // ==========================================

        reused_rid = heap.InsertRecord(replacement_record);

        assert(reused_rid.page_id == record_ids[1].page_id);
        assert(reused_rid.slot_id == record_ids[1].slot_id);

        assert(heap.GetRecord(reused_rid) == replacement_record);

        assert(heap.GetDataPageCount() == 6);

        std::cout << "Deleted slot reuse: PASS\n";


        // ==========================================
        // TEST 10: Maximum-size record
        // ==========================================

        const std::vector<std::uint8_t> maximum_record(
            4076, 0x7A
        );

        const RID maximum_rid =
            heap.InsertRecord(maximum_record);

        assert(heap.GetRecord(maximum_rid) == maximum_record);

        assert(heap.GetDataPageCount() == 7);

        std::cout << "Maximum-size record handling: PASS\n";


        // ==========================================
        // TEST 11: Invalid RID handling
        // ==========================================

        const RID invalid_rid{999999, 0};

        assert(!heap.UpdateRecord(
            invalid_rid,
            updated_record
        ));

        assert(!heap.DeleteRecord(invalid_rid));

        assert(ThrowsStorageException([&]() {
            heap.GetRecord(invalid_rid);
        }));

        std::cout << "Invalid RID handling: PASS\n";


        // ==========================================
        // TEST 12: Dirty page flush
        // ==========================================

        // Deliberately modify an existing record immediately
        // before FlushAllPages().
        //
        // This ensures at least one resident page is dirty.

        const std::vector<std::uint8_t> flush_record(
            2000, 0x5C
        );

        assert(heap.UpdateRecord(
            record_ids[2],
            flush_record
        ));

        records[2] = flush_record;

        assert(
            heap.GetRecord(record_ids[2]) ==
            flush_record
        );

        const std::size_t flushed =
            pool.FlushAllPages();

        assert(flushed >= 1);

        std::cout << "Database page flush: PASS\n";
    }


    // ==============================================
    // Reopen database with a new DiskManager
    // and a completely new BufferPool.
    // ==============================================

    {
        DiskManager reopened_disk(database_file);

        BufferPool reopened_pool(reopened_disk, 4);

        TableHeap reopened_heap(
            reopened_pool,
            root_page_id
        );

        assert(
            reopened_heap.GetRootPageId() ==
            root_page_id
        );

        assert(reopened_heap.GetDataPageCount() == 7);

        // Updated record must survive reopening.
        assert(
            reopened_heap.GetRecord(record_ids[0]) ==
            updated_record
        );

        // All other original records must survive.
        for (std::size_t i = 2; i < records.size(); ++i) {

            assert(
                reopened_heap.GetRecord(record_ids[i]) ==
                records[i]
            );
        }

        // The deleted slot must contain its replacement.
        assert(
            reopened_heap.GetRecord(reused_rid) ==
            replacement_record
        );

        // The deleted record must remain deleted.
        // The deleted slot was reused by a new record.
// Both RIDs point to the same page and slot.
assert(record_ids[1].page_id == reused_rid.page_id);
assert(record_ids[1].slot_id == reused_rid.slot_id);

// The reused record must survive database reopening.
assert(
    reopened_heap.GetRecord(record_ids[1]) ==
    replacement_record
);

    assert(
        reopened_heap.GetRecord(reused_rid) ==
        replacement_record
    );

        std::cout << "Database reopen and record persistence: PASS\n";

        assert(reopened_pool.FlushAllPages() == 0);
    }

        // ==============================================
    // TRANSACTIONAL TABLEHEAP TESTS
    // ==============================================

    const std::string tx_database =
        "data/table_heap_transaction_test.db";

    const std::string tx_wal =
        "data/table_heap_transaction_test.wal";

    std::filesystem::remove(tx_database);
    std::filesystem::remove(tx_wal);

    {
        DiskManager tx_disk(tx_database);
        LogManager tx_log(tx_wal);
        LockManager tx_locks;

        BufferPool tx_pool(
            tx_disk,
            4,
            [&tx_log]() {
                tx_log.Flush();
            }
        );

        TableHeap tx_heap(tx_pool);

        TxnManager tx_manager(tx_log, &tx_locks);

        const std::vector<std::uint8_t> original = {
            'O', 'R', 'I', 'G'
        };

        const std::vector<std::uint8_t> changed = {
            'U', 'P', 'D', 'A', 'T', 'E'
        };

        // Baseline record.
        RID baseline = tx_heap.InsertRecord(original);

        // ------------------------------------------
        // TEST 14: Transactional INSERT + ROLLBACK
        // ------------------------------------------

        auto insert_txn = tx_manager.BeginTransaction();

        const std::vector<std::uint8_t> inserted_data = {
            'I', 'N', 'S', 'E', 'R', 'T'
        };

        RID inserted_rid = tx_heap.InsertRecord(
            *insert_txn,
            tx_manager,
            inserted_data
        );

        assert(
            tx_heap.GetRecord(
                *insert_txn,
                tx_manager,
                inserted_rid
            ) == inserted_data
        );

        tx_manager.AbortTransaction(
            *insert_txn,
            [&](const LogRecord& record) {
                tx_heap.UndoRecord(record);
            },
            [&]() {
                tx_pool.FlushAllPages();
            }
        );

        assert(ThrowsStorageException([&]() {
            tx_heap.GetRecord(inserted_rid);
        }));

        std::cout << "Transactional INSERT rollback: PASS\n";


        // ------------------------------------------
        // TEST 15: Transactional UPDATE + ROLLBACK
        // ------------------------------------------

        auto update_txn = tx_manager.BeginTransaction();

        assert(tx_heap.UpdateRecord(
            *update_txn,
            tx_manager,
            baseline,
            changed
        ));

        assert(
            tx_heap.GetRecord(
                *update_txn,
                tx_manager,
                baseline
            ) == changed
        );

        tx_manager.AbortTransaction(
            *update_txn,
            [&](const LogRecord& record) {
                tx_heap.UndoRecord(record);
            },
            [&]() {
                tx_pool.FlushAllPages();
            }
        );

        assert(tx_heap.GetRecord(baseline) == original);

        std::cout << "Transactional UPDATE rollback: PASS\n";


        // ------------------------------------------
        // TEST 16: Transactional DELETE + ROLLBACK
        // ------------------------------------------

        auto delete_txn = tx_manager.BeginTransaction();

        assert(tx_heap.DeleteRecord(
            *delete_txn,
            tx_manager,
            baseline
        ));

        tx_manager.AbortTransaction(
            *delete_txn,
            [&](const LogRecord& record) {
                tx_heap.UndoRecord(record);
            },
            [&]() {
                tx_pool.FlushAllPages();
            }
        );

        assert(tx_heap.GetRecord(baseline) == original);

        std::cout << "Transactional DELETE rollback: PASS\n";


        // ------------------------------------------
        // TEST 17: Transactional INSERT + COMMIT
        // ------------------------------------------

        auto commit_insert_txn =
            tx_manager.BeginTransaction();

        const std::vector<std::uint8_t> committed_data = {
            'C', 'O', 'M', 'M', 'I', 'T'
        };

        RID committed_rid = tx_heap.InsertRecord(
            *commit_insert_txn,
            tx_manager,
            committed_data
        );

        tx_manager.CommitTransaction(*commit_insert_txn);

        assert(
            tx_heap.GetRecord(committed_rid) ==
            committed_data
        );

        std::cout << "Transactional INSERT commit: PASS\n";


        // ------------------------------------------
        // TEST 18: Transactional UPDATE + COMMIT
        // ------------------------------------------

        auto commit_update_txn =
            tx_manager.BeginTransaction();

        assert(tx_heap.UpdateRecord(
            *commit_update_txn,
            tx_manager,
            baseline,
            changed
        ));

        tx_manager.CommitTransaction(*commit_update_txn);

        assert(tx_heap.GetRecord(baseline) == changed);

        std::cout << "Transactional UPDATE commit: PASS\n";

                // ------------------------------------------
        // TEST 19: RecoveryManager + TableHeap
        // ------------------------------------------

        RecoveryManager recovery(tx_log);

        const RecoveryResult recovery_result =
            recovery.Recover(
                [&](const LogRecord& record, RecoveryAction action) {

                    if (action == RecoveryAction::REDO) {
                        tx_heap.RedoRecord(record);
                    }
                    else {
                        tx_heap.UndoRecord(record);
                    }
                },
                [&]() {
                    tx_pool.FlushAllPages();
                }
            );

        assert(recovery_result.records_scanned > 0);

        assert(recovery_result.records_redone >= 2);

        assert(
            tx_heap.GetRecord(committed_rid) ==
            committed_data
        );

        assert(
            tx_heap.GetRecord(baseline) ==
            changed
        );

        std::cout << "RecoveryManager and TableHeap integration: PASS\n";

        assert(tx_pool.FlushAllPages() == 0);

        std::cout << "Recovery page flush verification: PASS\n";
    }

    std::filesystem::remove(tx_database);
    std::filesystem::remove(tx_wal);

    std::filesystem::remove(database_file);

    std::cout << "\nAll TableHeap tests passed!\n";

    return 0;
}
