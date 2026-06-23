#include "optimizer.hpp"
#include <stdexcept>

// Expression compilation
std::unique_ptr<AbstractExpression> CompileExpression(const ASTExpression *ast_expr, const Schema *schema) {
    if (ast_expr == nullptr) return nullptr;

    if (ast_expr->GetType() == ASTNodeType::LITERAL_EXPRESSION) {
        auto lit = static_cast<const LiteralExpression*>(ast_expr);
        if (lit->GetLiteralType() == LiteralType::NUMBER) {
            int64_t val = std::stoll(lit->GetValue());
            return std::make_unique<ConstantValueExpression>(Value(val));
        } else {
            return std::make_unique<ConstantValueExpression>(Value(lit->GetValue()));
        }
    }
    if (ast_expr->GetType() == ASTNodeType::COLUMN_EXPRESSION) {
        auto col = static_cast<const ColumnExpression*>(ast_expr);
        int idx = schema->GetColIdx(col->GetName());
        if (idx == -1) {
            throw std::runtime_error("Column not found: " + col->GetName());
        }
        return std::make_unique<ColumnValueExpression>(static_cast<uint32_t>(idx), schema->GetColumn(idx).GetType());
    }
    if (ast_expr->GetType() == ASTNodeType::BINARY_EXPRESSION) {
        auto bin = static_cast<const BinaryExpression*>(ast_expr);
        std::string op = bin->GetOp();
        if (op == "AND" || op == "OR") {
            return std::make_unique<LogicalExpression>(
                CompileExpression(bin->GetLeft(), schema),
                op,
                CompileExpression(bin->GetRight(), schema)
            );
        } else {
            return std::make_unique<ComparisonExpression>(
                CompileExpression(bin->GetLeft(), schema),
                op,
                CompileExpression(bin->GetRight(), schema)
            );
        }
    }
    throw std::runtime_error("Unknown AST expression type");
}

// Planner implementation
Planner::Planner(Catalog *catalog) : catalog_(catalog) {}

std::unique_ptr<AbstractPlanNode> Planner::Plan(const SQLStatement *stmt) {
    if (stmt->GetType() == ASTNodeType::SELECT_STATEMENT) {
        return PlanSelect(static_cast<const SelectStatement*>(stmt));
    }
    if (stmt->GetType() == ASTNodeType::INSERT_STATEMENT) {
        return PlanInsert(static_cast<const InsertStatement*>(stmt));
    }
    throw std::runtime_error("Unsupported AST statement type");
}

std::unique_ptr<AbstractPlanNode> Planner::PlanSelect(const SelectStatement *stmt) {
    auto tbl = catalog_->GetTable(stmt->GetTableName());
    if (tbl == nullptr) {
        throw std::runtime_error("Table not found: " + stmt->GetTableName());
    }

    auto scan = std::make_unique<SeqScanPlanNode>(tbl->GetSchema(), stmt->GetTableName());
    std::unique_ptr<AbstractPlanNode> plan = std::move(scan);

    if (stmt->GetWhereClause() != nullptr) {
        auto pred = CompileExpression(stmt->GetWhereClause(), &tbl->GetSchema());
        plan = std::make_unique<FilterPlanNode>(std::move(plan), std::move(pred));
    }
    return plan;
}

std::unique_ptr<AbstractPlanNode> Planner::PlanInsert(const InsertStatement *stmt) {
    auto tbl = catalog_->GetTable(stmt->GetTableName());
    if (tbl == nullptr) {
        throw std::runtime_error("Table not found: " + stmt->GetTableName());
    }

    std::vector<std::vector<std::unique_ptr<AbstractExpression>>> values;
    std::vector<std::unique_ptr<AbstractExpression>> row;
    for (const auto &val : stmt->GetValues()) {
        row.push_back(CompileExpression(val.get(), &tbl->GetSchema()));
    }
    values.push_back(std::move(row));

    return std::make_unique<InsertPlanNode>(stmt->GetTableName(), std::move(values));
}

// Optimizer implementation
Optimizer::Optimizer(Catalog *catalog) : catalog_(catalog) {}

std::unique_ptr<AbstractPlanNode> Optimizer::Optimize(std::unique_ptr<AbstractPlanNode> plan) {
    // Recursively optimize children first
    std::vector<std::unique_ptr<AbstractPlanNode>> optimized_children;
    
    // Apply Rule 1: Push Down Filter
    if (plan->GetType() == PlanType::Filter) {
        auto filter = static_cast<FilterPlanNode*>(plan.get());
        auto child = filter->GetChildren()[0].get();
        if (child->GetType() == PlanType::SeqScan) {
            auto scan = static_cast<const SeqScanPlanNode*>(child);
            auto new_scan = std::make_unique<SeqScanPlanNode>(
                *scan->GetOutputSchema(),
                scan->GetTableName(),
                filter->GetPredicate()->Clone()
            );
            return Optimize(std::move(new_scan));
        }
    }

    // Apply Rule 2: SeqScan with Filter to IndexScan replacement
    if (plan->GetType() == PlanType::SeqScan) {
        auto scan = static_cast<SeqScanPlanNode*>(plan.get());
        if (scan->GetFilterPredicate() != nullptr) {
            auto indexes = catalog_->GetTableIndexes(scan->GetTableName());
            for (auto idx_meta : indexes) {
                IndexMatch match = MatchIndex(scan->GetFilterPredicate(), idx_meta);
                if (match.matched) {
                    return std::make_unique<IndexScanPlanNode>(
                        *scan->GetOutputSchema(),
                        scan->GetTableName(),
                        idx_meta->GetName(),
                        scan->GetFilterPredicate()->Clone()
                    );
                }
            }
        }
    }

    return plan;
}

Optimizer::IndexMatch Optimizer::MatchIndex(const AbstractExpression *expr, const IndexMetadata *idx_meta) {
    if (expr == nullptr) return {false, ""};

    if (auto comp = dynamic_cast<const ComparisonExpression*>(expr)) {
        auto left = comp->GetLeft();
        auto right = comp->GetRight();

        auto col_expr = dynamic_cast<const ColumnValueExpression*>(left);
        auto const_expr = dynamic_cast<const ConstantValueExpression*>(right);

        if (col_expr == nullptr || const_expr == nullptr) {
            col_expr = dynamic_cast<const ColumnValueExpression*>(right);
            const_expr = dynamic_cast<const ConstantValueExpression*>(left);
        }

        if (col_expr != nullptr && const_expr != nullptr) {
            for (auto idx_col_idx : idx_meta->GetKeyAttrs()) {
                if (col_expr->GetColIdx() == idx_col_idx) {
                    return {true, comp->GetOp()};
                }
            }
        }
    }

    if (auto logical = dynamic_cast<const LogicalExpression*>(expr)) {
        if (logical->GetOp() == "AND") {
            auto match_left = MatchIndex(logical->GetLeft(), idx_meta);
            if (match_left.matched) return match_left;
            return MatchIndex(logical->GetRight(), idx_meta);
        }
    }

    return {false, ""};
}
