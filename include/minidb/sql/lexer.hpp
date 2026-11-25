
#pragma once

#include "minidb/sql/token.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace minidb {

class Lexer {
public:

    explicit Lexer(std::string source);

    // Converts complete SQL source into tokens.
    // Throws MiniDBException on invalid input.
    std::vector<Token> Tokenize();

private:

    bool IsAtEnd() const noexcept;

    char Peek() const noexcept;

    char PeekNext() const noexcept;

    char Advance();

    bool Match(char expected);

    void SkipWhitespaceAndComments();

    void ScanToken();

    void ReadIdentifier();

    void ReadNumber();

    void ReadString();

    void ReadQuotedIdentifier(char quote);

    void AddToken(TokenType type);

    void AddToken(
        TokenType type,
        std::string lexeme
    );

    [[noreturn]] void ThrowError(
        const std::string& message
    ) const;

    static bool IsDigit(char c) noexcept;

    static bool IsAlpha(char c) noexcept;

    static std::string ToUpper(std::string value);

    std::string source_;

    std::size_t start_{0};
    std::size_t current_{0};

    std::size_t line_{1};
    std::size_t column_{1};

    std::size_t token_line_{1};
    std::size_t token_column_{1};

    std::vector<Token> tokens_;
};

} // namespace minidb
