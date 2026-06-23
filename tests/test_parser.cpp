#include "lexer.hpp"
#include "ast.hpp"
#include "parser.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>

void TestLexer() {
    std::cout << "Running TestLexer..." << std::endl;
    std::string sql = "SELECT id, name FROM users WHERE age > 18 AND status = 'active'";
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.Tokenize();

    assert(tokens.size() == 15);
    assert(tokens[0].type == TokenType::SELECT);
    assert(tokens[1].type == TokenType::IDENTIFIER && tokens[1].text == "id");
    assert(tokens[2].type == TokenType::COMMA);
    assert(tokens[3].type == TokenType::IDENTIFIER && tokens[3].text == "name");
    assert(tokens[4].type == TokenType::FROM);
    assert(tokens[5].type == TokenType::IDENTIFIER && tokens[5].text == "users");
    assert(tokens[6].type == TokenType::WHERE);
    assert(tokens[7].type == TokenType::IDENTIFIER && tokens[7].text == "age");
    assert(tokens[8].type == TokenType::GREATER);
    assert(tokens[9].type == TokenType::NUMBER && tokens[9].text == "18");
    assert(tokens[10].type == TokenType::AND);
    assert(tokens[11].type == TokenType::IDENTIFIER && tokens[11].text == "status");
    assert(tokens[12].type == TokenType::EQUALS);
    assert(tokens[13].type == TokenType::STRING_LITERAL && tokens[13].text == "active");
    assert(tokens[14].type == TokenType::EOF_TOKEN);

    std::cout << "TestLexer PASSED." << std::endl;
}

void TestCreateTableParser() {
    std::cout << "Running TestCreateTableParser..." << std::endl;
    std::string sql = "CREATE TABLE employees (id INT, name VARCHAR(100), salary INT)";
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    std::unique_ptr<SQLStatement> stmt = parser.Parse();

    assert(stmt != nullptr);
    assert(stmt->GetType() == ASTNodeType::CREATE_TABLE_STATEMENT);
    assert(stmt->ToString() == "CREATE TABLE employees (id INT, name VARCHAR(100), salary INT)");

    auto *create_stmt = reinterpret_cast<CreateTableStatement*>(stmt.get());
    assert(create_stmt->GetTableName() == "employees");
    assert(create_stmt->GetColumns().size() == 3);
    assert(create_stmt->GetColumns()[0].name == "id" && create_stmt->GetColumns()[0].type == "INT");
    assert(create_stmt->GetColumns()[1].name == "name" && create_stmt->GetColumns()[1].type == "VARCHAR(100)");

    // Error case
    std::string invalid_sql = "CREATE TABLE employees (id INT, name VARCHAR)";
    Lexer lexer2(invalid_sql);
    Parser parser2(lexer2.Tokenize());
    try {
        parser2.Parse();
        assert(false && "Should have thrown parser exception");
    } catch (const ParserException &ex) {
        assert(ex.GetLine() == 1);
        assert(ex.GetCol() == 45); // points to the closing ')' where length was expected
    }

    std::cout << "TestCreateTableParser PASSED." << std::endl;
}

void TestInsertParser() {
    std::cout << "Running TestInsertParser..." << std::endl;
    std::string sql = "INSERT INTO customers (id, name) VALUES (123, 'John Doe')";
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    std::unique_ptr<SQLStatement> stmt = parser.Parse();

    assert(stmt != nullptr);
    assert(stmt->GetType() == ASTNodeType::INSERT_STATEMENT);
    assert(stmt->ToString() == "INSERT INTO customers (id, name) VALUES (123, John Doe)");

    auto *insert_stmt = reinterpret_cast<InsertStatement*>(stmt.get());
    assert(insert_stmt->GetTableName() == "customers");
    assert(insert_stmt->GetColumns().size() == 2);
    assert(insert_stmt->GetColumns()[0] == "id");
    assert(insert_stmt->GetValues().size() == 2);
    assert(insert_stmt->GetValues()[0]->GetValue() == "123");
    assert(insert_stmt->GetValues()[1]->GetValue() == "John Doe");

    std::cout << "TestInsertParser PASSED." << std::endl;
}

void TestSelectParser() {
    std::cout << "Running TestSelectParser..." << std::endl;
    std::string sql = "SELECT name, age FROM users WHERE age >= 21 AND status = 'active' OR role = 'admin'";
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    std::unique_ptr<SQLStatement> stmt = parser.Parse();

    assert(stmt != nullptr);
    assert(stmt->GetType() == ASTNodeType::SELECT_STATEMENT);

    // Operator precedence check: AND has higher precedence than OR.
    // Expression tree should compile as: ((age >= 21 AND status = 'active') OR role = 'admin')
    // Stringified output: SELECT name, age FROM users WHERE (((age >= 21) AND (status = active)) OR (role = admin))
    assert(stmt->ToString() == "SELECT name, age FROM users WHERE (((age >= 21) AND (status = active)) OR (role = admin))");

    std::cout << "TestSelectParser PASSED." << std::endl;
}

void TestUpdateParser() {
    std::cout << "Running TestUpdateParser..." << std::endl;
    std::string sql = "UPDATE products SET price = 99.99, quantity = 5 WHERE id = 10";
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    std::unique_ptr<SQLStatement> stmt = parser.Parse();

    assert(stmt != nullptr);
    assert(stmt->GetType() == ASTNodeType::UPDATE_STATEMENT);
    assert(stmt->ToString() == "UPDATE products SET price = 99.99, quantity = 5 WHERE (id = 10)");

    std::cout << "TestUpdateParser PASSED." << std::endl;
}

void TestDeleteParser() {
    std::cout << "Running TestDeleteParser..." << std::endl;
    std::string sql = "DELETE FROM items WHERE price > 500";
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    std::unique_ptr<SQLStatement> stmt = parser.Parse();

    assert(stmt != nullptr);
    assert(stmt->GetType() == ASTNodeType::DELETE_STATEMENT);
    assert(stmt->ToString() == "DELETE FROM items WHERE (price > 500)");

    std::cout << "TestDeleteParser PASSED." << std::endl;
}

int main() {
    try {
        TestLexer();
        TestCreateTableParser();
        TestInsertParser();
        TestSelectParser();
        TestUpdateParser();
        TestDeleteParser();
        std::cout << "\nALL PARSER AND LEXER TESTS PASSED SUCCESSFULLY!" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
