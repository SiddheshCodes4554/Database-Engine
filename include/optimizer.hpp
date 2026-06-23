#pragma once

#include "catalog.hpp"
#include "plan.hpp"
#include "ast.hpp"
#include <memory>
#include <vector>
#include <string>

std::unique_ptr<AbstractExpression> CompileExpression(const ASTExpression *ast_expr, const Schema *schema);

class Planner {
public:
    explicit Planner(Catalog *catalog);
    std::unique_ptr<AbstractPlanNode> Plan(const SQLStatement *stmt);

private:
    std::unique_ptr<AbstractPlanNode> PlanSelect(const SelectStatement *stmt);
    std::unique_ptr<AbstractPlanNode> PlanInsert(const InsertStatement *stmt);

    Catalog *catalog_;
};

class Optimizer {
public:
    explicit Optimizer(Catalog *catalog);
    std::unique_ptr<AbstractPlanNode> Optimize(std::unique_ptr<AbstractPlanNode> plan);

private:
    struct IndexMatch {
        bool matched{false};
        std::string op;
    };

    IndexMatch MatchIndex(const AbstractExpression *expr, const IndexMetadata *idx_meta);

    Catalog *catalog_;
};
