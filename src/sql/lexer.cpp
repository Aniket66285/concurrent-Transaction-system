
#include "minidb/sql/lexer.hpp"

#include "minidb/common/exception.hpp"

#include <cctype>
#include <unordered_map>
#include <utility>

namespace minidb {

namespace {

const std::unordered_map<std::string, TokenType>& Keywords() {

    static const std::unordered_map<std::string, TokenType> keywords{

        {"CREATE", TokenType::KW_CREATE},
        {"TABLE", TokenType::KW_TABLE},
        {"DROP", TokenType::KW_DROP},
        {"ALTER", TokenType::KW_ALTER},
        {"ADD", TokenType::KW_ADD},
        {"COLUMN", TokenType::KW_COLUMN},

        {"INSERT", TokenType::KW_INSERT},
        {"INTO", TokenType::KW_INTO},
        {"VALUES", TokenType::KW_VALUES},

        {"SELECT", TokenType::KW_SELECT},
        {"DISTINCT", TokenType::KW_DISTINCT},
        {"FROM", TokenType::KW_FROM},
        {"WHERE", TokenType::KW_WHERE},

        {"AND", TokenType::KW_AND},
        {"OR", TokenType::KW_OR},
        {"NOT", TokenType::KW_NOT},
        {"IS", TokenType::KW_IS},
        {"NULL", TokenType::KW_NULL},
        {"LIKE", TokenType::KW_LIKE},

        {"ORDER", TokenType::KW_ORDER},
        {"BY", TokenType::KW_BY},
        {"ASC", TokenType::KW_ASC},
        {"DESC", TokenType::KW_DESC},
        {"LIMIT", TokenType::KW_LIMIT},
        {"OFFSET", TokenType::KW_OFFSET},

        {"UPDATE", TokenType::KW_UPDATE},
        {"SET", TokenType::KW_SET},
        {"DELETE", TokenType::KW_DELETE},

        {"BEGIN", TokenType::KW_BEGIN},
        {"TRANSACTION", TokenType::KW_TRANSACTION},
        {"COMMIT", TokenType::KW_COMMIT},
        {"ROLLBACK", TokenType::KW_ROLLBACK},

        {"PRIMARY", TokenType::KW_PRIMARY},
        {"KEY", TokenType::KW_KEY},
        {"UNIQUE", TokenType::KW_UNIQUE},
        {"DEFAULT", TokenType::KW_DEFAULT},

        {"TRUE", TokenType::KW_TRUE},
        {"FALSE", TokenType::KW_FALSE},

        {"INT", TokenType::KW_INT},
        {"INTEGER", TokenType::KW_INTEGER},
        {"BIGINT", TokenType::KW_BIGINT},
        {"DOUBLE", TokenType::KW_DOUBLE},
        {"FLOAT", TokenType::KW_FLOAT},
        {"VARCHAR", TokenType::KW_VARCHAR},
        {"BOOLEAN", TokenType::KW_BOOLEAN},

        {"COUNT", TokenType::KW_COUNT},
        {"AS", TokenType::KW_AS},

        {"JOIN", TokenType::KW_JOIN},
        {"INNER", TokenType::KW_INNER},
        {"LEFT", TokenType::KW_LEFT},
        {"RIGHT", TokenType::KW_RIGHT},
        {"ON", TokenType::KW_ON},

        {"GROUP", TokenType::KW_GROUP},
        {"HAVING", TokenType::KW_HAVING}
    };

    return keywords;
}

} // namespace


// --------------------------------------------------
// Constructor
// --------------------------------------------------

Lexer::Lexer(std::string source)
    : source_(std::move(source)) {
}


// --------------------------------------------------
// IsAtEnd
// --------------------------------------------------

bool Lexer::IsAtEnd() const noexcept {
    return current_ >= source_.size();
}


// --------------------------------------------------
// Peek
// --------------------------------------------------

char Lexer::Peek() const noexcept {

    if (IsAtEnd()) {
        return '\0';
    }

    return source_[current_];
}


// --------------------------------------------------
// PeekNext
// --------------------------------------------------

char Lexer::PeekNext() const noexcept {

    if (current_ + 1 >= source_.size()) {
        return '\0';
    }

    return source_[current_ + 1];
}


// --------------------------------------------------
// Advance
// --------------------------------------------------

char Lexer::Advance() {

    const char c = source_[current_++];

    if (c == '\n') {
        ++line_;
        column_ = 1;
    }
    else {
        ++column_;
    }

    return c;
}


// --------------------------------------------------
// Match
// --------------------------------------------------

bool Lexer::Match(char expected) {

    if (IsAtEnd() || Peek() != expected) {
        return false;
    }

    Advance();

    return true;
}


// --------------------------------------------------
// Character helpers
// --------------------------------------------------

bool Lexer::IsDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}


bool Lexer::IsAlpha(char c) noexcept {

    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}


std::string Lexer::ToUpper(std::string value) {

    for (char& c : value) {

        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))
        );
    }

    return value;
}


// --------------------------------------------------
// Error handling
// --------------------------------------------------

[[noreturn]] void Lexer::ThrowError(
    const std::string& message
) const {

    throw MiniDBException(
        "SQL Lexer error at line " +
        std::to_string(token_line_) +
        ", column " +
        std::to_string(token_column_) +
        ": " + message
    );
}


// --------------------------------------------------
// AddToken
// --------------------------------------------------

void Lexer::AddToken(TokenType type) {

    AddToken(
        type,
        source_.substr(start_, current_ - start_)
    );
}


void Lexer::AddToken(
    TokenType type,
    std::string lexeme
) {

    tokens_.push_back({
        type,
        std::move(lexeme),
        token_line_,
        token_column_
    });
}


// --------------------------------------------------
// SkipWhitespaceAndComments
// --------------------------------------------------

void Lexer::SkipWhitespaceAndComments() {

    while (!IsAtEnd()) {

        const char c = Peek();

        if (c == ' ' ||
            c == '\r' ||
            c == '\t' ||
            c == '\n') {

            Advance();

            continue;
        }

        // SQL single-line comment: -- comment
        if (c == '-' && PeekNext() == '-') {

            while (!IsAtEnd() && Peek() != '\n') {
                Advance();
            }

            continue;
        }

        // SQL block comment: /* comment */
        if (c == '/' && PeekNext() == '*') {

            Advance();
            Advance();

            while (!IsAtEnd()) {

                if (Peek() == '*' && PeekNext() == '/') {

                    Advance();
                    Advance();

                    break;
                }

                Advance();
            }

            if (IsAtEnd() &&
                (source_.size() < 2 ||
                 source_[source_.size() - 2] != '*' ||
                 source_[source_.size() - 1] != '/')) {

                ThrowError("Unterminated block comment");
            }

            continue;
        }

        break;
    }
}


// --------------------------------------------------
// ReadIdentifier
// --------------------------------------------------

void Lexer::ReadIdentifier() {

    while (IsAlpha(Peek()) || IsDigit(Peek())) {
        Advance();
    }

    std::string text =
        source_.substr(start_, current_ - start_);

    const std::string normalized = ToUpper(text);

    const auto iterator = Keywords().find(normalized);

    if (iterator != Keywords().end()) {
        AddToken(iterator->second, std::move(text));
    }
    else {
        AddToken(TokenType::IDENTIFIER, std::move(text));
    }
}


// --------------------------------------------------
// ReadQuotedIdentifier
// --------------------------------------------------

void Lexer::ReadQuotedIdentifier(char quote) {

    std::string identifier;

    while (!IsAtEnd()) {

        if (Peek() == quote) {

            Advance();

            if (Peek() == quote) {

                Advance();

                identifier.push_back(quote);

                continue;
            }

            AddToken(
                TokenType::IDENTIFIER,
                std::move(identifier)
            );

            return;
        }

        identifier.push_back(Advance());
    }

    ThrowError("Unterminated quoted identifier");
}


// --------------------------------------------------
// ReadString
// --------------------------------------------------

void Lexer::ReadString() {

    std::string value;

    while (!IsAtEnd()) {

        const char c = Advance();

        if (c == '\'') {

            // SQL escaping: 'It''s working'
            if (Peek() == '\'') {

                Advance();

                value.push_back('\'');

                continue;
            }

            AddToken(
                TokenType::STRING_LITERAL,
                std::move(value)
            );

            return;
        }

        value.push_back(c);
    }

    ThrowError("Unterminated string literal");
}


// --------------------------------------------------
// ReadNumber
// --------------------------------------------------

void Lexer::ReadNumber() {

    while (IsDigit(Peek())) {
        Advance();
    }

    bool is_float = false;

    // Decimal component.
    if (Peek() == '.' && IsDigit(PeekNext())) {

        is_float = true;

        Advance();

        while (IsDigit(Peek())) {
            Advance();
        }
    }

    // Exponent component.
    if (Peek() == 'e' || Peek() == 'E') {

        is_float = true;

        Advance();

        if (Peek() == '+' || Peek() == '-') {
            Advance();
        }

        if (!IsDigit(Peek())) {
            ThrowError("Invalid numeric exponent");
        }

        while (IsDigit(Peek())) {
            Advance();
        }
    }

    AddToken(
        is_float
            ? TokenType::FLOAT_LITERAL
            : TokenType::INTEGER_LITERAL
    );
}


// --------------------------------------------------
// ScanToken
// --------------------------------------------------

void Lexer::ScanToken() {

    const char c = Advance();

    switch (c) {

        case ',':
            AddToken(TokenType::COMMA);
            break;

        case '.':
            AddToken(TokenType::DOT);
            break;

        case ';':
            AddToken(TokenType::SEMICOLON);
            break;

        case '(':
            AddToken(TokenType::LEFT_PAREN);
            break;

        case ')':
            AddToken(TokenType::RIGHT_PAREN);
            break;

        case '*':
            AddToken(TokenType::STAR);
            break;

        case '+':
            AddToken(TokenType::PLUS);
            break;

        case '-':
            AddToken(TokenType::MINUS);
            break;

        case '/':
            AddToken(TokenType::SLASH);
            break;

        case '%':
            AddToken(TokenType::PERCENT);
            break;

        case '=':
            AddToken(TokenType::EQUAL);
            break;

        case '!':
            if (Match('=')) {
                AddToken(TokenType::NOT_EQUAL);
            }
            else {
                ThrowError("Unexpected character '!'");
            }
            break;

        case '<':
            if (Match('=')) {
                AddToken(TokenType::LESS_EQUAL);
            }
            else if (Match('>')) {
                AddToken(TokenType::NOT_EQUAL);
            }
            else {
                AddToken(TokenType::LESS);
            }
            break;

        case '>':
            if (Match('=')) {
                AddToken(TokenType::GREATER_EQUAL);
            }
            else {
                AddToken(TokenType::GREATER);
            }
            break;

        case '\'':
            ReadString();
            break;

        case '"':
        case '`':
            ReadQuotedIdentifier(c);
            break;

        default:

            if (IsDigit(c)) {
                ReadNumber();
            }
            else if (IsAlpha(c)) {
                ReadIdentifier();
            }
            else {
                ThrowError(
                    std::string("Unexpected character '") + c + "'"
                );
            }

            break;
    }
}


// --------------------------------------------------
// Tokenize
// --------------------------------------------------

std::vector<Token> Lexer::Tokenize() {

    tokens_.clear();

    start_ = 0;
    current_ = 0;

    line_ = 1;
    column_ = 1;

    while (!IsAtEnd()) {

        SkipWhitespaceAndComments();

        if (IsAtEnd()) {
            break;
        }

        start_ = current_;

        token_line_ = line_;
        token_column_ = column_;

        ScanToken();
    }

    tokens_.push_back({
        TokenType::END_OF_FILE,
        "",
        line_,
        column_
    });

    return tokens_;
}


// --------------------------------------------------
// TokenTypeToString
// --------------------------------------------------

const char* TokenTypeToString(TokenType type) noexcept {

    switch (type) {

        case TokenType::END_OF_FILE: return "END_OF_FILE";

        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";

        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::LEFT_PAREN: return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN: return "RIGHT_PAREN";

        case TokenType::STAR: return "STAR";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";

        case TokenType::EQUAL: return "EQUAL";
        case TokenType::NOT_EQUAL: return "NOT_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";

        default: return "KEYWORD";
    }
}

} // namespace minidb
