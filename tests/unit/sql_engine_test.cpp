
#include "minidb/catalog/catalog.hpp"
#include "minidb/storage/disk_manager.hpp"
#include "minidb/buffer/buffer_pool.hpp"

#include "minidb/recovery/log_manager.hpp"
#include "minidb/concurrency/lock_manager.hpp"
#include "minidb/transaction/txn_manager.hpp"

#include "minidb/sql/sql_engine.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace minidb;

ExecutionResult ExecuteSQL(
    SqlEngine& engine,
    session_id_t session,
    const std::string& sql
) {

    auto results = engine.Execute(session, sql);

    assert(!results.empty());

    return results.back();
}

int main() {

    const std::string database_file =
        "data/sql_engine_test.db";

    const std::string wal_file =
        "data/sql_engine_test.wal";

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

        Catalog catalog;

        TxnManager txn_manager(log, &locks);

        SqlEngine engine(
            catalog,
            pool,
            txn_manager
        );


        // ==========================================
        // TEST 1: Create two independent sessions
        // ==========================================

        const session_id_t client_a =
            engine.OpenSession();

        const session_id_t client_b =
            engine.OpenSession();

        assert(client_a != client_b);

        assert(engine.GetSessionCount() == 2);

        std::cout << "Independent client sessions: PASS\n";


        // ==========================================
        // TEST 2: CREATE TABLE
        // ==========================================

        ExecuteSQL(
            engine,
            client_a,
            "CREATE TABLE students ("
            "id BIGINT PRIMARY KEY, "
            "name VARCHAR(50) NOT NULL"
            ");"
        );

        assert(catalog.HasTable("students"));

        std::cout << "SQL Engine CREATE TABLE: PASS\n";


        // ==========================================
        // TEST 3: Client A inserts a row
        // ==========================================

        const auto inserted = ExecuteSQL(
            engine,
            client_a,
            "INSERT INTO students (id, name) "
            "VALUES (1, 'Aman');"
        );

        assert(inserted.rows_affected == 1);

        std::cout << "Client A SQL execution: PASS\n";


        // ==========================================
        // TEST 4: Client B reads shared database
        // ==========================================

        const auto selected = ExecuteSQL(
            engine,
            client_b,
            "SELECT id, name FROM students "
            "WHERE id = 1;"
        );

        assert(selected.rows.size() == 1);

        assert(
            std::get<std::int64_t>(
                selected.rows[0][0]
            ) == 1
        );

        assert(
            std::get<std::string>(
                selected.rows[0][1]
            ) == "Aman"
        );

        std::cout << "Client B shared database access: PASS\n";


        // ==========================================
        // TEST 5: Session-specific transaction
        // ==========================================

        ExecuteSQL(
            engine,
            client_a,
            "BEGIN;"
        );

        ExecuteSQL(
            engine,
            client_a,
            "INSERT INTO students (id, name) "
            "VALUES (2, 'Temporary');"
        );

        assert(txn_manager.GetActiveTransactionCount() == 1);

        std::cout << "Independent session transaction: PASS\n";


        // ==========================================
        // TEST 6: Disconnect triggers rollback
        // ==========================================

        assert(engine.CloseSession(client_a));

        assert(engine.GetSessionCount() == 1);

        assert(txn_manager.GetActiveTransactionCount() == 0);

        std::cout << "Disconnect rollback: PASS\n";


        // ==========================================
        // TEST 7: Verify rollback from Client B
        // ==========================================

        const auto count = ExecuteSQL(
            engine,
            client_b,
            "SELECT COUNT(*) FROM students;"
        );

        assert(count.rows.size() == 1);

        assert(
            std::get<std::int64_t>(
                count.rows[0][0]
            ) == 1
        );

        std::cout << "Disconnected transaction data rollback: PASS\n";


        // ==========================================
        // TEST 8: Closed session rejection
        // ==========================================

        bool rejected = false;

        try {
            engine.Execute(
                client_a,
                "SELECT 1;"
            );
        }
        catch (const MiniDBException&) {
            rejected = true;
        }

        assert(rejected);

        std::cout << "Closed session rejection: PASS\n";


        // ==========================================
        // TEST 9: Session registry cleanup
        // ==========================================

        assert(engine.CloseSession(client_b));

        assert(engine.GetSessionCount() == 0);

        assert(txn_manager.GetActiveTransactionCount() == 0);

        std::cout << "Session registry cleanup: PASS\n";


        pool.FlushAllPages();
    }

    std::filesystem::remove(database_file);
    std::filesystem::remove(wal_file);

    std::cout << "\nAll SQL Engine tests passed!\n";

    return 0;
}
