
#include "minidb/catalog/catalog.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/sql/lexer.hpp"
#include "minidb/sql/parser.hpp"
#include "minidb/sql/executor.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace minidb;

ExecutionResult RunSQL(
    Executor& executor,
    const std::string& sql,
    std::shared_ptr<Transaction>& transaction
) {

    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());

    const auto statements = parser.Parse();

    ExecutionResult result;

    for (const Statement& statement : statements) {
        result = executor.Execute(statement, transaction);
    }

    return result;
}

int main() {

    const std::string database_file =
        "data/sql_executor_test.db";

    const std::string wal_file =
        "data/sql_executor_test.wal";

    std::filesystem::remove(database_file);
    std::filesystem::remove(wal_file);

    {
        DiskManager disk(database_file);

        LogManager log(wal_file);

        LockManager locks;

        BufferPool pool(
            disk,
            4,
            [&log]() {
                log.Flush();
            }
        );

        TxnManager txn_manager(log, &locks);

        Catalog catalog;

        Executor executor(
            catalog,
            pool,
            txn_manager
        );

        std::shared_ptr<Transaction> transaction;

        // ==========================================
        // TEST 1: CREATE TABLE
        // ==========================================

        RunSQL(
            executor,
            "CREATE TABLE students ("
            "id BIGINT PRIMARY KEY, "
            "name VARCHAR(50) NOT NULL, "
            "score DOUBLE, "
            "active BOOLEAN DEFAULT TRUE"
            ");",
            transaction
        );

        assert(catalog.HasTable("students"));

        std::cout << "CREATE TABLE execution: PASS\n";


        // ==========================================
        // TEST 2: INSERT multiple rows
        // ==========================================

        const auto inserted = RunSQL(
            executor,
            "INSERT INTO students "
            "(id, name, score, active) VALUES "
            "(1, 'Aman', 8.5, TRUE), "
            "(2, 'Aniket', 7.9, TRUE), "
            "(3, 'Rahul', 9.1, FALSE);",
            transaction
        );

        assert(inserted.rows_affected == 3);

        std::cout << "INSERT execution: PASS\n";


        // ==========================================
        // TEST 3: SELECT with WHERE
        // ORDER BY and LIMIT
        // ==========================================

        const auto selected = RunSQL(
            executor,
            "SELECT name, score FROM students "
            "WHERE score >= 8.0 "
            "ORDER BY score DESC LIMIT 1;",
            transaction
        );

        assert(selected.rows.size() == 1);

        assert(
            std::get<std::string>(selected.rows[0][0]) ==
            "Rahul"
        );

        assert(
            std::get<double>(selected.rows[0][1]) == 9.1
        );

        std::cout << "SELECT WHERE ORDER BY LIMIT: PASS\n";


        // ==========================================
        // TEST 4: COUNT(*)
        // ==========================================

        const auto count = RunSQL(
            executor,
            "SELECT COUNT(*) AS total FROM students;",
            transaction
        );

        assert(count.rows.size() == 1);

        assert(
            std::get<std::int64_t>(count.rows[0][0]) == 3
        );

        std::cout << "COUNT(*) execution: PASS\n";


        // ==========================================
        // TEST 5: UPDATE
        // ==========================================

        const auto updated = RunSQL(
            executor,
            "UPDATE students "
            "SET score = score + 1.0 "
            "WHERE id = 2;",
            transaction
        );

        assert(updated.rows_affected == 1);

        const auto after_update = RunSQL(
            executor,
            "SELECT score FROM students WHERE id = 2;",
            transaction
        );

        assert(
            std::get<double>(after_update.rows[0][0]) == 8.9
        );

        std::cout << "UPDATE execution: PASS\n";


        // ==========================================
        // TEST 6: DELETE
        // ==========================================

        const auto deleted = RunSQL(
            executor,
            "DELETE FROM students WHERE id = 1;",
            transaction
        );

        assert(deleted.rows_affected == 1);

        const auto after_delete = RunSQL(
            executor,
            "SELECT COUNT(*) FROM students;",
            transaction
        );

        assert(
            std::get<std::int64_t>(
                after_delete.rows[0][0]
            ) == 2
        );

        std::cout << "DELETE execution: PASS\n";


        // ==========================================
        // TEST 7: TRANSACTION ROLLBACK
        // ==========================================

        RunSQL(
            executor,
            "BEGIN;",
            transaction
        );

        RunSQL(
            executor,
            "INSERT INTO students "
            "(id, name, score, active) "
            "VALUES (4, 'Temporary', 6.0, TRUE);",
            transaction
        );

        assert(transaction != nullptr);

        RunSQL(
            executor,
            "ROLLBACK;",
            transaction
        );

        assert(transaction == nullptr);

        const auto after_rollback = RunSQL(
            executor,
            "SELECT COUNT(*) FROM students;",
            transaction
        );

        assert(
            std::get<std::int64_t>(
                after_rollback.rows[0][0]
            ) == 2
        );

        std::cout << "BEGIN and ROLLBACK execution: PASS\n";


        // ==========================================
        // TEST 8: COMMIT
        // ==========================================

        RunSQL(
            executor,
            "BEGIN;",
            transaction
        );

        RunSQL(
            executor,
            "INSERT INTO students "
            "(id, name, score, active) "
            "VALUES (5, 'Committed', 9.5, TRUE);",
            transaction
        );

        RunSQL(
            executor,
            "COMMIT;",
            transaction
        );

        assert(transaction == nullptr);

        const auto after_commit = RunSQL(
            executor,
            "SELECT COUNT(*) FROM students;",
            transaction
        );

        assert(
            std::get<std::int64_t>(
                after_commit.rows[0][0]
            ) == 3
        );

        std::cout << "BEGIN and COMMIT execution: PASS\n";


        // ==========================================
        // TEST 9: PRIMARY KEY uniqueness
        // ==========================================

        bool duplicate_rejected = false;

        try {

            RunSQL(
                executor,
                "INSERT INTO students "
                "(id, name, score, active) "
                "VALUES (5, 'Duplicate', 7.0, TRUE);",
                transaction
            );
        }
        catch (const MiniDBException&) {
            duplicate_rejected = true;
        }

        assert(duplicate_rejected);

        assert(transaction == nullptr);

        std::cout << "PRIMARY KEY uniqueness: PASS\n";


        // ==========================================
        // TEST 10: DROP TABLE
        // ==========================================

        RunSQL(
            executor,
            "DROP TABLE students;",
            transaction
        );

        assert(!catalog.HasTable("students"));

        std::cout << "DROP TABLE execution: PASS\n";

        assert(txn_manager.GetActiveTransactionCount() == 0);

        pool.FlushAllPages();
    }

    std::filesystem::remove(database_file);
    std::filesystem::remove(wal_file);

    std::cout << "\nAll SQL Executor tests passed!\n";

    return 0;
}
