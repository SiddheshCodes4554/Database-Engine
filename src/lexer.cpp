#include "lexer.hpp"
#include <algorithm>
#include <cctype>

Lexer::Lexer(std::string input) : input_(std::move(input)) {}

char Lexer::Peek() const {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_];
}

char Lexer::Advance() {
    if (pos_ >= input_.size()) return '\0';
    char c = input_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

void Lexer::SkipWhitespace() {
    while (pos_ < input_.size()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Advance();
        } else {
            break;
        }
    }
}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    while (true) {
        SkipWhitespace();
        Token t = NextToken();
        tokens.push_back(t);
        if (t.type == TokenType::EOF_TOKEN) {
            break;
        }
    }
    return tokens;
}

Token Lexer::NextToken() {
    if (pos_ >= input_.size()) {
        return Token{TokenType::EOF_TOKEN, "", line_, col_};
    }
    size_t start_line = line_;
    size_t start_col = col_;
    char c = Peek();
    if (std::isalpha(c) || c == '_') {
        return ScanIdentifier();
    }
    if (std::isdigit(c)) {
        return ScanNumber();
    }
    if (c == '\'' || c == '"') {
        return ScanString();
    }
    Advance();
    std::string text(1, c);
    switch (c) {
        case '*': return Token{TokenType::STAR, text, start_line, start_col};
        case ',': return Token{TokenType::COMMA, text, start_line, start_col};
        case '(': return Token{TokenType::LPAREN, text, start_line, start_col};
        case ')': return Token{TokenType::RPAREN, text, start_line, start_col};
        case '=': return Token{TokenType::EQUALS, text, start_line, start_col};
        case '!':
            if (Peek() == '=') {
                Advance();
                return Token{TokenType::NOT_EQUALS, "!=", start_line, start_col};
            }
            return Token{TokenType::INVALID, text, start_line, start_col};
        case '<':
            if (Peek() == '>') {
                Advance();
                return Token{TokenType::NOT_EQUALS, "<>", start_line, start_col};
            }
            if (Peek() == '=') {
                Advance();
                return Token{TokenType::LESS_EQUALS, "<=", start_line, start_col};
            }
            return Token{TokenType::LESS, text, start_line, start_col};
        case '>':
            if (Peek() == '=') {
                Advance();
                return Token{TokenType::GREATER_EQUALS, ">=", start_line, start_col};
            }
            return Token{TokenType::GREATER, text, start_line, start_col};
        default:
            return Token{TokenType::INVALID, text, start_line, start_col};
    }
}

Token Lexer::ScanIdentifier() {
    size_t start_line = line_;
    size_t start_col = col_;
    size_t start_pos = pos_;
    while (pos_ < input_.size()) {
        char c = Peek();
        if (std::isalnum(c) || c == '_') {
            Advance();
        } else {
            break;
        }
    }
    std::string text = input_.substr(start_pos, pos_ - start_pos);
    std::string upper_text = text;
    std::transform(upper_text.begin(), upper_text.end(), upper_text.begin(), ::toupper);
    TokenType type = TokenType::IDENTIFIER;
    if (upper_text == "CREATE") type = TokenType::CREATE;
    else if (upper_text == "TABLE") type = TokenType::TABLE;
    else if (upper_text == "INSERT") type = TokenType::INSERT;
    else if (upper_text == "INTO") type = TokenType::INTO;
    else if (upper_text == "SELECT") type = TokenType::SELECT;
    else if (upper_text == "FROM") type = TokenType::FROM;
    else if (upper_text == "WHERE") type = TokenType::WHERE;
    else if (upper_text == "UPDATE") type = TokenType::UPDATE;
    else if (upper_text == "SET") type = TokenType::SET;
    else if (upper_text == "DELETE") type = TokenType::DELETE;
    else if (upper_text == "VALUES") type = TokenType::VALUES;
    else if (upper_text == "AND") type = TokenType::AND;
    else if (upper_text == "OR") type = TokenType::OR;
    else if (upper_text == "INT") type = TokenType::INT;
    else if (upper_text == "VARCHAR") type = TokenType::VARCHAR;
    return Token{type, text, start_line, start_col};
}

Token Lexer::ScanNumber() {
    size_t start_line = line_;
    size_t start_col = col_;
    size_t start_pos = pos_;
    while (pos_ < input_.size()) {
        char c = Peek();
        if (std::isdigit(c) || c == '.') {
            Advance();
        } else {
            break;
        }
    }
    std::string text = input_.substr(start_pos, pos_ - start_pos);
    return Token{TokenType::NUMBER, text, start_line, start_col};
}

Token Lexer::ScanString() {
    size_t start_line = line_;
    size_t start_col = col_;
    char quote_char = Advance();
    size_t start_pos = pos_;
    while (pos_ < input_.size()) {
        char c = Peek();
        if (c == quote_char) {
            Advance();
            std::string text = input_.substr(start_pos, pos_ - start_pos - 1);
            return Token{TokenType::STRING_LITERAL, text, start_line, start_col};
        }
        Advance();
    }
    std::string text = input_.substr(start_pos);
    return Token{TokenType::INVALID, text, start_line, start_col};
}
