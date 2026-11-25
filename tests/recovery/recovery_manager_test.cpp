
#include "minidb/recovery/recovery_manager.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/recovery/log_record.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace minidb;

struct AppliedAction {

    txn_id_t txn_id;

    LogRecordType type;

    RecoveryAction action;
};


int main() {

    const std::string walFile =
        "data/recovery_manager_test.wal";

    std::filesystem::remove(walFile);

    using RecordKey =
        std::pair<page_id_t, slot_id_t>;

    // Represents the database records after recovery.
    std::map<RecordKey, std::vector<std::uint8_t>> database;

    std::vector<AppliedAction> actions;

    bool flushCalled = false;


    // ---------------------------------------------
    // PHASE 1: Create the WAL history.
    // ---------------------------------------------

    {
        LogManager writer(walFile);


        // TRANSACTION 1:
        // INSERT followed by COMMIT.
        // This transaction must be REDONE.

        LogRecord begin1;

        begin1.txn_id = 1;
        begin1.type = LogRecordType::BEGIN;

        const lsn_t begin1LSN =
            writer.AppendLogRecord(begin1);


        LogRecord insert1;

        insert1.txn_id = 1;
        insert1.prev_lsn = begin1LSN;
        insert1.type = LogRecordType::INSERT;
        insert1.has_page = true;
        insert1.page_id = 1;
        insert1.slot_id = 1;
        insert1.after_image = {'A'};

        const lsn_t insert1LSN =
            writer.AppendLogRecord(insert1);


        LogRecord commit1;

        commit1.txn_id = 1;
        commit1.prev_lsn = insert1LSN;
        commit1.type = LogRecordType::COMMIT;

        writer.AppendLogRecord(commit1);


        // TRANSACTION 2:
        // UPDATE without COMMIT.
        // This transaction must be UNDONE.

        LogRecord begin2;

        begin2.txn_id = 2;
        begin2.type = LogRecordType::BEGIN;

        const lsn_t begin2LSN =
            writer.AppendLogRecord(begin2);


        LogRecord update2;

        update2.txn_id = 2;
        update2.prev_lsn = begin2LSN;
        update2.type = LogRecordType::UPDATE;
        update2.has_page = true;
        update2.page_id = 1;
        update2.slot_id = 1;
        update2.before_image = {'A'};
        update2.after_image = {'B'};

        writer.AppendLogRecord(update2);


        // TRANSACTION 3:
        // UPDATE followed by ABORT.
        // Runtime rollback is assumed complete.
        // Recovery must skip this transaction.

        LogRecord begin3;

        begin3.txn_id = 3;
        begin3.type = LogRecordType::BEGIN;

        const lsn_t begin3LSN =
            writer.AppendLogRecord(begin3);


        LogRecord update3;

        update3.txn_id = 3;
        update3.prev_lsn = begin3LSN;
        update3.type = LogRecordType::UPDATE;
        update3.has_page = true;
        update3.page_id = 1;
        update3.slot_id = 2;
        update3.before_image = {'C'};
        update3.after_image = {'D'};

        const lsn_t update3LSN =
            writer.AppendLogRecord(update3);


        LogRecord abort3;

        abort3.txn_id = 3;
        abort3.prev_lsn = update3LSN;
        abort3.type = LogRecordType::ABORT;

        writer.AppendLogRecord(abort3);


        // TRANSACTION 4:
        // INSERT followed by UPDATE, without COMMIT.
        // Both operations must be undone in reverse order.

        LogRecord begin4;

        begin4.txn_id = 4;
        begin4.type = LogRecordType::BEGIN;

        const lsn_t begin4LSN =
            writer.AppendLogRecord(begin4);


        LogRecord insert4;

        insert4.txn_id = 4;
        insert4.prev_lsn = begin4LSN;
        insert4.type = LogRecordType::INSERT;
        insert4.has_page = true;
        insert4.page_id = 2;
        insert4.slot_id = 1;
        insert4.after_image = {'X'};

        const lsn_t insert4LSN =
            writer.AppendLogRecord(insert4);


        LogRecord update4;

        update4.txn_id = 4;
        update4.prev_lsn = insert4LSN;
        update4.type = LogRecordType::UPDATE;
        update4.has_page = true;
        update4.page_id = 2;
        update4.slot_id = 1;
        update4.before_image = {'X'};
        update4.after_image = {'Y'};

        writer.AppendLogRecord(update4);


        // All test log records must be durable.
        writer.Flush();
    }

    std::cout << "WAL history creation: PASS\n";


    // ---------------------------------------------
    // PHASE 2: Start recovery.
    // ---------------------------------------------

    {
        LogManager reader(walFile);

        RecoveryManager recovery(reader);


        // Callback that applies a record's before/after image.
        const auto applyRecord =
            [&](const LogRecord& record,
                RecoveryAction action) {

                const RecordKey key{
                    record.page_id,
                    record.slot_id
                };

                actions.push_back({
                    record.txn_id,
                    record.type,
                    action
                });


                if (action == RecoveryAction::REDO) {

                    // REDO applies the AFTER image.
                    switch (record.type) {

                        case LogRecordType::INSERT:
                        case LogRecordType::UPDATE:

                            database[key] =
                                record.after_image;

                            break;

                        case LogRecordType::DELETE:

                            database.erase(key);

                            break;

                        default:
                            assert(false);
                    }
                }
                else {

                    // UNDO restores the BEFORE image.
                    switch (record.type) {

                        case LogRecordType::INSERT:

                            database.erase(key);

                            break;

                        case LogRecordType::UPDATE:
                        case LogRecordType::DELETE:

                            database[key] =
                                record.before_image;

                            break;

                        default:
                            assert(false);
                    }
                }
            };


        // Called after all recovery changes are applied.
        const auto flushPages = [&]() {

            flushCalled = true;
        };


        const RecoveryResult result =
            recovery.Recover(
                applyRecord,
                flushPages
            );


        // -----------------------------------------
        // PHASE 3: Validate recovery statistics.
        // -----------------------------------------

        assert(result.records_scanned == 11);

        assert(result.records_redone == 1);

        assert(result.records_undone == 3);

        assert(result.committed_transactions == 1);

        assert(result.aborted_transactions == 1);

        assert(result.incomplete_transactions == 2);

        std::cout << "WAL analysis: PASS\n";

        std::cout << "Committed transaction REDO: PASS\n";

        std::cout << "Incomplete transaction UNDO: PASS\n";

        std::cout << "Aborted transaction handling: PASS\n";


        // -----------------------------------------
        // PHASE 4: Validate operation order.
        // -----------------------------------------

        assert(actions.size() == 4);


        // First: committed T1 INSERT must be redone.
        assert(actions[0].txn_id == 1);

        assert(actions[0].type == LogRecordType::INSERT);

        assert(actions[0].action == RecoveryAction::REDO);


        // Second: T4 UPDATE must be undone first.
        assert(actions[1].txn_id == 4);

        assert(actions[1].type == LogRecordType::UPDATE);

        assert(actions[1].action == RecoveryAction::UNDO);


        // Third: T4 INSERT must be undone next.
        assert(actions[2].txn_id == 4);

        assert(actions[2].type == LogRecordType::INSERT);

        assert(actions[2].action == RecoveryAction::UNDO);


        // Fourth: T2 UPDATE must be undone.
        assert(actions[3].txn_id == 2);

        assert(actions[3].type == LogRecordType::UPDATE);

        assert(actions[3].action == RecoveryAction::UNDO);


        std::cout << "Reverse-order UNDO: PASS\n";


        // -----------------------------------------
        // PHASE 5: Validate final database state.
        // -----------------------------------------

        // T1's committed INSERT must exist.
        assert(database.count({1, 1}) == 1);

        assert(database.at({1, 1}) ==
               std::vector<std::uint8_t>{'A'});


        // T4's incomplete INSERT must be removed.
        assert(database.count({2, 1}) == 0);


        // Page-flush callback must execute.
        assert(flushCalled);

        std::cout << "Final database state: PASS\n";

        std::cout << "Recovery page flush: PASS\n";
    }


    std::filesystem::remove(walFile);

    std::cout << "\nAll RecoveryManager tests passed!\n";

    return 0;
}
