
#pragma once

#include "minidb/catalog/schema.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

// NULL is represented by std::monostate.
using FieldValue = std::variant<
    std::monostate,
    std::int64_t,
    double,
    std::string,
    bool
>;


// Represents one row of a database table.
class Tuple {
public:

    // Creates a tuple and validates every value against the schema.
    Tuple(
        const Schema& schema,
        std::vector<FieldValue> values
    );

    // Number of fields in the tuple.
    std::size_t GetFieldCount() const noexcept;

    // Returns the schema associated with this tuple.
    const Schema& GetSchema() const noexcept;

    // Returns all field values.
    const std::vector<FieldValue>& GetValues() const noexcept;

    // Returns a value by column position.
    const FieldValue& GetValue(std::size_t index) const;

    // Returns a value by exact column name.
    const FieldValue& GetValue(
        const std::string& column_name
    ) const;

    // Updates a value by column position.
    void SetValue(
        std::size_t index,
        FieldValue value
    );

    // Updates a value by exact column name.
    void SetValue(
        const std::string& column_name,
        FieldValue value
    );

    // Checks whether a particular field is NULL.
    bool IsNull(std::size_t index) const;

    // Returns a readable representation of the tuple.
    std::string ToString() const;

    // Converts the tuple into a binary representation.
    std::vector<std::uint8_t> Serialize() const;

    // Reconstructs a tuple from binary bytes using a schema.
    static Tuple Deserialize(
        const Schema& schema,
        const std::vector<std::uint8_t>& bytes
    );

private:

    // Validates a value against a column definition.
    static void ValidateValue(
        const Column& column,
        const FieldValue& value
    );

    Schema schema_;

    std::vector<FieldValue> values_;
};

} // namespace minidb
