
#pragma once

#include "minidb/catalog/catalog.hpp"
#include "minidb/catalog/tuple.hpp"
#include "minidb/storage/table_heap.hpp"
#include "minidb/transaction/txn_manager.hpp"
#include "minidb/sql/ast.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

struct ExecutionResult {

    std::vector<std::string> columns;

    std::vector<std::vector<FieldValue>> rows;

    std::size_t rows_affected{0};

    std::string message;
};


class Executor {
public:

    Executor(
        Catalog& catalog,
        BufferPool& buffer_pool,
        TxnManager& txn_manager
    );

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    // Executes one parsed statement.
    //
    // Pass the session's current transaction pointer.
    // BEGIN creates one, COMMIT/ROLLBACK clears it.
    // Other DML statements use autocommit when null.
    ExecutionResult Execute(
        const Statement& statement,
        std::shared_ptr<Transaction>& session_transaction
    );

    void AbortTransaction(Transaction& transaction);

private:

    // Statement execution.
    ExecutionResult ExecuteCreateTable(
        const CreateTableStatement& statement
    );

    ExecutionResult ExecuteDropTable(
        const DropTableStatement& statement
    );

    ExecutionResult ExecuteInsert(
        const InsertStatement& statement,
        Transaction& transaction
    );

    ExecutionResult ExecuteSelect(
        const SelectStatement& statement,
        Transaction& transaction
    );

    ExecutionResult ExecuteUpdate(
        const UpdateStatement& statement,
        Transaction& transaction
    );

    ExecutionResult ExecuteDelete(
        const DeleteStatement& statement,
        Transaction& transaction
    );

    // Expression evaluation.
    FieldValue EvaluateExpression(
        const Expression& expression,
        const Tuple* tuple
    ) const;

    bool EvaluatePredicate(
        const Expression& expression,
        const Tuple* tuple
    ) const;

    // Catalog and storage lookup.
    std::shared_ptr<const TableMetadata> GetMetadata(
        const std::string& table_name
    ) const;

    std::shared_ptr<TableHeap> GetHeap(
        const std::string& table_name
    ) const;

    std::vector<ColumnDefinition> GetDefinitions(
        const std::string& table_name
    ) const;

    // Enforce PRIMARY KEY and UNIQUE for single-column
    // constraints declared in CREATE TABLE.
    void ValidateUniqueConstraints(
        const std::string& table_name,
        const Tuple& candidate,
        Transaction& transaction,
        const std::optional<RID>& ignored_rid = std::nullopt
    );

    // Transaction rollback helpers.
    void UndoRecord(const LogRecord& record);

    void FlushPages();

    Catalog& catalog_;

    BufferPool& buffer_pool_;

    TxnManager& txn_manager_;

    // Executor owns the TableHeap objects for its lifetime.
    std::unordered_map<
        std::string,
        std::shared_ptr<TableHeap>
    > heaps_;

    // Schema alone does not retain DEFAULT, UNIQUE and
    // PRIMARY KEY declarations, so keep their definitions.
    std::unordered_map<
        std::string,
        std::vector<ColumnDefinition>
    > definitions_;

    mutable std::mutex mutex_;

    // Serializes uniqueness checks against concurrent writes.
    std::mutex constraint_mutex_;
};

} // namespace minidb
