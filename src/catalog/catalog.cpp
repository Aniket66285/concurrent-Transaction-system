
#include "minidb/catalog/catalog.hpp"

#include "minidb/common/exception.hpp"

#include <limits>
#include <utility>

namespace minidb {


// --------------------------------------------------
// TableMetadata Constructor
// --------------------------------------------------

TableMetadata::TableMetadata(
    table_id_t table_id,
    std::string table_name,
    Schema schema
)
    : table_id_(table_id),
      table_name_(std::move(table_name)),
      schema_(std::move(schema)) {
}


// --------------------------------------------------
// GetId
// --------------------------------------------------

table_id_t TableMetadata::GetId() const noexcept {

    return table_id_;
}


// --------------------------------------------------
// GetName
// --------------------------------------------------

const std::string& TableMetadata::GetName() const noexcept {

    return table_name_;
}


// --------------------------------------------------
// GetSchema
// --------------------------------------------------

const Schema& TableMetadata::GetSchema() const noexcept {

    return schema_;
}


// --------------------------------------------------
// CreateTable
// --------------------------------------------------

table_id_t Catalog::CreateTable(
    const std::string& table_name,
    Schema schema
) {

    if (table_name.empty()) {
        throw MiniDBException(
            "Table name cannot be empty"
        );
    }

    std::lock_guard<std::mutex> lock(mutex_);


    // Reject duplicate table names.
    if (tables_by_name_.find(table_name) !=
        tables_by_name_.end()) {

        throw MiniDBException(
            "Table already exists: " + table_name
        );
    }


    // Reserve zero as an invalid table ID.
    // The maximum value is left unused to avoid overflow.
    if (next_table_id_ == 0 ||
        next_table_id_ ==
            std::numeric_limits<table_id_t>::max()) {

        throw MiniDBException(
            "Table ID limit reached"
        );
    }


    const table_id_t tableId = next_table_id_;


    // Create immutable metadata.
    auto metadata =
        std::make_shared<const TableMetadata>(
            tableId,
            table_name,
            std::move(schema)
        );


    // First insert into the name index.
    const auto nameResult =
        tables_by_name_.emplace(
            table_name,
            metadata
        );

    if (!nameResult.second) {
        throw MiniDBException(
            "Table already exists: " + table_name
        );
    }


    // Insert into the ID index.
    // Roll back the first insertion if this insertion fails.
    try {

        const auto idResult =
            tables_by_id_.emplace(
                tableId,
                metadata
            );

        if (!idResult.second) {

            tables_by_name_.erase(table_name);

            throw MiniDBException(
                "Table ID already exists"
            );
        }
    }
    catch (...) {

        tables_by_name_.erase(table_name);

        throw;
    }


    // Advance the table ID only after successful insertion.
    ++next_table_id_;

    return tableId;
}


// --------------------------------------------------
// DropTable
// --------------------------------------------------

bool Catalog::DropTable(
    const std::string& table_name
) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        tables_by_name_.find(table_name);

    if (iterator == tables_by_name_.end()) {
        return false;
    }


    const table_id_t tableId =
        iterator->second->GetId();


    // Remove both indexes.
    tables_by_id_.erase(tableId);

    tables_by_name_.erase(iterator);

    return true;
}


// --------------------------------------------------
// GetTable
// --------------------------------------------------

std::shared_ptr<const TableMetadata>
Catalog::GetTable(
    const std::string& table_name
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        tables_by_name_.find(table_name);

    if (iterator == tables_by_name_.end()) {
        return nullptr;
    }

    return iterator->second;
}


// --------------------------------------------------
// GetTableById
// --------------------------------------------------

std::shared_ptr<const TableMetadata>
Catalog::GetTableById(
    table_id_t table_id
) const {

    if (table_id == 0) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        tables_by_id_.find(table_id);

    if (iterator == tables_by_id_.end()) {
        return nullptr;
    }

    return iterator->second;
}


// --------------------------------------------------
// FindTableId
// --------------------------------------------------

std::optional<table_id_t> Catalog::FindTableId(
    const std::string& table_name
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        tables_by_name_.find(table_name);

    if (iterator == tables_by_name_.end()) {
        return std::nullopt;
    }

    return iterator->second->GetId();
}


// --------------------------------------------------
// HasTable
// --------------------------------------------------

bool Catalog::HasTable(
    const std::string& table_name
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return tables_by_name_.find(table_name) !=
           tables_by_name_.end();
}


// --------------------------------------------------
// GetTableNames
// --------------------------------------------------

std::vector<std::string> Catalog::GetTableNames() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> names;

    names.reserve(tables_by_name_.size());

    // std::map keeps names in alphabetical order.
    for (const auto& entry : tables_by_name_) {

        names.push_back(entry.first);
    }

    return names;
}


// --------------------------------------------------
// GetTableCount
// --------------------------------------------------

std::size_t Catalog::GetTableCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return tables_by_name_.size();
}


// --------------------------------------------------
// Clear
// --------------------------------------------------

void Catalog::Clear() {

    std::lock_guard<std::mutex> lock(mutex_);

    tables_by_name_.clear();

    tables_by_id_.clear();

    // Do not reset next_table_id_.
    // Old table IDs must not be accidentally reused.
}


} // namespace minidb
