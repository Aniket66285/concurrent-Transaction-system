
#include "minidb/catalog/schema.hpp"

#include "minidb/common/exception.hpp"

#include <utility>

namespace minidb {


// --------------------------------------------------
// Constructor
// --------------------------------------------------

Schema::Schema(std::vector<Column> columns)
    : columns_(std::move(columns)) {

    if (columns_.empty()) {
        throw MiniDBException(
            "A table schema must contain at least one column"
        );
    }


    // Validate every column.
    for (std::size_t i = 0; i < columns_.size(); ++i) {

        const Column& column = columns_[i];


        // Column names cannot be empty.
        if (column.name.empty()) {
            throw MiniDBException(
                "Column name cannot be empty"
            );
        }


        // Only VARCHAR columns can have a max length.
        if (column.type != ColumnType::VARCHAR &&
            column.max_length != 0) {

            throw MiniDBException(
                "max_length is only valid for VARCHAR columns"
            );
        }


        // Reject duplicate column names.
        const auto result =
            column_indices_.emplace(
                column.name,
                i
            );

        if (!result.second) {
            throw MiniDBException(
                "Duplicate column name: " + column.name
            );
        }
    }
}


// --------------------------------------------------
// GetColumnCount
// --------------------------------------------------

std::size_t Schema::GetColumnCount() const noexcept {

    return columns_.size();
}


// --------------------------------------------------
// GetColumns
// --------------------------------------------------

const std::vector<Column>&
Schema::GetColumns() const noexcept {

    return columns_;
}


// --------------------------------------------------
// GetColumn by index
// --------------------------------------------------

const Column& Schema::GetColumn(
    std::size_t index
) const {

    if (index >= columns_.size()) {
        throw MiniDBException(
            "Column index is out of range"
        );
    }

    return columns_[index];
}


// --------------------------------------------------
// GetColumn by name
// --------------------------------------------------

const Column& Schema::GetColumn(
    const std::string& name
) const {

    return GetColumn(
        GetColumnIndex(name)
    );
}


// --------------------------------------------------
// FindColumnIndex
// --------------------------------------------------

std::optional<std::size_t> Schema::FindColumnIndex(
    const std::string& name
) const {

    const auto iterator =
        column_indices_.find(name);

    if (iterator == column_indices_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}


// --------------------------------------------------
// GetColumnIndex
// --------------------------------------------------

std::size_t Schema::GetColumnIndex(
    const std::string& name
) const {

    const auto index =
        FindColumnIndex(name);

    if (!index.has_value()) {
        throw MiniDBException(
            "Column does not exist: " + name
        );
    }

    return *index;
}


// --------------------------------------------------
// HasColumn
// --------------------------------------------------

bool Schema::HasColumn(
    const std::string& name
) const {

    return column_indices_.find(name) !=
           column_indices_.end();
}


// --------------------------------------------------
// ColumnTypeToString
// --------------------------------------------------

const char* ColumnTypeToString(
    ColumnType type
) noexcept {

    switch (type) {

        case ColumnType::INT64:
            return "INT64";

        case ColumnType::DOUBLE:
            return "DOUBLE";

        case ColumnType::VARCHAR:
            return "VARCHAR";

        case ColumnType::BOOLEAN:
            return "BOOLEAN";

        default:
            return "UNKNOWN";
    }
}


} // namespace minidb
