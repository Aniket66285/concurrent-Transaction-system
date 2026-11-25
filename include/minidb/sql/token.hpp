
#pragma once

#include <cstddef>
#include <string>

namespace minidb {

enum class TokenType {
    END_OF_FILE,

    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,

    COMMA,
    DOT,
    SEMICOLON,
    LEFT_PAREN,
    RIGHT_PAREN,

    STAR,
    PLUS,
    MINUS,
    SLASH,
    PERCENT,

    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,

    KW_CREATE,
    KW_TABLE,
    KW_DROP,
    KW_ALTER,
    KW_ADD,
    KW_COLUMN,

    KW_INSERT,
    KW_INTO,
    KW_VALUES,

    KW_SELECT,
    KW_DISTINCT,
    KW_FROM,
    KW_WHERE,

    KW_AND,
    KW_OR,
    KW_NOT,
    KW_IS,
    KW_NULL,
    KW_LIKE,

    KW_ORDER,
    KW_BY,
    KW_ASC,
    KW_DESC,
    KW_LIMIT,
    KW_OFFSET,

    KW_UPDATE,
    KW_SET,
    KW_DELETE,

    KW_BEGIN,
    KW_TRANSACTION,
    KW_COMMIT,
    KW_ROLLBACK,

    KW_PRIMARY,
    KW_KEY,
    KW_UNIQUE,
    KW_DEFAULT,

    KW_TRUE,
    KW_FALSE,

    KW_INT,
    KW_INTEGER,
    KW_BIGINT,
    KW_DOUBLE,
    KW_FLOAT,
    KW_VARCHAR,
    KW_BOOLEAN,

    KW_COUNT,
    KW_AS,

    KW_JOIN,
    KW_INNER,
    KW_LEFT,
    KW_RIGHT,
    KW_ON,

    KW_GROUP,
    KW_HAVING
};

struct Token {
    TokenType type;
    std::string lexeme;

    std::size_t line{1};
    std::size_t column{1};
};

const char* TokenTypeToString(TokenType type) noexcept;

} // namespace minidb
