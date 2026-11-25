
#include "minidb/recovery/checkpoint.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/recovery/log_record.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using namespace minidb;

int main() {

    const std::string walFile =
        "data/checkpoint_test.wal";

    const std::string masterFile =
        "data/checkpoint_test.master";

    // Start with clean test files.
    std::filesystem::remove(walFile);
    std::filesystem::remove(masterFile);
    std::filesystem::remove(masterFile + ".tmp");

    lsn_t beginLSN = kInvalidLSN;
    lsn_t checkpointLSN = kInvalidLSN;


    // ---------------------------------------------
    // TEST 1: Create a checkpoint.
    // ---------------------------------------------

    {
        LogManager logManager(walFile);

        assert(std::filesystem::exists(walFile));

        CheckpointManager checkpointManager(
            logManager,
            masterFile
        );

        assert(!checkpointManager.HasCheckpoint());

        std::cout << "Initial checkpoint state: PASS\n";


        // Append a BEGIN record.
        LogRecord beginRecord;

        beginRecord.txn_id = 1;
        beginRecord.type = LogRecordType::BEGIN;

        beginLSN =
            logManager.AppendLogRecord(beginRecord);

        logManager.Flush();

        assert(
            logManager.GetPersistentLSN() == beginLSN
        );


        // This callback represents the Buffer Pool's
        // future dirty-page flushing operation.
        bool pageFlushCalled = false;

        checkpointLSN =
            checkpointManager.CreateCheckpoint(
                [&]() {

                    pageFlushCalled = true;

                    // WAL must already be durable
                    // before database pages are flushed.
                    assert(
                        logManager.GetPersistentLSN() ==
                        beginLSN
                    );
                }
            );

        assert(pageFlushCalled);

        assert(checkpointLSN != kInvalidLSN);

        assert(checkpointLSN > beginLSN);

        assert(
            checkpointManager.HasCheckpoint()
        );

        assert(
            checkpointManager.GetLastCheckpointLSN() ==
            checkpointLSN
        );

        assert(
            logManager.GetPersistentLSN() ==
            checkpointLSN
        );

        std::cout << "Checkpoint creation: PASS\n";

        std::cout << "WAL-before-data ordering: PASS\n";


        // -----------------------------------------
        // TEST 2: Verify master file.
        // -----------------------------------------

        assert(
            std::filesystem::exists(masterFile)
        );

        // Master record format has exactly 20 bytes.
        assert(
            std::filesystem::file_size(masterFile) == 20
        );

        std::cout << "Master file persistence: PASS\n";


        // -----------------------------------------
        // TEST 3: Reload checkpoint from disk.
        // -----------------------------------------

        CheckpointManager reopenedManager(
            logManager,
            masterFile
        );

        assert(reopenedManager.HasCheckpoint());

        assert(
            reopenedManager.GetLastCheckpointLSN() ==
            checkpointLSN
        );

        std::cout << "Checkpoint reload: PASS\n";


        // -----------------------------------------
        // TEST 4: Verify checkpoint WAL record.
        // -----------------------------------------

        const auto records =
            logManager.ReadAllRecords(false);

        assert(records.size() == 2);

        assert(
            records[0].type == LogRecordType::BEGIN
        );

        assert(
            records[1].type == LogRecordType::CHECKPOINT
        );

        assert(records[1].txn_id == 0);

        assert(records[1].lsn == checkpointLSN);

        std::cout << "Checkpoint WAL record: PASS\n";
    }


    // Clean up test files.
    std::filesystem::remove(walFile);

    std::filesystem::remove(masterFile);

    std::filesystem::remove(masterFile + ".tmp");


    std::cout << "\nAll CheckpointManager tests passed!\n";

    return 0;
}
