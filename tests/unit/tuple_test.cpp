
#include "minidb/catalog/tuple.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace minidb;

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

Schema CreateStudentSchema() {

    return Schema({
        {"id", ColumnType::INT64, false, 0},
        {"name", ColumnType::VARCHAR, false, 20},
        {"cgpa", ColumnType::DOUBLE, true, 0},
        {"active", ColumnType::BOOLEAN, false, 0},
        {"note", ColumnType::VARCHAR, true, 50}
    });
}

int main() {

    const Schema schema = CreateStudentSchema();


    // TEST 1: Create valid tuple.
    Tuple tuple(
        schema,
        {
            std::int64_t{-42},
            std::string{"Aman"},
            8.5,
            true,
            std::monostate{}
        }
    );

    assert(tuple.GetFieldCount() == 5);

    std::cout << "Tuple creation: PASS\n";


    // TEST 2: Read values by index.
    assert(std::get<std::int64_t>(tuple.GetValue(0)) == -42);
    assert(std::get<std::string>(tuple.GetValue(1)) == "Aman");
    assert(std::get<double>(tuple.GetValue(2)) == 8.5);
    assert(std::get<bool>(tuple.GetValue(3)));

    std::cout << "Value lookup by index: PASS\n";


    // TEST 3: Read values by column name.
    assert(std::get<std::int64_t>(tuple.GetValue("id")) == -42);
    assert(std::get<std::string>(tuple.GetValue("name")) == "Aman");
    assert(std::get<double>(tuple.GetValue("cgpa")) == 8.5);
    assert(std::get<bool>(tuple.GetValue("active")));

    std::cout << "Value lookup by column name: PASS\n";


    // TEST 4: NULL support.
    assert(!tuple.IsNull(0));
    assert(!tuple.IsNull(1));
    assert(tuple.IsNull(4));

    assert(
        std::holds_alternative<std::monostate>(
            tuple.GetValue("note")
        )
    );

    std::cout << "NULL value handling: PASS\n";


    // TEST 5: Update value by index.
    tuple.SetValue(2, 9.1);

    assert(std::get<double>(tuple.GetValue(2)) == 9.1);

    std::cout << "Update value by index: PASS\n";


    // TEST 6: Update value by column name.
    tuple.SetValue("name", std::string{"Aniket"});

    assert(
        std::get<std::string>(tuple.GetValue("name")) ==
        "Aniket"
    );

    std::cout << "Update value by column name: PASS\n";


    // TEST 7: Set NULL.
    tuple.SetValue("cgpa", std::monostate{});

    assert(tuple.IsNull(2));

    std::cout << "Set NULL value: PASS\n";


    // TEST 8: Restore non-NULL value.
    tuple.SetValue("cgpa", 9.5);

    assert(!tuple.IsNull(2));
    assert(std::get<double>(tuple.GetValue("cgpa")) == 9.5);

    std::cout << "Restore non-NULL value: PASS\n";


    // TEST 9: Wrong data type.
    assert(
        ThrowsMiniDBException([&]() {
            Tuple invalid(
                schema,
                {
                    std::string{"101"},
                    std::string{"Aman"},
                    8.5,
                    true,
                    std::monostate{}
                }
            );
        })
    );

    std::cout << "Invalid data type rejection: PASS\n";


    // TEST 10: NULL in NOT NULL column.
    assert(
        ThrowsMiniDBException([&]() {
            Tuple invalid(
                schema,
                {
                    std::monostate{},
                    std::string{"Aman"},
                    8.5,
                    true,
                    std::monostate{}
                }
            );
        })
    );

    std::cout << "NOT NULL constraint: PASS\n";


    // TEST 11: VARCHAR maximum length.
    assert(
        ThrowsMiniDBException([&]() {
            Tuple invalid(
                schema,
                {
                    std::int64_t{101},
                    std::string{"ThisNameIsTooLongForColumn"},
                    8.5,
                    true,
                    std::monostate{}
                }
            );
        })
    );

    std::cout << "VARCHAR maximum length: PASS\n";


    // TEST 12: Wrong number of fields.
    assert(
        ThrowsMiniDBException([&]() {
            Tuple invalid(
                schema,
                {
                    std::int64_t{101},
                    std::string{"Aman"}
                }
            );
        })
    );

    std::cout << "Tuple field count validation: PASS\n";


    // TEST 13: Invalid field index.
    assert(
        ThrowsMiniDBException([&]() {
            tuple.GetValue(20);
        })
    );

    assert(
        ThrowsMiniDBException([&]() {
            tuple.SetValue(20, std::int64_t{100});
        })
    );

    std::cout << "Invalid field index rejection: PASS\n";


    // TEST 14: Missing column.
    assert(
        ThrowsMiniDBException([&]() {
            tuple.GetValue("address");
        })
    );

    std::cout << "Missing column rejection: PASS\n";


    // TEST 15: Failed update preserves old value.
    const std::string oldName =
        std::get<std::string>(tuple.GetValue("name"));

    assert(
        ThrowsMiniDBException([&]() {
            tuple.SetValue(
                "name",
                std::string{"A name that is far too long"}
            );
        })
    );

    assert(
        std::get<std::string>(tuple.GetValue("name")) ==
        oldName
    );

    std::cout << "Failed update preserves old value: PASS\n";


    // TEST 16: Readable representation.
    const std::string display = tuple.ToString();

    assert(display.find("id=-42") != std::string::npos);
    assert(display.find("name=\"Aniket\"") != std::string::npos);
    assert(display.find("cgpa=9.5") != std::string::npos);
    assert(display.find("active=true") != std::string::npos);
    assert(display.find("note=NULL") != std::string::npos);

    std::cout << "Tuple readable representation: PASS\n";


    // TEST 17: Serialization.
    const std::vector<std::uint8_t> encoded =
        tuple.Serialize();

    assert(!encoded.empty());
    assert(encoded.size() >= 8);

    assert(encoded[0] == 'T');
    assert(encoded[1] == 'U');
    assert(encoded[2] == 'P');
    assert(encoded[3] == 'L');

    std::cout << "Tuple binary serialization: PASS\n";


    // TEST 18: Deserialization.
    Tuple decoded = Tuple::Deserialize(schema, encoded);

    assert(decoded.GetFieldCount() == tuple.GetFieldCount());
    assert(decoded.GetValues() == tuple.GetValues());
    assert(decoded.ToString() == tuple.ToString());

    std::cout << "Tuple binary deserialization: PASS\n";


    // TEST 19: Corrupted magic.
    auto corrupted = encoded;

    corrupted[0] = 0x00;

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(schema, corrupted);
        })
    );

    std::cout << "Corrupted signature rejection: PASS\n";


    // TEST 20: Truncated tuple.
    auto truncated = encoded;

    truncated.pop_back();

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(schema, truncated);
        })
    );

    std::cout << "Truncated tuple rejection: PASS\n";


    // TEST 21: Trailing bytes.
    auto trailing = encoded;

    trailing.push_back(0x00);

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(schema, trailing);
        })
    );

    std::cout << "Trailing byte rejection: PASS\n";


    // TEST 22: Invalid NULL marker.
    auto invalidNullMarker = encoded;

    // First field's NULL marker is at byte offset 8.
    invalidNullMarker[8] = 2;

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(schema, invalidNullMarker);
        })
    );

    std::cout << "Invalid NULL marker rejection: PASS\n";


    // TEST 23: Invalid BOOLEAN encoding.
    auto invalidBoolean = encoded;

    // Header: 8 bytes
    // INT64:  9 bytes
    // VARCHAR "Aniket": 11 bytes
    // DOUBLE: 9 bytes
    // BOOLEAN payload: byte 38.
    invalidBoolean[38] = 2;

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(schema, invalidBoolean);
        })
    );

    std::cout << "Invalid BOOLEAN rejection: PASS\n";


    // TEST 24: Schema field-count mismatch.
    const Schema smallerSchema({
        {"id", ColumnType::INT64, false, 0}
    });

    assert(
        ThrowsMiniDBException([&]() {
            Tuple::Deserialize(smallerSchema, encoded);
        })
    );

    std::cout << "Deserialization schema validation: PASS\n";


    std::cout << "\nAll Tuple tests passed!\n";

    return 0;
}
