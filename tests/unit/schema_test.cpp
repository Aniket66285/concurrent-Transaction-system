
#include "minidb/catalog/schema.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <string>
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


int main() {

    // ---------------------------------------------
    // TEST 1: Create a valid schema.
    // ---------------------------------------------

    Schema schema({
        {"id", ColumnType::INT64, false, 0},
        {"name", ColumnType::VARCHAR, false, 100},
        {"cgpa", ColumnType::DOUBLE, true, 0},
        {"active", ColumnType::BOOLEAN, true, 0}
    });

    assert(schema.GetColumnCount() == 4);

    std::cout << "Schema creation: PASS\n";


    // ---------------------------------------------
    // TEST 2: Verify column order.
    // ---------------------------------------------

    assert(schema.GetColumn(0).name == "id");

    assert(schema.GetColumn(1).name == "name");

    assert(schema.GetColumn(2).name == "cgpa");

    assert(schema.GetColumn(3).name == "active");

    std::cout << "Column ordering: PASS\n";


    // ---------------------------------------------
    // TEST 3: Column properties.
    // ---------------------------------------------

    assert(schema.GetColumn(0).type == ColumnType::INT64);

    assert(!schema.GetColumn(0).nullable);

    assert(schema.GetColumn(1).type == ColumnType::VARCHAR);

    assert(schema.GetColumn(1).max_length == 100);

    assert(schema.GetColumn(2).nullable);

    assert(schema.GetColumn(3).type == ColumnType::BOOLEAN);

    std::cout << "Column properties: PASS\n";


    // ---------------------------------------------
    // TEST 4: Find column index.
    // ---------------------------------------------

    assert(schema.GetColumnIndex("id") == 0);

    assert(schema.GetColumnIndex("name") == 1);

    assert(schema.GetColumnIndex("cgpa") == 2);

    assert(schema.GetColumnIndex("active") == 3);

    std::cout << "Column index lookup: PASS\n";


    // ---------------------------------------------
    // TEST 5: FindColumnIndex.
    // ---------------------------------------------

    const auto nameIndex =
        schema.FindColumnIndex("name");

    assert(nameIndex.has_value());

    assert(*nameIndex == 1);

    const auto missingIndex =
        schema.FindColumnIndex("address");

    assert(!missingIndex.has_value());

    std::cout << "Optional column lookup: PASS\n";


    // ---------------------------------------------
    // TEST 6: Column existence.
    // ---------------------------------------------

    assert(schema.HasColumn("id"));

    assert(schema.HasColumn("name"));

    assert(!schema.HasColumn("address"));

    std::cout << "Column existence checking: PASS\n";


    // ---------------------------------------------
    // TEST 7: Retrieve column using name.
    // ---------------------------------------------

    const Column& nameColumn =
        schema.GetColumn("name");

    assert(nameColumn.type == ColumnType::VARCHAR);

    assert(nameColumn.max_length == 100);

    std::cout << "Column lookup by name: PASS\n";


    // ---------------------------------------------
    // TEST 8: Exact case sensitivity.
    // ---------------------------------------------

    assert(schema.HasColumn("name"));

    assert(!schema.HasColumn("Name"));

    std::cout << "Exact identifier matching: PASS\n";


    // ---------------------------------------------
    // TEST 9: Missing column rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([&]() {
            schema.GetColumnIndex("address");
        })
    );

    std::cout << "Missing column rejection: PASS\n";


    // ---------------------------------------------
    // TEST 10: Invalid column index.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([&]() {
            schema.GetColumn(10);
        })
    );

    std::cout << "Invalid column index rejection: PASS\n";


    // ---------------------------------------------
    // TEST 11: Duplicate column rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([]() {

            Schema invalid({
                {"id", ColumnType::INT64, false, 0},
                {"name", ColumnType::VARCHAR, true, 50},
                {"id", ColumnType::DOUBLE, true, 0}
            });

        })
    );

    std::cout << "Duplicate column rejection: PASS\n";


    // ---------------------------------------------
    // TEST 12: Empty column name rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([]() {

            Schema invalid({
                {"", ColumnType::INT64, false, 0}
            });

        })
    );

    std::cout << "Empty column name rejection: PASS\n";


    // ---------------------------------------------
    // TEST 13: Empty schema rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([]() {

            Schema invalid(
                std::vector<Column>{}
            );

        })
    );

    std::cout << "Empty schema rejection: PASS\n";


    // ---------------------------------------------
    // TEST 14: Invalid max_length rejection.
    // ---------------------------------------------

    assert(
        ThrowsMiniDBException([]() {

            Schema invalid({
                {"id", ColumnType::INT64, false, 100}
            });

        })
    );

    std::cout << "Invalid maximum length rejection: PASS\n";


    // ---------------------------------------------
    // TEST 15: VARCHAR without explicit limit.
    // ---------------------------------------------

    Schema unlimitedVarchar({
        {"description", ColumnType::VARCHAR, true, 0}
    });

    assert(
        unlimitedVarchar.GetColumn(0).max_length == 0
    );

    std::cout << "Unlimited VARCHAR support: PASS\n";


    // ---------------------------------------------
    // TEST 16: Column type conversion.
    // ---------------------------------------------

    assert(
        std::string(
            ColumnTypeToString(ColumnType::INT64)
        ) == "INT64"
    );

    assert(
        std::string(
            ColumnTypeToString(ColumnType::DOUBLE)
        ) == "DOUBLE"
    );

    assert(
        std::string(
            ColumnTypeToString(ColumnType::VARCHAR)
        ) == "VARCHAR"
    );

    assert(
        std::string(
            ColumnTypeToString(ColumnType::BOOLEAN)
        ) == "BOOLEAN"
    );

    std::cout << "Column type conversion: PASS\n";


    std::cout << "\nAll Schema tests passed!\n";

    return 0;
}
