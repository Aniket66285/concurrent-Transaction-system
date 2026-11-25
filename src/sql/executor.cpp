
#include "minidb/sql/executor.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/recovery/log_record.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_set>
#include <utility>

namespace minidb {

namespace {

// ==================================================
// VALUE HELPERS
// ==================================================

bool IsNullValue(const FieldValue& value) {
    return std::holds_alternative<std::monostate>(value);
}

bool IsNumericValue(const FieldValue& value) {
    return std::holds_alternative<std::int64_t>(value) ||
           std::holds_alternative<double>(value);
}

long double GetNumber(const FieldValue& value) {

    if (const auto* integer =
            std::get_if<std::int64_t>(&value)) {
        return static_cast<long double>(*integer);
    }

    if (const auto* real =
            std::get_if<double>(&value)) {
        return static_cast<long double>(*real);
    }

    throw MiniDBException("Expected a numeric value");
}

bool GetBoolean(const FieldValue& value) {

    if (const auto* boolean =
            std::get_if<bool>(&value)) {
        return *boolean;
    }

    throw MiniDBException(
        "SQL condition must evaluate to BOOLEAN"
    );
}

int CompareValues(
    const FieldValue& left,
    const FieldValue& right
) {

    if (IsNullValue(left) || IsNullValue(right)) {
        throw MiniDBException(
            "Cannot compare NULL using an ordinary comparison"
        );
    }

    if (IsNumericValue(left) && IsNumericValue(right)) {

        const long double a = GetNumber(left);
        const long double b = GetNumber(right);

        if (a < b) return -1;
        if (a > b) return 1;

        return 0;
    }

    if (const auto* a =
            std::get_if<std::string>(&left)) {

        const auto* b =
            std::get_if<std::string>(&right);

        if (b == nullptr) {
            throw MiniDBException("Incompatible comparison types");
        }

        if (*a < *b) return -1;
        if (*a > *b) return 1;

        return 0;
    }

    if (const auto* a =
            std::get_if<bool>(&left)) {

        const auto* b =
            std::get_if<bool>(&right);

        if (b == nullptr) {
            throw MiniDBException("Incompatible comparison types");
        }

        if (*a == *b) return 0;

        return *a ? 1 : -1;
    }

    throw MiniDBException("Incompatible comparison types");
}


// '%' matches zero or more characters.
// '_' matches exactly one character.
bool LikeMatch(
    const std::string& value,
    const std::string& pattern
) {

    const std::size_t n = value.size();
    const std::size_t m = pattern.size();

    std::vector<std::vector<bool>> dp(
        n + 1,
        std::vector<bool>(m + 1, false)
    );

    dp[0][0] = true;

    for (std::size_t j = 1; j <= m; ++j) {

        if (pattern[j - 1] == '%') {
            dp[0][j] = dp[0][j - 1];
        }
    }

    for (std::size_t i = 1; i <= n; ++i) {

        for (std::size_t j = 1; j <= m; ++j) {

            if (pattern[j - 1] == '%') {

                dp[i][j] =
                    dp[i][j - 1] ||
                    dp[i - 1][j];
            }
            else if (pattern[j - 1] == '_' ||
                     pattern[j - 1] == value[i - 1]) {

                dp[i][j] = dp[i - 1][j - 1];
            }
        }
    }

    return dp[n][m];
}


FieldValue Evaluate(
    const Expression& expression,
    const Tuple* tuple
) {

    switch (expression.kind) {

        case ExpressionKind::LITERAL:

            try {

                switch (expression.literal_type) {

                    case TokenType::INTEGER_LITERAL:
                        return static_cast<std::int64_t>(
                            std::stoll(expression.value)
                        );

                    case TokenType::FLOAT_LITERAL:
                        return std::stod(expression.value);

                    case TokenType::STRING_LITERAL:
                        return expression.value;

                    case TokenType::KW_TRUE:
                        return true;

                    case TokenType::KW_FALSE:
                        return false;

                    case TokenType::KW_NULL:
                        return std::monostate{};

                    default:
                        throw MiniDBException(
                            "Unsupported SQL literal"
                        );
                }
            }
            catch (const MiniDBException&) {
                throw;
            }
            catch (...) {
                throw MiniDBException(
                    "Invalid SQL literal: " + expression.value
                );
            }

        case ExpressionKind::COLUMN: {

            if (tuple == nullptr) {
                throw MiniDBException(
                    "Column reference requires a FROM table"
                );
            }

            std::string name = expression.value;

            const auto dot = name.rfind('.');

            if (dot != std::string::npos) {
                name = name.substr(dot + 1);
            }

            return tuple->GetValue(name);
        }

        case ExpressionKind::UNARY: {

            if (expression.children.size() != 1) {
                throw MiniDBException("Invalid unary expression");
            }

            const FieldValue value =
                Evaluate(expression.children[0], tuple);

            if (expression.value == "NOT") {
                return !GetBoolean(value);
            }

            if (!IsNumericValue(value)) {
                throw MiniDBException(
                    "Unary arithmetic requires numeric input"
                );
            }

            if (expression.value == "-") {

                if (const auto* integer =
                        std::get_if<std::int64_t>(&value)) {

                    if (*integer ==
                        std::numeric_limits<std::int64_t>::min()) {
                        throw MiniDBException("Integer overflow");
                    }

                    return -*integer;
                }

                return -static_cast<double>(GetNumber(value));
            }

            return value;
        }

        case ExpressionKind::IS_NULL: {

            if (expression.children.size() != 1) {
                throw MiniDBException("Invalid IS NULL expression");
            }

            const bool is_null = IsNullValue(
                Evaluate(expression.children[0], tuple)
            );

            return expression.negated ? !is_null : is_null;
        }

        case ExpressionKind::BINARY: {

            if (expression.children.size() != 2) {
                throw MiniDBException("Invalid binary expression");
            }

            const FieldValue left =
                Evaluate(expression.children[0], tuple);

            const FieldValue right =
                Evaluate(expression.children[1], tuple);

            const std::string& op = expression.value;

            if (op == "AND") {
                return !IsNullValue(left) &&
                       !IsNullValue(right) &&
                       GetBoolean(left) &&
                       GetBoolean(right);
            }

            if (op == "OR") {
                return (!IsNullValue(left) && GetBoolean(left)) ||
                       (!IsNullValue(right) && GetBoolean(right));
            }

            if (op == "LIKE" || op == "NOT LIKE") {

                if (IsNullValue(left) || IsNullValue(right)) {
                    return false;
                }

                const auto* a =
                    std::get_if<std::string>(&left);

                const auto* b =
                    std::get_if<std::string>(&right);

                if (a == nullptr || b == nullptr) {
                    throw MiniDBException(
                        "LIKE requires string operands"
                    );
                }

                const bool matched = LikeMatch(*a, *b);

                return op == "NOT LIKE" ? !matched : matched;
            }

            if (op == "+" || op == "-" ||
                op == "*" || op == "/" || op == "%") {

                if (!IsNumericValue(left) ||
                    !IsNumericValue(right)) {
                    throw MiniDBException(
                        "Arithmetic requires numeric operands"
                    );
                }

                const long double a = GetNumber(left);
                const long double b = GetNumber(right);

                if ((op == "/" || op == "%") && b == 0) {
                    throw MiniDBException("Division by zero");
                }

                if (op == "%") {

                    const auto* ai =
                        std::get_if<std::int64_t>(&left);

                    const auto* bi =
                        std::get_if<std::int64_t>(&right);

                    if (ai == nullptr || bi == nullptr) {
                        throw MiniDBException(
                            "Modulo requires integer operands"
                        );
                    }

                    if (*ai == std::numeric_limits<std::int64_t>::min() &&
                        *bi == -1) {
                        throw MiniDBException("Integer overflow");
                    }

                    return *ai % *bi;
                }

                const long double calculated =
                    op == "+" ? a + b :
                    op == "-" ? a - b :
                    op == "*" ? a * b :
                                a / b;

                if (!std::isfinite(calculated) ||
                    calculated >
                        std::numeric_limits<double>::max() ||
                    calculated <
                        -std::numeric_limits<double>::max()) {
                    throw MiniDBException("Numeric result overflow");
                }

                if (std::holds_alternative<std::int64_t>(left) &&
                    std::holds_alternative<std::int64_t>(right) &&
                    op != "/") {

                    if (calculated <
                            std::numeric_limits<std::int64_t>::min() ||
                        calculated >
                            std::numeric_limits<std::int64_t>::max()) {
                        throw MiniDBException("Integer overflow");
                    }

                    return static_cast<std::int64_t>(calculated);
                }

                return static_cast<double>(calculated);
            }

            // SQL comparison with NULL returns a non-true
            // predicate in this simplified evaluator.
            if (IsNullValue(left) || IsNullValue(right)) {
                return false;
            }

            const int cmp = CompareValues(left, right);

            if (op == "=") return cmp == 0;
            if (op == "!=" || op == "<>") return cmp != 0;
            if (op == "<") return cmp < 0;
            if (op == "<=") return cmp <= 0;
            if (op == ">") return cmp > 0;
            if (op == ">=") return cmp >= 0;

            throw MiniDBException(
                "Unsupported SQL operator: " + op
            );
        }

        case ExpressionKind::FUNCTION:
            throw MiniDBException(
                "Aggregate functions are evaluated by SELECT"
            );

        case ExpressionKind::STAR:
            throw MiniDBException(
                "Wildcard cannot be evaluated as a scalar"
            );
    }

    throw MiniDBException("Unknown SQL expression");
}


bool MatchesCondition(
    const Expression& expression,
    const Tuple* tuple
) {

    const FieldValue value = Evaluate(expression, tuple);

    return !IsNullValue(value) && GetBoolean(value);
}


std::string GetExpressionLabel(
    const Expression& expression
) {

    if (expression.kind == ExpressionKind::COLUMN) {

        const auto dot = expression.value.rfind('.');

        return dot == std::string::npos
            ? expression.value
            : expression.value.substr(dot + 1);
    }

    if (expression.kind == ExpressionKind::FUNCTION) {
        return expression.value;
    }

    return expression.value.empty()
        ? "expression"
        : expression.value;
}

} // namespace


// ==================================================
// CONSTRUCTOR
// ==================================================

Executor::Executor(
    Catalog& catalog,
    BufferPool& buffer_pool,
    TxnManager& txn_manager
)
    : catalog_(catalog),
      buffer_pool_(buffer_pool),
      txn_manager_(txn_manager) {
}


// ==================================================
// LOOKUPS
// ==================================================

std::shared_ptr<const TableMetadata>
Executor::GetMetadata(const std::string& table_name) const {

    auto metadata = catalog_.GetTable(table_name);

    if (!metadata) {
        throw MiniDBException(
            "Table does not exist: " + table_name
        );
    }

    return metadata;
}


std::shared_ptr<TableHeap>
Executor::GetHeap(const std::string& table_name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = heaps_.find(table_name);

    if (it == heaps_.end()) {
        throw MiniDBException(
            "Table storage is unavailable: " + table_name
        );
    }

    return it->second;
}


std::vector<ColumnDefinition>
Executor::GetDefinitions(const std::string& table_name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = definitions_.find(table_name);

    if (it == definitions_.end()) {
        throw MiniDBException(
            "Table definitions are unavailable: " + table_name
        );
    }

    return it->second;
}


// ==================================================
// ROLLBACK HELPERS
// ==================================================

void Executor::UndoRecord(const LogRecord& record) {

    std::vector<std::shared_ptr<TableHeap>> heaps;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& entry : heaps_) {
            heaps.push_back(entry.second);
        }
    }

    for (const auto& heap : heaps) {

        if (heap->OwnsPage(record.page_id)) {
            heap->UndoRecord(record);
            return;
        }
    }

    throw StorageException(
        "No TableHeap owns WAL page " +
        std::to_string(record.page_id)
    );
}


void Executor::FlushPages() {
    buffer_pool_.FlushAllPages();
}


// ==================================================
// EXECUTE
// ==================================================

ExecutionResult Executor::Execute(
    const Statement& statement,
    std::shared_ptr<Transaction>& session_transaction
) {

    if (std::holds_alternative<BeginStatement>(statement.data)) {

        if (session_transaction) {
            throw TransactionException(
                "Transaction is already active in this session"
            );
        }

        session_transaction =
            txn_manager_.BeginTransaction();

        return {{}, {}, 0, "Transaction started"};
    }

    if (std::holds_alternative<CommitStatement>(statement.data)) {

        if (!session_transaction) {
            throw TransactionException(
                "No active transaction to COMMIT"
            );
        }

        txn_manager_.CommitTransaction(*session_transaction);

        session_transaction.reset();

        return {{}, {}, 0, "Transaction committed"};
    }

    if (std::holds_alternative<RollbackStatement>(statement.data)) {

        if (!session_transaction) {
            throw TransactionException(
                "No active transaction to ROLLBACK"
            );
        }

        txn_manager_.AbortTransaction(
            *session_transaction,
            [&](const LogRecord& record) {
                UndoRecord(record);
            },
            [&]() {
                FlushPages();
            }
        );

        session_transaction.reset();

        return {{}, {}, 0, "Transaction rolled back"};
    }

    if (session_transaction &&
        (std::holds_alternative<CreateTableStatement>(statement.data) ||
         std::holds_alternative<DropTableStatement>(statement.data))) {

        throw TransactionException(
            "CREATE TABLE and DROP TABLE are not transactional yet"
        );
    }

    if (const auto* create =
            std::get_if<CreateTableStatement>(&statement.data)) {
        return ExecuteCreateTable(*create);
    }

    if (const auto* drop =
            std::get_if<DropTableStatement>(&statement.data)) {
        return ExecuteDropTable(*drop);
    }

    const bool autocommit = !session_transaction;

    std::shared_ptr<Transaction> transaction =
        session_transaction;

    if (autocommit) {
        transaction = txn_manager_.BeginTransaction();
    }

    try {

        ExecutionResult result;

        if (const auto* insert =
                std::get_if<InsertStatement>(&statement.data)) {
            result = ExecuteInsert(*insert, *transaction);
        }
        else if (const auto* select =
                     std::get_if<SelectStatement>(&statement.data)) {
            result = ExecuteSelect(*select, *transaction);
        }
        else if (const auto* update =
                     std::get_if<UpdateStatement>(&statement.data)) {
            result = ExecuteUpdate(*update, *transaction);
        }
        else if (const auto* remove =
                     std::get_if<DeleteStatement>(&statement.data)) {
            result = ExecuteDelete(*remove, *transaction);
        }
        else {
            throw MiniDBException("Unsupported SQL statement");
        }

        if (autocommit) {
            txn_manager_.CommitTransaction(*transaction);
        }

        return result;
    }
    catch (...) {

        const std::exception_ptr original_error =
            std::current_exception();

        if (transaction->IsActive()) {

            try {
                txn_manager_.AbortTransaction(
                    *transaction,
                    [&](const LogRecord& record) {
                        UndoRecord(record);
                    },
                    [&]() {
                        FlushPages();
                    }
                );
            }
            catch (...) {

                if (!autocommit) {
                    session_transaction.reset();
                }

                throw;
            }
        }

        if (!autocommit) {
            session_transaction.reset();
        }

        std::rethrow_exception(original_error);
    }
}


// ==================================================
// CREATE TABLE
// ==================================================

ExecutionResult Executor::ExecuteCreateTable(
    const CreateTableStatement& statement
) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (catalog_.HasTable(statement.table_name)) {
        throw MiniDBException(
            "Table already exists: " + statement.table_name
        );
    }

    std::vector<Column> columns;

    for (const ColumnDefinition& definition : statement.columns) {

        Column column;

        column.name = definition.name;
        column.type = definition.type;

        column.nullable =
            definition.primary_key
                ? false
                : definition.nullable;

        column.max_length = definition.max_length;

        columns.push_back(std::move(column));
    }

    Schema schema(std::move(columns));

    auto heap =
        std::make_shared<TableHeap>(buffer_pool_);

    catalog_.CreateTable(
        statement.table_name,
        std::move(schema)
    );

    heaps_[statement.table_name] = std::move(heap);

    definitions_[statement.table_name] =
        statement.columns;

    return {
        {}, {}, 0,
        "Table created: " + statement.table_name
    };
}


// ==================================================
// DROP TABLE
// ==================================================

ExecutionResult Executor::ExecuteDropTable(
    const DropTableStatement& statement
) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!catalog_.DropTable(statement.table_name)) {
        throw MiniDBException(
            "Unknown table: " + statement.table_name
        );
    }

    heaps_.erase(statement.table_name);
    definitions_.erase(statement.table_name);

    return {
        {}, {}, 0,
        "Table dropped: " + statement.table_name
    };
}


// ==================================================
// UNIQUE / PRIMARY KEY CHECK
// ==================================================

void Executor::ValidateUniqueConstraints(
    const std::string& table_name,
    const Tuple& candidate,
    Transaction& transaction,
    const std::optional<RID>& ignored_rid
) {

    // The global constraint mutex serializes INSERT and UPDATE.
    // Use a storage snapshot here rather than waiting for row locks
    // while holding that mutex.
    (void)transaction;

    const auto metadata = GetMetadata(table_name);
    const Schema& schema = metadata->GetSchema();

    const auto definitions = GetDefinitions(table_name);
    const auto heap = GetHeap(table_name);

    const auto rids = heap->GetAllRIDs();

    for (std::size_t index = 0;
         index < definitions.size();
         ++index) {

        const auto& definition = definitions[index];

        if (!definition.primary_key && !definition.unique) {
            continue;
        }

        const FieldValue& value =
            candidate.GetValue(index);

        if (IsNullValue(value)) {

            if (definition.primary_key) {
                throw MiniDBException(
                    "PRIMARY KEY cannot be NULL"
                );
            }

            continue;
        }

        for (const RID& rid : rids) {

            if (ignored_rid && rid == *ignored_rid) {
                continue;
            }

            const auto bytes = heap->GetRecord(rid);

            const Tuple existing =
                Tuple::Deserialize(schema, bytes);

            if (existing.GetValue(index) == value) {
                throw MiniDBException(
                    "Duplicate value in column: " +
                    definition.name
                );
            }
        }
    }
}


// ==================================================
// INSERT
// ==================================================

ExecutionResult Executor::ExecuteInsert(
    const InsertStatement& statement,
    Transaction& transaction
) {

    auto operation_lock =
        transaction.AcquireOperationLock();

    std::lock_guard<std::mutex> constraint_lock(
        constraint_mutex_
    );

    const auto metadata = GetMetadata(statement.table_name);
    const Schema& schema = metadata->GetSchema();

    const auto definitions = GetDefinitions(statement.table_name);
    const auto heap = GetHeap(statement.table_name);

    std::size_t inserted = 0;

    for (const auto& row : statement.rows) {

        std::vector<FieldValue> values(
            schema.GetColumnCount(),
            std::monostate{}
        );

        std::vector<bool> assigned(
            schema.GetColumnCount(),
            false
        );

        if (statement.columns.empty()) {

            if (row.size() != schema.GetColumnCount()) {
                throw MiniDBException(
                    "INSERT value count does not match schema"
                );
            }

            for (std::size_t i = 0; i < row.size(); ++i) {
                values[i] = Evaluate(row[i], nullptr);
                assigned[i] = true;
            }
        }
        else {

            std::unordered_set<std::size_t> used_columns;

            for (std::size_t i = 0; i < statement.columns.size(); ++i) {

                const std::size_t index =
                    schema.GetColumnIndex(statement.columns[i]);

                if (!used_columns.insert(index).second) {
                    throw MiniDBException(
                        "Duplicate column in INSERT list"
                    );
                }

                values[index] = Evaluate(row[i], nullptr);
                assigned[index] = true;
            }
        }

        // Apply defaults to omitted columns.
        for (std::size_t i = 0; i < values.size(); ++i) {

            if (!assigned[i] && definitions[i].default_value) {
                values[i] = Evaluate(
                    *definitions[i].default_value,
                    nullptr
                );
            }
        }

        Tuple tuple(schema, std::move(values));

        ValidateUniqueConstraints(
            statement.table_name,
            tuple,
            transaction
        );

        heap->InsertRecord(
            transaction,
            txn_manager_,
            tuple.Serialize()
        );

        ++inserted;
    }

    return {
        {}, {}, inserted,
        "Rows inserted: " + std::to_string(inserted)
    };
}


// ==================================================
// SELECT
// ==================================================

ExecutionResult Executor::ExecuteSelect(
    const SelectStatement& statement,
    Transaction& transaction
) {

    auto operation_lock =
        transaction.AcquireOperationLock();

    std::shared_ptr<const TableMetadata> metadata;
    std::shared_ptr<TableHeap> heap;
    const Schema* schema = nullptr;

    std::vector<RID> rids;

    if (statement.from_table) {

        metadata = GetMetadata(*statement.from_table);
        heap = GetHeap(*statement.from_table);

        schema = &metadata->GetSchema();

        rids = heap->GetAllRIDs();
    }

    struct SelectedRow {
        std::vector<FieldValue> values;
        std::vector<FieldValue> order_values;
    };

    std::vector<SelectedRow> selected;

    ExecutionResult result;

    for (const SelectItem& item : statement.columns) {

        if (item.expression.kind == ExpressionKind::STAR) {

            if (schema == nullptr) {
                throw MiniDBException(
                    "SELECT * requires a FROM table"
                );
            }

            for (const Column& column : schema->GetColumns()) {
                result.columns.push_back(column.name);
            }
        }
        else {

            result.columns.push_back(
                item.alias.empty()
                    ? GetExpressionLabel(item.expression)
                    : item.alias
            );
        }
    }

    const bool count_query =
        statement.columns.size() == 1 &&
        statement.columns[0].expression.kind ==
            ExpressionKind::FUNCTION &&
        statement.columns[0].expression.value == "COUNT";

    std::int64_t count = 0;

    if (count_query &&
        statement.columns[0].expression.children.size() != 1) {

        throw MiniDBException("Invalid COUNT expression");
    }

    auto process_tuple = [&](const Tuple* tuple) {

        if (statement.where &&
            !MatchesCondition(*statement.where, tuple)) {
            return;
        }

        if (count_query) {

            const Expression& argument =
                statement.columns[0].expression.children[0];

            if (argument.kind == ExpressionKind::STAR ||
                !IsNullValue(Evaluate(argument, tuple))) {
                ++count;
            }

            return;
        }

        SelectedRow row;

        for (const SelectItem& item : statement.columns) {

            if (item.expression.kind == ExpressionKind::STAR) {

                if (tuple == nullptr) {
                    throw MiniDBException(
                        "SELECT * requires a table"
                    );
                }

                for (const FieldValue& value : tuple->GetValues()) {
                    row.values.push_back(value);
                }
            }
            else {
                row.values.push_back(
                    Evaluate(item.expression, tuple)
                );
            }
        }

        for (const OrderByItem& item : statement.order_by) {
            row.order_values.push_back(
                Evaluate(item.expression, tuple)
            );
        }

        selected.push_back(std::move(row));
    };

    if (heap) {

        for (const RID& rid : rids) {

            const auto bytes = heap->GetRecord(
                transaction,
                txn_manager_,
                rid
            );

            const Tuple tuple =
                Tuple::Deserialize(*schema, bytes);

            process_tuple(&tuple);
        }
    }
    else {
        process_tuple(nullptr);
    }

    if (count_query) {

        result.rows.push_back({
            FieldValue{count}
        });

        result.rows_affected = 1;

        result.message = "COUNT returned one row";

        return result;
    }

    // DISTINCT.
    if (statement.distinct) {

        std::vector<SelectedRow> unique_rows;

        for (SelectedRow& candidate : selected) {

            const bool already_present =
                std::any_of(
                    unique_rows.begin(),
                    unique_rows.end(),
                    [&](const SelectedRow& existing) {
                        return existing.values == candidate.values;
                    }
                );

            if (!already_present) {
                unique_rows.push_back(std::move(candidate));
            }
        }

        selected = std::move(unique_rows);
    }

    // ORDER BY.
    if (!statement.order_by.empty()) {

        std::stable_sort(
            selected.begin(),
            selected.end(),
            [&](const SelectedRow& a, const SelectedRow& b) {

                for (std::size_t i = 0;
                     i < statement.order_by.size();
                     ++i) {

                    const int comparison =
                        CompareValues(
                            a.order_values[i],
                            b.order_values[i]
                        );

                    if (comparison == 0) {
                        continue;
                    }

                    return statement.order_by[i].ascending
                        ? comparison < 0
                        : comparison > 0;
                }

                return false;
            }
        );
    }

    // OFFSET and LIMIT.
    const std::size_t start = std::min(
        statement.offset.value_or(0),
        selected.size()
    );

    const std::size_t available = selected.size() - start;

    const std::size_t amount = std::min(
        statement.limit.value_or(available),
        available
    );

    for (std::size_t i = 0; i < amount; ++i) {
        result.rows.push_back(
            std::move(selected[start + i].values)
        );
    }

    result.rows_affected = result.rows.size();

    result.message =
        "Rows returned: " +
        std::to_string(result.rows.size());

    return result;
}


// ==================================================
// UPDATE
// ==================================================

ExecutionResult Executor::ExecuteUpdate(
    const UpdateStatement& statement,
    Transaction& transaction
) {

    auto operation_lock =
        transaction.AcquireOperationLock();

    std::lock_guard<std::mutex> constraint_lock(
        constraint_mutex_
    );

    const auto metadata = GetMetadata(statement.table_name);
    const Schema& schema = metadata->GetSchema();

    const auto heap = GetHeap(statement.table_name);
    const auto rids = heap->GetAllRIDs();

    std::size_t updated_count = 0;

    for (const RID& rid : rids) {

        const auto bytes = heap->GetRecord(
            transaction,
            txn_manager_,
            rid
        );

        const Tuple original =
            Tuple::Deserialize(schema, bytes);

        if (statement.where &&
            !MatchesCondition(*statement.where, &original)) {
            continue;
        }

        Tuple updated = original;

        std::unordered_set<std::size_t> assigned;

        for (const Assignment& assignment :
             statement.assignments) {

            const std::size_t index =
                schema.GetColumnIndex(assignment.column_name);

            if (!assigned.insert(index).second) {
                throw MiniDBException(
                    "Column assigned more than once in UPDATE"
                );
            }

            const FieldValue value =
                Evaluate(assignment.value, &original);

            updated.SetValue(index, value);
        }

        ValidateUniqueConstraints(
            statement.table_name,
            updated,
            transaction,
            rid
        );

        if (heap->UpdateRecord(
                transaction,
                txn_manager_,
                rid,
                updated.Serialize()
            )) {
            ++updated_count;
        }
    }

    return {
        {}, {}, updated_count,
        "Rows updated: " + std::to_string(updated_count)
    };
}


// ==================================================
// DELETE
// ==================================================

ExecutionResult Executor::ExecuteDelete(
    const DeleteStatement& statement,
    Transaction& transaction
) {

    auto operation_lock =
        transaction.AcquireOperationLock();

    const auto metadata = GetMetadata(statement.table_name);
    const Schema& schema = metadata->GetSchema();

    const auto heap = GetHeap(statement.table_name);
    const auto rids = heap->GetAllRIDs();

    std::size_t deleted_count = 0;

    for (const RID& rid : rids) {

        const auto bytes = heap->GetRecord(
            transaction,
            txn_manager_,
            rid
        );

        const Tuple tuple =
            Tuple::Deserialize(schema, bytes);

        if (statement.where &&
            !MatchesCondition(*statement.where, &tuple)) {
            continue;
        }

        if (heap->DeleteRecord(
                transaction,
                txn_manager_,
                rid
            )) {
            ++deleted_count;
        }
    }

    return {
        {}, {}, deleted_count,
        "Rows deleted: " + std::to_string(deleted_count)
    };
}


// ==================================================
// EXPRESSION API
// ==================================================

FieldValue Executor::EvaluateExpression(
    const Expression& expression,
    const Tuple* tuple
) const {
    return Evaluate(expression, tuple);
}


bool Executor::EvaluatePredicate(
    const Expression& expression,
    const Tuple* tuple
) const {
    return MatchesCondition(expression, tuple);
}

void Executor::AbortTransaction(Transaction& transaction) {

    txn_manager_.AbortTransaction(
        transaction,
        [&](const LogRecord& record) {
            UndoRecord(record);
        },
        [&]() {
            FlushPages();
        }
    );
}

} // namespace minidb
