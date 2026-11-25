#include "minidb/sql/lexer.hpp"
#include "minidb/common/exception.hpp"


#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace minidb;

int main() {

// ==============================================
// TEST 1: Basic SQL tokenization
// ==============================================

Lexer lexer(
"SELECT id, name FROM students "
"WHERE id >= 10 AND name != 'Aman''s DB';"
);

const auto tokens = lexer.Tokenize();

assert(tokens.size() == 16);

assert(tokens[0].type == TokenType::KW_SELECT);
assert(tokens[1].type == TokenType::IDENTIFIER);
assert(tokens[2].type == TokenType::COMMA);
assert(tokens[4].type == TokenType::KW_FROM);
assert(tokens[6].type == TokenType::KW_WHERE);
assert(tokens[8].type == TokenType::GREATER_EQUAL);
assert(tokens[9].type == TokenType::INTEGER_LITERAL);
assert(tokens[10].type == TokenType::KW_AND);
assert(tokens[12].type == TokenType::NOT_EQUAL);

assert(tokens[13].type == TokenType::STRING_LITERAL);
assert(tokens[13].lexeme == "Aman's DB");

assert(tokens[14].type == TokenType::SEMICOLON);
assert(tokens[15].type == TokenType::END_OF_FILE);

std::cout << "Basic SQL tokenization: PASS\n";

// ==============================================
// TEST 2: Case-insensitive keywords
// ==============================================

Lexer case_lexer("select ID from Students");

const auto case_tokens = case_lexer.Tokenize();

assert(case_tokens[0].type == TokenType::KW_SELECT);
assert(case_tokens[1].type == TokenType::IDENTIFIER);
assert(case_tokens[2].type == TokenType::KW_FROM);

std::cout << "Case-insensitive keywords: PASS\n";

// ==============================================
// TEST 3: Numeric literals
// ==============================================

Lexer number_lexer("100 25.75 3e+2 5E-1");

const auto numbers = number_lexer.Tokenize();

assert(numbers[0].type == TokenType::INTEGER_LITERAL);
assert(numbers[1].type == TokenType::FLOAT_LITERAL);
assert(numbers[2].type == TokenType::FLOAT_LITERAL);
assert(numbers[3].type == TokenType::FLOAT_LITERAL);

assert(numbers[2].lexeme == "3e+2");

std::cout << "Numeric literals: PASS\n";

// ==============================================
// TEST 4: Comments
// ==============================================

Lexer comment_lexer(
"-- first comment\n"
"SELECT /* middle comment */ id FROM students;"
);

const auto comment_tokens = comment_lexer.Tokenize();

assert(comment_tokens[0].type == TokenType::KW_SELECT);
assert(comment_tokens[1].type == TokenType::IDENTIFIER);
assert(comment_tokens[2].type == TokenType::KW_FROM);

std::cout << "SQL comments: PASS\n";

// ==============================================
// TEST 5: Quoted identifiers
// ==============================================

Lexer quoted_lexer(
    R"(SELECT "first name", `order` FROM students)"
);

const auto quoted_tokens = quoted_lexer.Tokenize();

assert(quoted_tokens[1].type == TokenType::IDENTIFIER);
assert(quoted_tokens[1].lexeme == "first name");

assert(quoted_tokens[3].type == TokenType::IDENTIFIER);
assert(quoted_tokens[3].lexeme == "order");

std::cout << "Quoted identifiers: PASS\n";

// ==============================================
// TEST 6: Comparison operators
// ==============================================

Lexer operator_lexer("= != <> < <= > >=");

const auto operators = operator_lexer.Tokenize();

assert(operators[0].type == TokenType::EQUAL);
assert(operators[1].type == TokenType::NOT_EQUAL);
assert(operators[2].type == TokenType::NOT_EQUAL);
assert(operators[3].type == TokenType::LESS);
assert(operators[4].type == TokenType::LESS_EQUAL);
assert(operators[5].type == TokenType::GREATER);
assert(operators[6].type == TokenType::GREATER_EQUAL);

std::cout << "Comparison operators: PASS\n";

// ==============================================
// TEST 7: Invalid string rejection
// ==============================================

bool string_error = false;

try {
Lexer invalid_string("SELECT 'unfinished");

invalid_string.Tokenize();
}
catch (const MiniDBException&) {
string_error = true;
}

assert(string_error);

std::cout << "Unterminated string rejection: PASS\n";

// ==============================================
// TEST 8: Invalid block comment rejection
// ==============================================

bool comment_error = false;

try {
Lexer invalid_comment("SELECT /* unfinished");

invalid_comment.Tokenize();
}
catch (const MiniDBException&) {
comment_error = true;
}

assert(comment_error);

std::cout << "Unterminated comment rejection: PASS\n";

std::cout << "\nAll SQL Lexer tests passed!\n";

return 0;
}