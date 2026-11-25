
#include "minidb/catalog/catalog.hpp"
#include "minidb/common/exception.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace minidb;


// Helper to verify expected exceptions.
bool ThrowsMiniDBException(
    const std::function<void()>& operation
) {

    try {
        operation();
    }
    catch (const MiniDBException&) {
        return true;
    }

    return false;
}


// Creates a reusable student schema.
Schema CreateStudentSchema() {

    return Schema({
        {"id", ColumnType::INT64, false, 0},
        {"name", ColumnType::VARCHAR, false, 100},
        {"cgpa", ColumnType::DOUBLE, true, 0}
    });
}


// Creates a simple one-column schema.
Schema CreateSimpleSchema() {

    return Schema({
        {"id", ColumnType::INT64, false, 0}
    });
}


int main() {

    Catalog catalog;


    // ---------------------------------------------
    // TEST 1: Empty catalog.
    // ---------------------------------------------

    assert(catalog.GetTableCount() == 0);

    assert(catalog.GetTableNames().empty());

    assert(!catalog.HasTable("students"));

    std::cout << "Empty catalog: PASS\n";


    // ---------------------------------------------
    // TEST 2: Create a table.
    // ---------------------------------------------

    const table_id_t studentsId =
        catalog.CreateTable(
            "students",
            CreateStudentSchema()
        );

    assert(studentsId == 1);

    assert(catalog.GetTableCount() == 1);

    std::cout << "Table creation: PASS\n";


    // ---------------------------------------------
    // TEST 3: Get table by name.
    // ---------------------------------------------

    const auto students =
        catalog.GetTable("students");

    assert(students != nullptr);

    assert(students->GetId() == studentsId);

    assert(students->GetName() == "students");

    std::cout << "Table lookup by name: PASS\n";


    // ---------------------------------------------
    // TEST 4: Get table by ID.
    // ---------------------------------------------

    const auto tableById =
        catalog.GetTableById(studentsId);

    assert(tableById != nullptr);

    assert(tableById->GetName() == "students");

    std::cout << "Table lookup by ID: PASS\n";


    // ---------------------------------------------
    // TEST 5: Find table ID.
    // ---------------------------------------------

    const auto foundId =
        catalog.FindTableId("students");

    assert(foundId.has_value());

    assert(*foundId == studentsId);

    const auto missingId =
        catalog.FindTableId("unknown");

    assert(!missingId.has_value());

    std::cout << "Optional table ID lookup: PASS\n";


    // ---------------------------------------------
    // TEST 6: Table existence.
    // ---------------------------------------------

    assert(catalog.HasTable("students"));

    assert(!catalog.HasTable("employees"));

    std::cout << "Table existence checking: PASS\n";


    // ---------------------------------------------
    // TEST 7: Schema metadata.
    // ---------------------------------------------

    assert(
        students->GetSchema().GetColumnCount() == 3
    );

    assert(
        students->GetSchema().GetColumn(0).name == "id"
    );

    assert(
        students->GetSchema().GetColumn(1).name == "name"
    );

    assert(
        students->GetSchema().GetColumn(2).name == "cgpa"
    );

    std::cout << "Table schema metadata: PASS\n";


    // ---------------------------------------------
    // TEST 8: Duplicate table rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([&]() {

            catalog.CreateTable(
                "students",
                CreateSimpleSchema()
            );

        })
    );

    assert(catalog.GetTableCount() == 1);

    std::cout << "Duplicate table rejection: PASS\n";


    // ---------------------------------------------
    // TEST 9: Empty table name rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([&]() {

            catalog.CreateTable(
                "",
                CreateSimpleSchema()
            );

        })
    );

    std::cout << "Empty table name rejection: PASS\n";


    // ---------------------------------------------
    // TEST 10: Exact table-name matching.
    // ---------------------------------------------

    assert(!catalog.HasTable("Students"));

    assert(!catalog.HasTable("STUDENTS"));

    std::cout << "Exact table-name matching: PASS\n";


    // ---------------------------------------------
    // TEST 11: Alphabetical table listing.
    // ---------------------------------------------

    const table_id_t employeesId =
        catalog.CreateTable(
            "employees",
            CreateSimpleSchema()
        );

    const table_id_t coursesId =
        catalog.CreateTable(
            "courses",
            CreateSimpleSchema()
        );

    const auto names =
        catalog.GetTableNames();

    const std::vector<std::string> expectedNames = {
        "courses",
        "employees",
        "students"
    };

    assert(names == expectedNames);

    assert(catalog.GetTableCount() == 3);

    std::cout << "Alphabetical table listing: PASS\n";


    // ---------------------------------------------
    // TEST 12: Drop a table.
    // ---------------------------------------------

    assert(catalog.DropTable("students"));

    assert(!catalog.HasTable("students"));

    assert(catalog.GetTableCount() == 2);

    assert(
        catalog.GetTableById(studentsId) == nullptr
    );

    // Existing metadata snapshots remain valid.
    assert(students->GetName() == "students");

    assert(students->GetSchema().GetColumnCount() == 3);

    std::cout << "Table removal: PASS\n";

    std::cout << "Metadata snapshot lifetime: PASS\n";


    // ---------------------------------------------
    // TEST 13: Dropping a missing table.
    // ---------------------------------------------

    assert(!catalog.DropTable("unknown"));

    assert(catalog.GetTableCount() == 2);

    std::cout << "Missing table drop handling: PASS\n";


    // ---------------------------------------------
    // TEST 14: Table IDs are not reused after DROP.
    // ---------------------------------------------

    const table_id_t departmentsId =
        catalog.CreateTable(
            "departments",
            CreateSimpleSchema()
        );

    assert(departmentsId > employeesId);

    assert(departmentsId > coursesId);

    assert(departmentsId == 4);

    std::cout << "Monotonic table ID allocation: PASS\n";


    // ---------------------------------------------
    // TEST 15: Clear catalog.
    // ---------------------------------------------

    catalog.Clear();

    assert(catalog.GetTableCount() == 0);

    assert(catalog.GetTableNames().empty());

    assert(!catalog.HasTable("employees"));

    assert(catalog.GetTableById(0) == nullptr);

    std::cout << "Catalog clear: PASS\n";


    // ---------------------------------------------
    // TEST 16: Table IDs are not reused after CLEAR.
    // ---------------------------------------------

    const table_id_t logsId =
        catalog.CreateTable(
            "logs",
            CreateSimpleSchema()
        );

    assert(logsId == 5);

    assert(catalog.GetTableCount() == 1);

    std::cout << "Table ID preservation after clear: PASS\n";


    // ---------------------------------------------
    // TEST 17: Concurrent table creation.
    // ---------------------------------------------

    Catalog concurrentCatalog;

    constexpr int kThreadCount = 4;

    constexpr int kTablesPerThread = 25;

    std::vector<std::thread> workers;

    std::vector<table_id_t> allocatedIds;

    std::mutex idsMutex;


    for (int i = 0; i < kThreadCount; ++i) {

        workers.emplace_back(
            [&, i]() {

                for (int j = 0;
                     j < kTablesPerThread;
                     ++j) {

                    const std::string tableName =
                        "table_" +
                        std::to_string(i) +
                        "_" +
                        std::to_string(j);

                    const table_id_t tableId =
                        concurrentCatalog.CreateTable(
                            tableName,
                            CreateSimpleSchema()
                        );

                    std::lock_guard<std::mutex> lock(idsMutex);

                    allocatedIds.push_back(tableId);
                }
            }
        );
    }


    for (std::thread& worker : workers) {
        worker.join();
    }


    assert(
        concurrentCatalog.GetTableCount() ==
        kThreadCount * kTablesPerThread
    );


    // All allocated IDs must be unique.
    std::set<table_id_t> uniqueIds(
        allocatedIds.begin(),
        allocatedIds.end()
    );

    assert(
        uniqueIds.size() == allocatedIds.size()
    );

    assert(
        allocatedIds.size() == 100
    );

    std::cout << "Concurrent table creation: PASS\n";

    std::cout << "Unique table ID allocation: PASS\n";


    std::cout << "\nAll Catalog tests passed!\n";

    return 0;
}
