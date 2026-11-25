
#include "minidb/recovery/log_manager.hpp"
#include "minidb/recovery/binary_log_record_codec.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace minidb;

int main() {

    const std::string testFile =
        "data/log_manager_test.wal";

    // Start with a fresh WAL file.
    std::filesystem::remove(testFile);

    std::uintmax_t expectedFileSize = 0;

    lsn_t beginLSN = kInvalidLSN;
    lsn_t commitLSN = kInvalidLSN;


    // ---------------------------------------------
    // TEST 1: Write BEGIN and COMMIT records.
    // ---------------------------------------------

    {
        LogManager manager(testFile);

        LogRecord beginRecord;

        beginRecord.txn_id = 1;
        beginRecord.type = LogRecordType::BEGIN;

        const auto beginBytes =
            EncodeLogRecord(beginRecord);

        beginLSN =
            manager.AppendLogRecord(beginRecord);

        assert(beginLSN == 0);

        assert(
            manager.GetNextLSN() == beginBytes.size()
        );

        manager.FlushUpTo(beginLSN);

        assert(
            manager.GetPersistentLSN() == beginLSN
        );

        std::cout << "BEGIN append and flush: PASS\n";


        LogRecord commitRecord;

        commitRecord.txn_id = 1;
        commitRecord.prev_lsn = beginLSN;
        commitRecord.type = LogRecordType::COMMIT;

        const auto commitBytes =
            EncodeLogRecord(commitRecord);

        commitLSN =
            manager.AppendLogRecord(commitRecord);

        assert(
            commitLSN == beginBytes.size()
        );

        manager.Flush();

        assert(
            manager.GetPersistentLSN() == commitLSN
        );

        std::cout << "COMMIT append and flush: PASS\n";

        expectedFileSize =
            beginBytes.size() + commitBytes.size();
    }


    // ---------------------------------------------
    // TEST 2: Create an incomplete WAL tail.
    // ---------------------------------------------

    {
        std::ofstream file(
            testFile,
            std::ios::binary | std::ios::app
        );

        assert(file.is_open());

        const char incompleteBytes[] = {
            'M', 'D', 'B'
        };

        file.write(
            incompleteBytes,
            sizeof(incompleteBytes)
        );

        assert(file.good());
    }

    std::cout << "Incomplete WAL tail created: PASS\n";


    // ---------------------------------------------
    // TEST 3: Scan WAL and repair incomplete tail.
    // ---------------------------------------------

    {
        LogManager manager(testFile);

        const auto records =
            manager.ReadAllRecords(true);

        assert(records.size() == 2);

        assert(
            records[0].type == LogRecordType::BEGIN
        );

        assert(
            records[1].type == LogRecordType::COMMIT
        );

        assert(records[0].lsn == beginLSN);
        assert(records[1].lsn == commitLSN);

        std::cout << "WAL scan and decode: PASS\n";


        // Check that the three incomplete bytes were removed.
        assert(
            std::filesystem::file_size(testFile) ==
            expectedFileSize
        );

        assert(
            manager.GetNextLSN() == expectedFileSize
        );

        std::cout << "Incomplete tail repair: PASS\n";


        // -----------------------------------------
        // TEST 4: Append after repairing the WAL.
        // -----------------------------------------

        LogRecord nextRecord;

        nextRecord.txn_id = 2;
        nextRecord.type = LogRecordType::BEGIN;

        const lsn_t nextLSN =
            manager.AppendLogRecord(nextRecord);

        assert(nextLSN == expectedFileSize);

        manager.Flush();

        assert(
            manager.GetPersistentLSN() == nextLSN
        );

        std::cout << "Append after recovery: PASS\n";
    }


    // Remove test WAL file.
    std::filesystem::remove(testFile);

    std::cout << "\nAll WAL scanning tests passed!\n";

    return 0;
}
