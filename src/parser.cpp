#include "parser.hpp"
#include <sstream>
#include <utility>

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

std::unique_ptr<SQLStatement> Parser::Parse() {
    if (Match(TokenType::CREATE)) {
        return ParseCreateTable();
    }
    if (Match(TokenType::INSERT)) {
        return ParseInsert();
    }
    if (Match(TokenType::SELECT)) {
        return ParseSelect();
    }
    if (Match(TokenType::UPDATE)) {
        return ParseUpdate();
    }
    if (Match(TokenType::DELETE)) {
        return ParseDelete();
    }
    throw ParserException("Expected SQL statement (CREATE, INSERT, SELECT, UPDATE, DELETE)", Peek().line, Peek().column);
}

std::unique_ptr<SQLStatement> Parser::ParseCreateTable() {
    Consume(TokenType::TABLE, "Expected 'TABLE' after 'CREATE'");
    Token table_token = Consume(TokenType::IDENTIFIER, "Expected table name");
    Consume(TokenType::LPAREN, "Expected '(' before column definitions");
    std::vector<ColumnDefinition> columns;
    do {
        Token col_name = Consume(TokenType::IDENTIFIER, "Expected column name");
        std::string col_type;
        if (Match(TokenType::INT)) {
            col_type = "INT";
        } else if (Match(TokenType::VARCHAR)) {
            col_type = "VARCHAR";
            Consume(TokenType::LPAREN, "Expected '(' after VARCHAR");
            Token num = Consume(TokenType::NUMBER, "Expected length for VARCHAR");
            Consume(TokenType::RPAREN, "Expected ')' after VARCHAR length");
            col_type += "(" + num.text + ")";
        } else {
            throw ParserException("Expected column type (INT, VARCHAR)", Peek().line, Peek().column);
        }
        columns.push_back(ColumnDefinition{col_name.text, col_type});
    } while (Match(TokenType::COMMA));
    Consume(TokenType::RPAREN, "Expected ')' after column definitions");
    return std::make_unique<CreateTableStatement>(table_token.text, std::move(columns));
}

std::unique_ptr<SQLStatement> Parser::ParseInsert() {
    Consume(TokenType::INTO, "Expected 'INTO' after 'INSERT'");
    Token table_token = Consume(TokenType::IDENTIFIER, "Expected table name");
    std::vector<std::string> columns;
    if (Match(TokenType::LPAREN)) {
        do {
            Token col = Consume(TokenType::IDENTIFIER, "Expected column name");
            columns.push_back(col.text);
        } while (Match(TokenType::COMMA));
        Consume(TokenType::RPAREN, "Expected ')' after columns list");
    }
    Consume(TokenType::VALUES, "Expected 'VALUES' after table name/columns");
    Consume(TokenType::LPAREN, "Expected '(' before values list");
    std::vector<std::unique_ptr<LiteralExpression>> values;
    do {
        if (Match(TokenType::NUMBER)) {
            values.push_back(std::make_unique<LiteralExpression>(LiteralType::NUMBER, Previous().text));
        } else if (Match(TokenType::STRING_LITERAL)) {
            values.push_back(std::make_unique<LiteralExpression>(LiteralType::STRING, Previous().text));
        } else {
            throw ParserException("Expected literal value (number or string) in VALUES", Peek().line, Peek().column);
        }
    } while (Match(TokenType::COMMA));
    Consume(TokenType::RPAREN, "Expected ')' after values list");
    return std::make_unique<InsertStatement>(table_token.text, std::move(columns), std::move(values));
}

std::unique_ptr<SQLStatement> Parser::ParseSelect() {
    std::vector<std::string> columns;
    if (!Match(TokenType::STAR)) {
        do {
            Token col = Consume(TokenType::IDENTIFIER, "Expected column name or '*'");
            columns.push_back(col.text);
        } while (Match(TokenType::COMMA));
    }
    Consume(TokenType::FROM, "Expected 'FROM' after projections list");
    Token table_token = Consume(TokenType::IDENTIFIER, "Expected table name");
    std::unique_ptr<ASTExpression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        where_clause = ParseExpression();
    }
    return std::make_unique<SelectStatement>(table_token.text, std::move(columns), std::move(where_clause));
}

std::unique_ptr<SQLStatement> Parser::ParseUpdate() {
    Token table_token = Consume(TokenType::IDENTIFIER, "Expected table name after 'UPDATE'");
    Consume(TokenType::SET, "Expected 'SET' in UPDATE statement");
    std::vector<UpdateAssignment> assignments;
    do {
        Token col = Consume(TokenType::IDENTIFIER, "Expected column name in assignment");
        Consume(TokenType::EQUALS, "Expected '=' after column name");
        std::unique_ptr<ASTExpression> expr = ParseExpression();
        assignments.push_back(UpdateAssignment{col.text, std::move(expr)});
    } while (Match(TokenType::COMMA));
    std::unique_ptr<ASTExpression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        where_clause = ParseExpression();
    }
    return std::make_unique<UpdateStatement>(table_token.text, std::move(assignments), std::move(where_clause));
}

std::unique_ptr<SQLStatement> Parser::ParseDelete() {
    Consume(TokenType::FROM, "Expected 'FROM' after 'DELETE'");
    Token table_token = Consume(TokenType::IDENTIFIER, "Expected table name");
    std::unique_ptr<ASTExpression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        where_clause = ParseExpression();
    }
    return std::make_unique<DeleteStatement>(table_token.text, std::move(where_clause));
}

std::unique_ptr<ASTExpression> Parser::ParseExpression() {
    std::unique_ptr<ASTExpression> expr = ParseAndExpression();
    while (Match(TokenType::OR)) {
        std::string op = Previous().text;
        std::unique_ptr<ASTExpression> right = ParseAndExpression();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTExpression> Parser::ParseAndExpression() {
    std::unique_ptr<ASTExpression> expr = ParseComparisonExpression();
    while (Match(TokenType::AND)) {
        std::string op = Previous().text;
        std::unique_ptr<ASTExpression> right = ParseComparisonExpression();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTExpression> Parser::ParseComparisonExpression() {
    std::unique_ptr<ASTExpression> expr = ParsePrimaryExpression();
    if (Match(TokenType::EQUALS) || Match(TokenType::NOT_EQUALS) || Match(TokenType::LESS) || Match(TokenType::GREATER) ||
        Match(TokenType::LESS_EQUALS) || Match(TokenType::GREATER_EQUALS)) {
        std::string op = Previous().text;
        std::unique_ptr<ASTExpression> right = ParsePrimaryExpression();
        expr = std::make_unique<BinaryExpression>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<ASTExpression> Parser::ParsePrimaryExpression() {
    if (Match(TokenType::LPAREN)) {
        std::unique_ptr<ASTExpression> expr = ParseExpression();
        Consume(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }
    if (Match(TokenType::NUMBER)) {
        return std::make_unique<LiteralExpression>(LiteralType::NUMBER, Previous().text);
    }
    if (Match(TokenType::STRING_LITERAL)) {
        return std::make_unique<LiteralExpression>(LiteralType::STRING, Previous().text);
    }
    if (Match(TokenType::IDENTIFIER)) {
        return std::make_unique<ColumnExpression>(Previous().text);
    }
    throw ParserException("Expected expression", Peek().line, Peek().column);
}

bool Parser::Check(TokenType type) const {
    if (IsAtEnd()) return false;
    return Peek().type == type;
}

bool Parser::Match(TokenType type) {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

Token Parser::Consume(TokenType type, const std::string &error_message) {
    if (Check(type)) return Advance();
    throw ParserException(error_message, Peek().line, Peek().column);
}

Token Parser::Advance() {
    if (!IsAtEnd()) current_++;
    return Previous();
}

Token Parser::Peek() const {
    return tokens_[current_];
}

Token Parser::Previous() const {
    return tokens_[current_ - 1];
}

bool Parser::IsAtEnd() const {
    return Peek().type == TokenType::EOF_TOKEN;
}
