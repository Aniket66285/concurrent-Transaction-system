
#include "minidb/storage/table_heap.hpp"

#include "minidb/storage/disk_manager.hpp"
#include "minidb/buffer/buffer_pool.hpp"

#include "minidb/recovery/log_manager.hpp"
#include "minidb/recovery/recovery_manager.hpp"

#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/transaction/txn_manager.hpp"

#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using namespace minidb;

struct TestDatabase {
    page_id_t root_page_id;
    RID rid;
};

void RunCrashProcess(
    const std::function<void()>& child_operation
) {

    const pid_t child = fork();

    if (child < 0) {
        throw StorageException("fork() failed");
    }

    if (child == 0) {

        try {
            child_operation();
            _exit(0);
        }
        catch (...) {
            _exit(101);
        }
    }

    int status = 0;

    if (waitpid(child, &status, 0) < 0) {
        throw StorageException("waitpid() failed");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw StorageException(
            "Child process failed before completing crash scenario"
        );
    }
}


TestDatabase CreateBaseDatabase(
    const std::string& database_file,
    const std::vector<std::uint8_t>& initial_record
) {

    std::filesystem::remove(database_file);

    TestDatabase result{};

    {
        DiskManager disk(database_file);

        BufferPool pool(disk, 4);

        TableHeap heap(pool);

        result.root_page_id = heap.GetRootPageId();

        result.rid = heap.InsertRecord(initial_record);

        // Persist the original committed database state.
        pool.FlushAllPages();
    }

    return result;
}


void VerifyRecovery(
    const std::string& database_file,
    const std::string& wal_file,
    page_id_t root_page_id,
    const RID& rid,
    const std::vector<std::uint8_t>& expected_record,
    bool expect_incomplete_transaction
) {

    DiskManager disk(database_file);

    LogManager log(wal_file);

    BufferPool pool(
        disk,
        4,
        [&log]() {
            log.Flush();
        }
    );

    TableHeap heap(pool, root_page_id);

    RecoveryManager recovery(log);

    const RecoveryResult result =
        recovery.Recover(
            [&](const LogRecord& record, RecoveryAction action) {

                if (action == RecoveryAction::REDO) {
                    heap.RedoRecord(record);
                }
                else {
                    heap.UndoRecord(record);
                }
            },
            [&]() {
                pool.FlushAllPages();
            }
        );

    assert(result.records_scanned > 0);

    assert(result.records_redone > 0);

    if (expect_incomplete_transaction) {
        assert(result.incomplete_transactions == 1);
        assert(result.records_undone > 0);
    }
    else {
        assert(result.committed_transactions == 1);
        assert(result.incomplete_transactions == 0);
        assert(result.records_undone == 0);
    }

    assert(heap.GetRecord(rid) == expected_record);

    std::cout << "Database recovery verification: PASS\n";
}


int main() {

    const std::string initial_database =
        "data/crash_recovery_committed.db";

    const std::string initial_wal =
        "data/crash_recovery_committed.wal";

    const std::string uncommitted_database =
        "data/crash_recovery_uncommitted.db";

    const std::string uncommitted_wal =
        "data/crash_recovery_uncommitted.wal";


    const std::vector<std::uint8_t> original = {
        'O', 'R', 'I', 'G', 'I', 'N', 'A', 'L'
    };

    const std::vector<std::uint8_t> committed_value = {
        'C', 'O', 'M', 'M', 'I', 'T', 'T', 'E', 'D'
    };

    const std::vector<std::uint8_t> uncommitted_value = {
        'U', 'N', 'C', 'O', 'M', 'M', 'I', 'T', 'T', 'E', 'D'
    };


    // ==============================================
    // TEST 1: Crash after durable COMMIT
    // ==============================================

    std::filesystem::remove(initial_wal);

    const TestDatabase committed_db =
        CreateBaseDatabase(initial_database, original);

    RunCrashProcess([&]() {

        DiskManager disk(initial_database);

        LogManager log(initial_wal);

        LockManager lock_manager;

        BufferPool pool(
            disk,
            4,
            [&log]() {
                log.Flush();
            }
        );

        TableHeap heap(
            pool,
            committed_db.root_page_id
        );

        TxnManager txn_manager(log, &lock_manager);

        auto transaction =
            txn_manager.BeginTransaction();

        const bool updated = heap.UpdateRecord(
            *transaction,
            txn_manager,
            committed_db.rid,
            committed_value
        );

        if (!updated) {
            _exit(10);
        }

        // COMMIT makes WAL durable.
        // Do not flush the database page.
        txn_manager.CommitTransaction(*transaction);

        // Simulate abrupt process termination.
        _exit(0);
    });

    std::cout << "Process crash after COMMIT: PASS\n";

    // Restart and recover.
    VerifyRecovery(
        initial_database,
        initial_wal,
        committed_db.root_page_id,
        committed_db.rid,
        committed_value,
        false
    );

    std::cout << "Committed transaction REDO: PASS\n";


    // ==============================================
    // TEST 2: Crash with uncommitted dirty page
    // ==============================================

    std::filesystem::remove(uncommitted_wal);

    const TestDatabase uncommitted_db =
        CreateBaseDatabase(uncommitted_database, original);

    RunCrashProcess([&]() {

        DiskManager disk(uncommitted_database);

        LogManager log(uncommitted_wal);

        LockManager lock_manager;

        BufferPool pool(
            disk,
            4,
            [&log]() {
                log.Flush();
            }
        );

        TableHeap heap(
            pool,
            uncommitted_db.root_page_id
        );

        TxnManager txn_manager(log, &lock_manager);

        auto transaction =
            txn_manager.BeginTransaction();

        const bool updated = heap.UpdateRecord(
            *transaction,
            txn_manager,
            uncommitted_db.rid,
            uncommitted_value
        );

        if (!updated) {
            _exit(20);
        }

        // Deliberately flush uncommitted dirty pages.
        // WAL is flushed before the database page.
        pool.FlushAllPages();

        // Do not COMMIT and do not call destructors.
        _exit(0);
    });

    std::cout << "Process crash with uncommitted data: PASS\n";

    // Restart and recover.
    VerifyRecovery(
        uncommitted_database,
        uncommitted_wal,
        uncommitted_db.root_page_id,
        uncommitted_db.rid,
        original,
        true
    );

    std::cout << "Uncommitted transaction UNDO: PASS\n";


    // Cleanup.
    std::filesystem::remove(initial_database);
    std::filesystem::remove(initial_wal);

    std::filesystem::remove(uncommitted_database);
    std::filesystem::remove(uncommitted_wal);

    std::cout << "\nAll crash recovery tests passed!\n";

    return 0;
}
