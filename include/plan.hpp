#pragma once

#include "catalog.hpp"
#include <memory>
#include <vector>
#include <string>

class AbstractExpression {
public:
    virtual ~AbstractExpression() = default;
    virtual Value Evaluate(const Tuple *tuple, const Schema *schema) const = 0;
    virtual std::unique_ptr<AbstractExpression> Clone() const = 0;
    virtual std::string ToString() const = 0;
};

class ConstantValueExpression : public AbstractExpression {
public:
    explicit ConstantValueExpression(Value val);
    Value Evaluate(const Tuple *tuple, const Schema *schema) const override;
    std::unique_ptr<AbstractExpression> Clone() const override;
    std::string ToString() const override;

private:
    Value val_;
};

class ColumnValueExpression : public AbstractExpression {
public:
    ColumnValueExpression(uint32_t col_idx, TypeID type);
    Value Evaluate(const Tuple *tuple, const Schema *schema) const override;
    std::unique_ptr<AbstractExpression> Clone() const override;
    std::string ToString() const override;
    uint32_t GetColIdx() const;
    TypeID GetType() const;

private:
    uint32_t col_idx_;
    TypeID type_;
};

class ComparisonExpression : public AbstractExpression {
public:
    ComparisonExpression(std::unique_ptr<AbstractExpression> left, std::string op, std::unique_ptr<AbstractExpression> right);
    Value Evaluate(const Tuple *tuple, const Schema *schema) const override;
    std::unique_ptr<AbstractExpression> Clone() const override;
    std::string ToString() const override;
    const AbstractExpression* GetLeft() const;
    const std::string& GetOp() const;
    const AbstractExpression* GetRight() const;

private:
    std::unique_ptr<AbstractExpression> left_;
    std::string op_;
    std::unique_ptr<AbstractExpression> right_;
};

class LogicalExpression : public AbstractExpression {
public:
    LogicalExpression(std::unique_ptr<AbstractExpression> left, std::string op, std::unique_ptr<AbstractExpression> right);
    Value Evaluate(const Tuple *tuple, const Schema *schema) const override;
    std::unique_ptr<AbstractExpression> Clone() const override;
    std::string ToString() const override;
    const AbstractExpression* GetLeft() const;
    const std::string& GetOp() const;
    const AbstractExpression* GetRight() const;

private:
    std::unique_ptr<AbstractExpression> left_;
    std::string op_;
    std::unique_ptr<AbstractExpression> right_;
};

enum class PlanType {
    SeqScan,
    IndexScan,
    Insert,
    NestedLoopJoin,
    Filter
};

class AbstractPlanNode {
public:
    virtual ~AbstractPlanNode() = default;
    virtual PlanType GetType() const = 0;
    virtual const Schema* GetOutputSchema() const = 0;
    virtual const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const = 0;
    virtual std::unique_ptr<AbstractPlanNode> Clone() const = 0;
};

class SeqScanPlanNode : public AbstractPlanNode {
public:
    SeqScanPlanNode(Schema schema, std::string table_name, std::unique_ptr<AbstractExpression> filter_predicate = nullptr);
    PlanType GetType() const override;
    const Schema* GetOutputSchema() const override;
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const override;
    const std::string& GetTableName() const;
    const AbstractExpression* GetFilterPredicate() const;
    std::unique_ptr<AbstractPlanNode> Clone() const override;

private:
    Schema schema_;
    std::string table_name_;
    std::unique_ptr<AbstractExpression> filter_predicate_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};

class IndexScanPlanNode : public AbstractPlanNode {
public:
    IndexScanPlanNode(Schema schema, std::string table_name, std::string index_name, std::unique_ptr<AbstractExpression> scan_predicate = nullptr);
    PlanType GetType() const override;
    const Schema* GetOutputSchema() const override;
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const override;
    const std::string& GetTableName() const;
    const std::string& GetIndexName() const;
    const AbstractExpression* GetScanPredicate() const;
    std::unique_ptr<AbstractPlanNode> Clone() const override;

private:
    Schema schema_;
    std::string table_name_;
    std::string index_name_;
    std::unique_ptr<AbstractExpression> scan_predicate_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};

class InsertPlanNode : public AbstractPlanNode {
public:
    InsertPlanNode(std::string table_name, std::vector<std::vector<std::unique_ptr<AbstractExpression>>> values);
    PlanType GetType() const override;
    const Schema* GetOutputSchema() const override;
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const override;
    const std::string& GetTableName() const;
    const std::vector<std::vector<std::unique_ptr<AbstractExpression>>>& GetValues() const;
    std::unique_ptr<AbstractPlanNode> Clone() const override;

private:
    std::string table_name_;
    std::vector<std::vector<std::unique_ptr<AbstractExpression>>> values_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};

class NestedLoopJoinPlanNode : public AbstractPlanNode {
public:
    NestedLoopJoinPlanNode(Schema schema, std::unique_ptr<AbstractPlanNode> left, std::unique_ptr<AbstractPlanNode> right, std::unique_ptr<AbstractExpression> join_predicate = nullptr);
    PlanType GetType() const override;
    const Schema* GetOutputSchema() const override;
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const override;
    const AbstractExpression* GetJoinPredicate() const;
    std::unique_ptr<AbstractPlanNode> Clone() const override;

private:
    Schema schema_;
    std::unique_ptr<AbstractExpression> join_predicate_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};

class FilterPlanNode : public AbstractPlanNode {
public:
    FilterPlanNode(std::unique_ptr<AbstractPlanNode> child, std::unique_ptr<AbstractExpression> predicate);
    PlanType GetType() const override;
    const Schema* GetOutputSchema() const override;
    const std::vector<std::unique_ptr<AbstractPlanNode>>& GetChildren() const override;
    const AbstractExpression* GetPredicate() const;
    std::unique_ptr<AbstractPlanNode> Clone() const override;

private:
    Schema schema_;
    std::unique_ptr<AbstractExpression> predicate_;
    std::vector<std::unique_ptr<AbstractPlanNode>> children_;
};
