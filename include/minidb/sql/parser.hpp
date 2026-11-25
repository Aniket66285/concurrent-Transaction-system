
#pragma once

#include "minidb/sql/ast.hpp"
#include "minidb/sql/token.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace minidb {

class Parser {
public:

    explicit Parser(std::vector<Token> tokens);

    // Parses one or more SQL statements.
    std::vector<Statement> Parse();

private:

    // --------------------------------------------------
    // Token helpers
    // --------------------------------------------------

    bool IsAtEnd() const noexcept;

    const Token& Peek() const;

    const Token& Previous() const;

    const Token& Advance();

    bool Check(TokenType type) const noexcept;

    bool Match(TokenType type);

    Token Consume(
        TokenType type,
        const std::string& message
    );

    std::string ConsumeIdentifier(
        const std::string& message
    );

    [[noreturn]] void ThrowError(
        const std::string& message
    ) const;


    // --------------------------------------------------
    // Statements
    // --------------------------------------------------

    Statement ParseStatement();

    CreateTableStatement ParseCreateTable();

    DropTableStatement ParseDropTable();

    InsertStatement ParseInsert();

    SelectStatement ParseSelect();

    UpdateStatement ParseUpdate();

    DeleteStatement ParseDelete();

    BeginStatement ParseBegin();

    CommitStatement ParseCommit();

    RollbackStatement ParseRollback();


    // --------------------------------------------------
    // Column definitions
    // --------------------------------------------------

    ColumnDefinition ParseColumnDefinition();

    ColumnType ParseColumnType(std::size_t& max_length);


    // --------------------------------------------------
    // Expressions
    // --------------------------------------------------

    Expression ParseExpression();

    Expression ParseOr();

    Expression ParseAnd();

    Expression ParseComparison();

    Expression ParseAdditive();

    Expression ParseMultiplicative();

    Expression ParseUnary();

    Expression ParsePrimary();

    Expression MakeBinary(
        std::string operation,
        Expression left,
        Expression right
    );


    // --------------------------------------------------
    // Helpers
    // --------------------------------------------------

    std::size_t ParseUnsignedInteger(
        const std::string& description
    );

    std::vector<Token> tokens_;

    std::size_t current_{0};
};

} // namespace minidb
