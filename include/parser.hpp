#pragma once

#include "lexer.hpp"
#include "ast.hpp"
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

class ParserException : public std::runtime_error {
public:
    ParserException(const std::string &message, size_t line, size_t col)
        : std::runtime_error("Parser Error at line " + std::to_string(line) + ", col " + std::to_string(col) + ": " + message),
          line_(line), col_(col) {}

    size_t GetLine() const { return line_; }
    size_t GetCol() const { return col_; }

private:
    size_t line_;
    size_t col_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<SQLStatement> Parse();

private:
    std::unique_ptr<SQLStatement> ParseCreateTable();
    std::unique_ptr<SQLStatement> ParseInsert();
    std::unique_ptr<SQLStatement> ParseSelect();
    std::unique_ptr<SQLStatement> ParseUpdate();
    std::unique_ptr<SQLStatement> ParseDelete();

    std::unique_ptr<ASTExpression> ParseExpression();
    std::unique_ptr<ASTExpression> ParseAndExpression();
    std::unique_ptr<ASTExpression> ParseComparisonExpression();
    std::unique_ptr<ASTExpression> ParsePrimaryExpression();

    bool Check(TokenType type) const;
    bool Match(TokenType type);
    Token Consume(TokenType type, const std::string &error_message);
    Token Advance();
    Token Peek() const;
    Token Previous() const;
    bool IsAtEnd() const;

    std::vector<Token> tokens_;
    size_t current_{0};
};
