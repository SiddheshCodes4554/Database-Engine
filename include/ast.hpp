#pragma once

#include <string>
#include <vector>
#include <memory>
#include <utility>

enum class ASTNodeType {
    CREATE_TABLE_STATEMENT,
    INSERT_STATEMENT,
    SELECT_STATEMENT,
    UPDATE_STATEMENT,
    DELETE_STATEMENT,
    LITERAL_EXPRESSION,
    COLUMN_EXPRESSION,
    BINARY_EXPRESSION
};

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual ASTNodeType GetType() const = 0;
    virtual std::string ToString() const = 0;
};

class ASTExpression : public ASTNode {};

enum class LiteralType {
    NUMBER,
    STRING
};

class LiteralExpression : public ASTExpression {
public:
    LiteralExpression(LiteralType type, std::string value)
        : type_(type), value_(std::move(value)) {}

    ASTNodeType GetType() const override { return ASTNodeType::LITERAL_EXPRESSION; }
    std::string ToString() const override { return value_; }

    LiteralType GetLiteralType() const { return type_; }
    const std::string& GetValue() const { return value_; }

private:
    LiteralType type_;
    std::string value_;
};

class ColumnExpression : public ASTExpression {
public:
    explicit ColumnExpression(std::string name) : name_(std::move(name)) {}

    ASTNodeType GetType() const override { return ASTNodeType::COLUMN_EXPRESSION; }
    std::string ToString() const override { return name_; }

    const std::string& GetName() const { return name_; }

private:
    std::string name_;
};

class BinaryExpression : public ASTExpression {
public:
    BinaryExpression(std::unique_ptr<ASTExpression> left, std::string op, std::unique_ptr<ASTExpression> right)
        : left_(std::move(left)), op_(std::move(op)), right_(std::move(right)) {}

    ASTNodeType GetType() const override { return ASTNodeType::BINARY_EXPRESSION; }
    std::string ToString() const override {
        return "(" + left_->ToString() + " " + op_ + " " + right_->ToString() + ")";
    }

    const ASTExpression* GetLeft() const { return left_.get(); }
    const std::string& GetOp() const { return op_; }
    const ASTExpression* GetRight() const { return right_.get(); }

private:
    std::unique_ptr<ASTExpression> left_;
    std::string op_;
    std::unique_ptr<ASTExpression> right_;
};

class SQLStatement : public ASTNode {};

struct ColumnDefinition {
    std::string name;
    std::string type;
};

class CreateTableStatement : public SQLStatement {
public:
    CreateTableStatement(std::string table_name, std::vector<ColumnDefinition> columns)
        : table_name_(std::move(table_name)), columns_(std::move(columns)) {}

    ASTNodeType GetType() const override { return ASTNodeType::CREATE_TABLE_STATEMENT; }
    std::string ToString() const override;

    const std::string& GetTableName() const { return table_name_; }
    const std::vector<ColumnDefinition>& GetColumns() const { return columns_; }

private:
    std::string table_name_;
    std::vector<ColumnDefinition> columns_;
};

class InsertStatement : public SQLStatement {
public:
    InsertStatement(std::string table_name, std::vector<std::string> columns, std::vector<std::unique_ptr<LiteralExpression>> values)
        : table_name_(std::move(table_name)), columns_(std::move(columns)), values_(std::move(values)) {}

    ASTNodeType GetType() const override { return ASTNodeType::INSERT_STATEMENT; }
    std::string ToString() const override;

    const std::string& GetTableName() const { return table_name_; }
    const std::vector<std::string>& GetColumns() const { return columns_; }
    const std::vector<std::unique_ptr<LiteralExpression>>& GetValues() const { return values_; }

private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::vector<std::unique_ptr<LiteralExpression>> values_;
};

class SelectStatement : public SQLStatement {
public:
    SelectStatement(std::string table_name, std::vector<std::string> columns, std::unique_ptr<ASTExpression> where_clause)
        : table_name_(std::move(table_name)), columns_(std::move(columns)), where_clause_(std::move(where_clause)) {}

    ASTNodeType GetType() const override { return ASTNodeType::SELECT_STATEMENT; }
    std::string ToString() const override;

    const std::string& GetTableName() const { return table_name_; }
    const std::vector<std::string>& GetColumns() const { return columns_; }
    const ASTExpression* GetWhereClause() const { return where_clause_.get(); }

private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::unique_ptr<ASTExpression> where_clause_;
};

struct UpdateAssignment {
    std::string column;
    std::unique_ptr<ASTExpression> expression;
};

class UpdateStatement : public SQLStatement {
public:
    UpdateStatement(std::string table_name, std::vector<UpdateAssignment> assignments, std::unique_ptr<ASTExpression> where_clause)
        : table_name_(std::move(table_name)), assignments_(std::move(assignments)), where_clause_(std::move(where_clause)) {}

    ASTNodeType GetType() const override { return ASTNodeType::UPDATE_STATEMENT; }
    std::string ToString() const override;

    const std::string& GetTableName() const { return table_name_; }
    const std::vector<UpdateAssignment>& GetAssignments() const { return assignments_; }
    const ASTExpression* GetWhereClause() const { return where_clause_.get(); }

private:
    std::string table_name_;
    std::vector<UpdateAssignment> assignments_;
    std::unique_ptr<ASTExpression> where_clause_;
};

class DeleteStatement : public SQLStatement {
public:
    DeleteStatement(std::string table_name, std::unique_ptr<ASTExpression> where_clause)
        : table_name_(std::move(table_name)), where_clause_(std::move(where_clause)) {}

    ASTNodeType GetType() const override { return ASTNodeType::DELETE_STATEMENT; }
    std::string ToString() const override;

    const std::string& GetTableName() const { return table_name_; }
    const ASTExpression* GetWhereClause() const { return where_clause_.get(); }

private:
    std::string table_name_;
    std::unique_ptr<ASTExpression> where_clause_;
};
