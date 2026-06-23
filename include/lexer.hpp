#pragma once

#include <string>
#include <vector>

enum class TokenType {
    CREATE, TABLE, INSERT, INTO, SELECT, FROM, WHERE, UPDATE, SET, DELETE, VALUES,
    AND, OR,
    INT, VARCHAR,
    IDENTIFIER, NUMBER, STRING_LITERAL,
    STAR, COMMA, LPAREN, RPAREN, EQUALS, NOT_EQUALS, LESS, GREATER, LESS_EQUALS, GREATER_EQUALS,
    EOF_TOKEN, INVALID
};

struct Token {
    TokenType type;
    std::string text;
    size_t line;
    size_t column;
};

class Lexer {
public:
    explicit Lexer(std::string input);
    std::vector<Token> Tokenize();

private:
    char Peek() const;
    char Advance();
    void SkipWhitespace();
    Token NextToken();
    Token ScanIdentifier();
    Token ScanNumber();
    Token ScanString();

    std::string input_;
    size_t pos_{0};
    size_t line_{1};
    size_t col_{1};
};
