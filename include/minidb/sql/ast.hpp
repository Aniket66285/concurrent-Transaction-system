
#pragma once

#include "minidb/catalog/schema.hpp"
#include "minidb/sql/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

// ==================================================
// EXPRESSIONS
// ==================================================

enum class ExpressionKind {
    LITERAL,
    COLUMN,
    STAR,
    UNARY,
    BINARY,
    IS_NULL,
    FUNCTION
};

struct Expression {

    ExpressionKind kind{ExpressionKind::LITERAL};

    // Operator, column name, function name or literal text.
    std::string value;

    TokenType literal_type{TokenType::END_OF_FILE};

    // Used for IS NOT NULL.
    bool negated{false};

    // Unary expressions have one child.
    // Binary expressions have two children.
    // Functions contain their argument expressions.
    std::vector<Expression> children;
};


// ==================================================
// CREATE TABLE
// ==================================================

struct ColumnDefinition {

    std::string name;

    ColumnType type{ColumnType::INT64};

    std::size_t max_length{0};

    bool nullable{true};

    bool primary_key{false};

    bool unique{false};

    std::optional<Expression> default_value;
};

struct CreateTableStatement {

    std::string table_name;

    std::vector<ColumnDefinition> columns;
};


// ==================================================
// DROP TABLE
// ==================================================

struct DropTableStatement {
    std::string table_name;
};


// ==================================================
// INSERT
// ==================================================

struct InsertStatement {

    std::string table_name;

    std::vector<std::string> columns;

    std::vector<std::vector<Expression>> rows;
};


// ==================================================
// SELECT
// ==================================================

struct SelectItem {

    Expression expression;

    std::string alias;
};

struct OrderByItem {

    Expression expression;

    bool ascending{true};
};

struct SelectStatement {

    bool distinct{false};

    std::vector<SelectItem> columns;

    std::optional<std::string> from_table;

    std::optional<Expression> where;

    std::vector<OrderByItem> order_by;

    std::optional<std::size_t> limit;

    std::optional<std::size_t> offset;
};


// ==================================================
// UPDATE
// ==================================================

struct Assignment {

    std::string column_name;

    Expression value;
};

struct UpdateStatement {

    std::string table_name;

    std::vector<Assignment> assignments;

    std::optional<Expression> where;
};


// ==================================================
// DELETE
// ==================================================

struct DeleteStatement {

    std::string table_name;

    std::optional<Expression> where;
};


// ==================================================
// TRANSACTION STATEMENTS
// ==================================================

struct BeginStatement {};

struct CommitStatement {};

struct RollbackStatement {};


// ==================================================
// COMPLETE STATEMENT
// ==================================================

using StatementData = std::variant<
    CreateTableStatement,
    DropTableStatement,
    InsertStatement,
    SelectStatement,
    UpdateStatement,
    DeleteStatement,
    BeginStatement,
    CommitStatement,
    RollbackStatement
>;

struct Statement {

    StatementData data;
};

} // namespace minidb
