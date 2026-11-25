
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// Supported database column types.
enum class ColumnType {
    INT64,
    DOUBLE,
    VARCHAR,
    BOOLEAN
};


// Describes one column of a database table.
struct Column {

    std::string name;

    ColumnType type{ColumnType::INT64};

    // Whether this column accepts NULL.
    bool nullable{true};

    // Maximum VARCHAR length in bytes.
    // Zero means no explicit length limit.
    std::size_t max_length{0};
};


class Schema {
public:

    // Constructs and validates a table schema.
    explicit Schema(std::vector<Column> columns);

    // Number of columns.
    std::size_t GetColumnCount() const noexcept;

    // Returns all columns in their original order.
    const std::vector<Column>& GetColumns() const noexcept;

    // Returns a column using its position.
    const Column& GetColumn(std::size_t index) const;

    // Returns a column using its exact name.
    const Column& GetColumn(const std::string& name) const;

    // Finds a column index.
    // Returns nullopt if the column does not exist.
    std::optional<std::size_t> FindColumnIndex(
        const std::string& name
    ) const;

    // Finds a column index.
    // Throws an exception if the column does not exist.
    std::size_t GetColumnIndex(
        const std::string& name
    ) const;

    // Checks whether the schema contains a column.
    bool HasColumn(const std::string& name) const;

private:

    std::vector<Column> columns_;

    // Maps exact column names to their positions.
    std::unordered_map<std::string, std::size_t> column_indices_;
};


// Converts a column type to a readable string.
const char* ColumnTypeToString(ColumnType type) noexcept;

} // namespace minidb
