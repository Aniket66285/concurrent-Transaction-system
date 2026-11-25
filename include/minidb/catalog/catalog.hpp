
#pragma once

#include "minidb/catalog/schema.hpp"
#include "minidb/common/types.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

class TableMetadata {
public:

    TableMetadata(
        table_id_t table_id,
        std::string table_name,
        Schema schema
    );

    table_id_t GetId() const noexcept;

    const std::string& GetName() const noexcept;

    const Schema& GetSchema() const noexcept;

private:

    table_id_t table_id_;

    std::string table_name_;

    Schema schema_;
};


class Catalog {
public:

    Catalog() = default;

    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    table_id_t CreateTable(
        const std::string& table_name,
        Schema schema
    );

    bool DropTable(const std::string& table_name);

    std::shared_ptr<const TableMetadata> GetTable(
        const std::string& table_name
    ) const;

    std::shared_ptr<const TableMetadata> GetTableById(
        table_id_t table_id
    ) const;

    std::optional<table_id_t> FindTableId(
        const std::string& table_name
    ) const;

    bool HasTable(const std::string& table_name) const;

    std::vector<std::string> GetTableNames() const;

    std::size_t GetTableCount() const;

    void Clear();

private:

    mutable std::mutex mutex_;

    std::map<
        std::string,
        std::shared_ptr<const TableMetadata>
    > tables_by_name_;

    std::map<
        table_id_t,
        std::shared_ptr<const TableMetadata>
    > tables_by_id_;

    table_id_t next_table_id_{1};
};

} // namespace minidb
