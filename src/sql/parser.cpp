
#include "minidb/sql/parser.hpp"

#include "minidb/common/exception.hpp"

#include <initializer_list>
#include <limits>
#include <utility>

namespace minidb {


// ==================================================
// CONSTRUCTOR
// ==================================================

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)) {

    if (tokens_.empty() ||
        tokens_.back().type != TokenType::END_OF_FILE) {

        tokens_.push_back({
            TokenType::END_OF_FILE,
            "",
            1,
            1
        });
    }
}


// ==================================================
// TOKEN HELPERS
// ==================================================

bool Parser::IsAtEnd() const noexcept {
    return current_ >= tokens_.size() ||
           tokens_[current_].type == TokenType::END_OF_FILE;
}


const Token& Parser::Peek() const {

    if (current_ >= tokens_.size()) {
        return tokens_.back();
    }

    return tokens_[current_];
}


const Token& Parser::Previous() const {
    return tokens_[current_ - 1];
}


const Token& Parser::Advance() {

    if (!IsAtEnd()) {
        ++current_;
    }

    return Previous();
}


bool Parser::Check(TokenType type) const noexcept {
    return !IsAtEnd() && Peek().type == type;
}


bool Parser::Match(TokenType type) {

    if (!Check(type)) {
        return false;
    }

    Advance();

    return true;
}


Token Parser::Consume(
    TokenType type,
    const std::string& message
) {

    if (Check(type)) {
        return Advance();
    }

    ThrowError(message);
}


std::string Parser::ConsumeIdentifier(
    const std::string& message
) {

    return Consume(
        TokenType::IDENTIFIER,
        message
    ).lexeme;
}


[[noreturn]] void Parser::ThrowError(
    const std::string& message
) const {

    const Token& token = Peek();

    throw MiniDBException(
        "SQL Parser error at line " +
        std::to_string(token.line) +
        ", column " +
        std::to_string(token.column) +
        ": " + message +
        " (found '" + token.lexeme + "')"
    );
}


// ==================================================
// COMPLETE PARSE
// ==================================================

std::vector<Statement> Parser::Parse() {

    std::vector<Statement> statements;

    while (!IsAtEnd()) {

        // Permit empty statements.
        if (Match(TokenType::SEMICOLON)) {
            continue;
        }

        statements.push_back(ParseStatement());

        // Statements must end with semicolon or EOF.
        if (Match(TokenType::SEMICOLON)) {
            continue;
        }

        if (!IsAtEnd()) {
            ThrowError("Expected ';' between SQL statements");
        }
    }

    return statements;
}


// ==================================================
// STATEMENT DISPATCH
// ==================================================

Statement Parser::ParseStatement() {

    if (Match(TokenType::KW_CREATE)) {
        return {ParseCreateTable()};
    }

    if (Match(TokenType::KW_DROP)) {
        return {ParseDropTable()};
    }

    if (Match(TokenType::KW_INSERT)) {
        return {ParseInsert()};
    }

    if (Match(TokenType::KW_SELECT)) {
        return {ParseSelect()};
    }

    if (Match(TokenType::KW_UPDATE)) {
        return {ParseUpdate()};
    }

    if (Match(TokenType::KW_DELETE)) {
        return {ParseDelete()};
    }

    if (Match(TokenType::KW_BEGIN)) {
        return {ParseBegin()};
    }

    if (Match(TokenType::KW_COMMIT)) {
        return {ParseCommit()};
    }

    if (Match(TokenType::KW_ROLLBACK)) {
        return {ParseRollback()};
    }

    ThrowError("Unsupported SQL statement");
}


// ==================================================
// CREATE TABLE
// ==================================================

CreateTableStatement Parser::ParseCreateTable() {

    Consume(
        TokenType::KW_TABLE,
        "Expected TABLE after CREATE"
    );

    CreateTableStatement statement;

    statement.table_name =
        ConsumeIdentifier("Expected table name");

    Consume(
        TokenType::LEFT_PAREN,
        "Expected '(' after table name"
    );

    if (Check(TokenType::RIGHT_PAREN)) {
        ThrowError("CREATE TABLE requires at least one column");
    }

    do {

        statement.columns.push_back(
            ParseColumnDefinition()
        );

    } while (Match(TokenType::COMMA));

    Consume(
        TokenType::RIGHT_PAREN,
        "Expected ')' after column definitions"
    );

    return statement;
}


// ==================================================
// DROP TABLE
// ==================================================

DropTableStatement Parser::ParseDropTable() {

    Consume(
        TokenType::KW_TABLE,
        "Expected TABLE after DROP"
    );

    DropTableStatement statement;

    statement.table_name =
        ConsumeIdentifier("Expected table name");

    return statement;
}


// ==================================================
// COLUMN DEFINITION
// ==================================================

ColumnDefinition Parser::ParseColumnDefinition() {

    ColumnDefinition column;

    column.name =
        ConsumeIdentifier("Expected column name");

    column.type = ParseColumnType(column.max_length);

    while (!IsAtEnd() &&
           !Check(TokenType::COMMA) &&
           !Check(TokenType::RIGHT_PAREN)) {

        if (Match(TokenType::KW_NOT)) {

            Consume(
                TokenType::KW_NULL,
                "Expected NULL after NOT"
            );

            column.nullable = false;

            continue;
        }

        if (Match(TokenType::KW_NULL)) {

            if (column.primary_key) {
                ThrowError("PRIMARY KEY cannot be nullable");
            }

            column.nullable = true;

            continue;
        }

        if (Match(TokenType::KW_PRIMARY)) {

            Consume(
                TokenType::KW_KEY,
                "Expected KEY after PRIMARY"
            );

            column.primary_key = true;
            column.nullable = false;

            continue;
        }

        if (Match(TokenType::KW_UNIQUE)) {

            column.unique = true;

            continue;
        }

        if (Match(TokenType::KW_DEFAULT)) {

            column.default_value = ParseUnary();

            continue;
        }

        ThrowError("Unexpected column constraint");
    }

    return column;
}


// ==================================================
// DATA TYPES
// ==================================================

ColumnType Parser::ParseColumnType(
    std::size_t& max_length
) {

    max_length = 0;

    if (Match(TokenType::KW_INT) ||
        Match(TokenType::KW_INTEGER) ||
        Match(TokenType::KW_BIGINT)) {

        return ColumnType::INT64;
    }

    if (Match(TokenType::KW_DOUBLE) ||
        Match(TokenType::KW_FLOAT)) {

        return ColumnType::DOUBLE;
    }

    if (Match(TokenType::KW_BOOLEAN)) {
        return ColumnType::BOOLEAN;
    }

    if (Match(TokenType::KW_VARCHAR)) {

        Consume(
            TokenType::LEFT_PAREN,
            "Expected '(' after VARCHAR"
        );

        max_length = ParseUnsignedInteger(
            "VARCHAR maximum length"
        );

        if (max_length == 0) {
            ThrowError("VARCHAR length must be greater than zero");
        }

        Consume(
            TokenType::RIGHT_PAREN,
            "Expected ')' after VARCHAR length"
        );

        return ColumnType::VARCHAR;
    }

    ThrowError("Unsupported column data type");
}


// ==================================================
// INSERT
// ==================================================

InsertStatement Parser::ParseInsert() {

    Consume(
        TokenType::KW_INTO,
        "Expected INTO after INSERT"
    );

    InsertStatement statement;

    statement.table_name =
        ConsumeIdentifier("Expected table name");

    // Optional column list.
    if (Match(TokenType::LEFT_PAREN)) {

        do {

            statement.columns.push_back(
                ConsumeIdentifier("Expected column name")
            );

        } while (Match(TokenType::COMMA));

        Consume(
            TokenType::RIGHT_PAREN,
            "Expected ')' after INSERT column list"
        );
    }

    Consume(
        TokenType::KW_VALUES,
        "Expected VALUES in INSERT statement"
    );

    // Multiple VALUES rows are supported.
    do {

        Consume(
            TokenType::LEFT_PAREN,
            "Expected '(' before INSERT values"
        );

        std::vector<Expression> row;

        row.push_back(ParseExpression());

        while (Match(TokenType::COMMA)) {
            row.push_back(ParseExpression());
        }

        Consume(
            TokenType::RIGHT_PAREN,
            "Expected ')' after INSERT values"
        );

        if (!statement.columns.empty() &&
            row.size() != statement.columns.size()) {

            ThrowError(
                "INSERT value count does not match column count"
            );
        }

        if (!statement.rows.empty() &&
            row.size() != statement.rows.front().size()) {

            ThrowError(
                "All INSERT rows must have the same number of values"
            );
        }

        statement.rows.push_back(std::move(row));

    } while (Match(TokenType::COMMA));

    return statement;
}


// ==================================================
// SELECT
// ==================================================

SelectStatement Parser::ParseSelect() {

    SelectStatement statement;

    statement.distinct =
        Match(TokenType::KW_DISTINCT);

    // Projection list.
    do {

        SelectItem item;

        item.expression = ParseExpression();

        if (Match(TokenType::KW_AS)) {

            item.alias =
                ConsumeIdentifier("Expected alias after AS");
        }

        statement.columns.push_back(
            std::move(item)
        );

    } while (Match(TokenType::COMMA));

    // FROM is optional to allow SELECT 1 and expressions.
    if (Match(TokenType::KW_FROM)) {

        statement.from_table =
            ConsumeIdentifier("Expected table name after FROM");
    }

    if (Match(TokenType::KW_WHERE)) {
        statement.where = ParseExpression();
    }

    // ORDER BY.
    if (Match(TokenType::KW_ORDER)) {

        Consume(
            TokenType::KW_BY,
            "Expected BY after ORDER"
        );

        do {

            OrderByItem item;

            item.expression = ParseExpression();

            if (Match(TokenType::KW_ASC)) {
                item.ascending = true;
            }
            else if (Match(TokenType::KW_DESC)) {
                item.ascending = false;
            }

            statement.order_by.push_back(
                std::move(item)
            );

        } while (Match(TokenType::COMMA));
    }

    // LIMIT.
    if (Match(TokenType::KW_LIMIT)) {

        statement.limit = ParseUnsignedInteger("LIMIT value");

        // OFFSET can follow LIMIT.
        if (Match(TokenType::KW_OFFSET)) {

            statement.offset =
                ParseUnsignedInteger("OFFSET value");
        }
    }
    else if (Match(TokenType::KW_OFFSET)) {

        ThrowError("OFFSET requires LIMIT in this SQL subset");
    }

    return statement;
}


// ==================================================
// UPDATE
// ==================================================

UpdateStatement Parser::ParseUpdate() {

    UpdateStatement statement;

    statement.table_name =
        ConsumeIdentifier("Expected table name after UPDATE");

    Consume(
        TokenType::KW_SET,
        "Expected SET after table name"
    );

    do {

        Assignment assignment;

        assignment.column_name =
            ConsumeIdentifier("Expected column name in SET");

        Consume(
            TokenType::EQUAL,
            "Expected '=' in UPDATE assignment"
        );

        assignment.value = ParseExpression();

        statement.assignments.push_back(
            std::move(assignment)
        );

    } while (Match(TokenType::COMMA));

    if (Match(TokenType::KW_WHERE)) {
        statement.where = ParseExpression();
    }

    return statement;
}


// ==================================================
// DELETE
// ==================================================

DeleteStatement Parser::ParseDelete() {

    Consume(
        TokenType::KW_FROM,
        "Expected FROM after DELETE"
    );

    DeleteStatement statement;

    statement.table_name =
        ConsumeIdentifier("Expected table name after FROM");

    if (Match(TokenType::KW_WHERE)) {
        statement.where = ParseExpression();
    }

    return statement;
}


// ==================================================
// TRANSACTION STATEMENTS
// ==================================================

BeginStatement Parser::ParseBegin() {

    Match(TokenType::KW_TRANSACTION);

    return {};
}


CommitStatement Parser::ParseCommit() {
    return {};
}


RollbackStatement Parser::ParseRollback() {
    return {};
}


// ==================================================
// NUMERIC HELPERS
// ==================================================

std::size_t Parser::ParseUnsignedInteger(
    const std::string& description
) {

    const Token token = Consume(
        TokenType::INTEGER_LITERAL,
        "Expected integer for " + description
    );

    try {

        const unsigned long long value =
            std::stoull(token.lexeme);

        if (value >
            static_cast<unsigned long long>(
                std::numeric_limits<std::size_t>::max()
            )) {

            ThrowError(description + " is too large");
        }

        return static_cast<std::size_t>(value);
    }
    catch (const MiniDBException&) {
        throw;
    }
    catch (...) {

        ThrowError("Invalid " + description);
    }
}


// ==================================================
// EXPRESSION ENTRY
// ==================================================

Expression Parser::ParseExpression() {
    return ParseOr();
}


// ==================================================
// OR
// ==================================================

Expression Parser::ParseOr() {

    Expression expression = ParseAnd();

    while (Match(TokenType::KW_OR)) {

        expression = MakeBinary(
            "OR",
            std::move(expression),
            ParseAnd()
        );
    }

    return expression;
}


// ==================================================
// AND
// ==================================================

Expression Parser::ParseAnd() {

    Expression expression = ParseComparison();

    while (Match(TokenType::KW_AND)) {

        expression = MakeBinary(
            "AND",
            std::move(expression),
            ParseComparison()
        );
    }

    return expression;
}


// ==================================================
// COMPARISON
// ==================================================

Expression Parser::ParseComparison() {

    Expression expression = ParseAdditive();

    while (true) {

        std::string operation;

        if (Match(TokenType::EQUAL)) {
            operation = "=";
        }
        else if (Match(TokenType::NOT_EQUAL)) {
            operation = "!=";
        }
        else if (Match(TokenType::LESS)) {
            operation = "<";
        }
        else if (Match(TokenType::LESS_EQUAL)) {
            operation = "<=";
        }
        else if (Match(TokenType::GREATER)) {
            operation = ">";
        }
        else if (Match(TokenType::GREATER_EQUAL)) {
            operation = ">=";
        }
        else if (Match(TokenType::KW_LIKE)) {
            operation = "LIKE";
        }
        else if (Match(TokenType::KW_NOT)) {

            Consume(
                TokenType::KW_LIKE,
                "Expected LIKE after NOT in comparison"
            );

            operation = "NOT LIKE";
        }
        else if (Match(TokenType::KW_IS)) {

            const bool is_not =
                Match(TokenType::KW_NOT);

            Consume(
                TokenType::KW_NULL,
                "Expected NULL after IS"
            );

            Expression null_expression;

            null_expression.kind = ExpressionKind::IS_NULL;

            null_expression.value = is_not
                ? "IS NOT NULL"
                : "IS NULL";

            null_expression.negated = is_not;

            null_expression.children.push_back(
                std::move(expression)
            );

            expression = std::move(null_expression);

            continue;
        }
        else {
            break;
        }

        expression = MakeBinary(
            std::move(operation),
            std::move(expression),
            ParseAdditive()
        );
    }

    return expression;
}


// ==================================================
// ADDITIVE
// ==================================================

Expression Parser::ParseAdditive() {

    Expression expression = ParseMultiplicative();

    while (Check(TokenType::PLUS) ||
           Check(TokenType::MINUS)) {

        std::string operation = Advance().lexeme;

        expression = MakeBinary(
            std::move(operation),
            std::move(expression),
            ParseMultiplicative()
        );
    }

    return expression;
}


// ==================================================
// MULTIPLICATIVE
// ==================================================

Expression Parser::ParseMultiplicative() {

    Expression expression = ParseUnary();

    while (Check(TokenType::STAR) ||
           Check(TokenType::SLASH) ||
           Check(TokenType::PERCENT)) {

        std::string operation = Advance().lexeme;

        expression = MakeBinary(
            std::move(operation),
            std::move(expression),
            ParseUnary()
        );
    }

    return expression;
}


// ==================================================
// UNARY
// ==================================================

Expression Parser::ParseUnary() {

    if (Match(TokenType::KW_NOT) ||
        Match(TokenType::MINUS) ||
        Match(TokenType::PLUS)) {

        const std::string operation =
            Previous().lexeme;

        Expression expression;

        expression.kind = ExpressionKind::UNARY;

        expression.value = operation;

        expression.children.push_back(
            ParseUnary()
        );

        return expression;
    }

    return ParsePrimary();
}


// ==================================================
// PRIMARY
// ==================================================

Expression Parser::ParsePrimary() {

    if (Match(TokenType::INTEGER_LITERAL) ||
        Match(TokenType::FLOAT_LITERAL) ||
        Match(TokenType::STRING_LITERAL) ||
        Match(TokenType::KW_TRUE) ||
        Match(TokenType::KW_FALSE) ||
        Match(TokenType::KW_NULL)) {

        Expression expression;

        expression.kind = ExpressionKind::LITERAL;

        expression.value = Previous().lexeme;

        expression.literal_type = Previous().type;

        return expression;
    }

    // COUNT(...) function.
    if (Match(TokenType::KW_COUNT)) {

        Expression function;

        function.kind = ExpressionKind::FUNCTION;

        function.value = "COUNT";

        Consume(
            TokenType::LEFT_PAREN,
            "Expected '(' after COUNT"
        );

        if (Match(TokenType::STAR)) {

            Expression star;

            star.kind = ExpressionKind::STAR;
            star.value = "*";

            function.children.push_back(
                std::move(star)
            );
        }
        else {

            function.children.push_back(
                ParseExpression()
            );
        }

        Consume(
            TokenType::RIGHT_PAREN,
            "Expected ')' after COUNT argument"
        );

        return function;
    }

    if (Match(TokenType::STAR)) {

        Expression expression;

        expression.kind = ExpressionKind::STAR;

        expression.value = "*";

        return expression;
    }

    if (Match(TokenType::IDENTIFIER)) {

        std::string column_name = Previous().lexeme;

        // Support table.column and table.*.
        if (Match(TokenType::DOT)) {

            if (Match(TokenType::STAR)) {
                column_name += ".*";

                Expression expression;

                expression.kind = ExpressionKind::STAR;

                expression.value = std::move(column_name);

                return expression;
            }

            column_name += ".";

            column_name += ConsumeIdentifier(
                "Expected column name after '.'"
            );
        }

        Expression expression;

        expression.kind = ExpressionKind::COLUMN;

        expression.value = std::move(column_name);

        return expression;
    }

    if (Match(TokenType::LEFT_PAREN)) {

        Expression expression = ParseExpression();

        Consume(
            TokenType::RIGHT_PAREN,
            "Expected ')' after expression"
        );

        return expression;
    }

    ThrowError("Expected an expression");
}


// ==================================================
// BINARY EXPRESSION FACTORY
// ==================================================

Expression Parser::MakeBinary(
    std::string operation,
    Expression left,
    Expression right
) {

    Expression expression;

    expression.kind = ExpressionKind::BINARY;

    expression.value = std::move(operation);

    expression.children.push_back(
        std::move(left)
    );

    expression.children.push_back(
        std::move(right)
    );

    return expression;
}

} // namespace minidb
