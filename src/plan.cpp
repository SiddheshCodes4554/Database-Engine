#include "plan.hpp"

// ConstantValueExpression implementation
ConstantValueExpression::ConstantValueExpression(Value val) : val_(std::move(val)) {}

Value ConstantValueExpression::Evaluate(const Tuple *tuple, const Schema *schema) const {
    return val_;
}

std::unique_ptr<AbstractExpression> ConstantValueExpression::Clone() const {
    return std::make_unique<ConstantValueExpression>(val_);
}

std::string ConstantValueExpression::ToString() const {
    return val_.ToString();
}

// ColumnValueExpression implementation
ColumnValueExpression::ColumnValueExpression(uint32_t col_idx, TypeID type)
    : col_idx_(col_idx), type_(type) {}

Value ColumnValueExpression::Evaluate(const Tuple *tuple, const Schema *schema) const {
    return tuple->GetValue(*schema, col_idx_);
}

std::unique_ptr<AbstractExpression> ColumnValueExpression::Clone() const {
    return std::make_unique<ColumnValueExpression>(col_idx_, type_);
}

std::string ColumnValueExpression::ToString() const {
    return "Col#" + std::to_string(col_idx_);
}

uint32_t ColumnValueExpression::GetColIdx() const { return col_idx_; }
TypeID ColumnValueExpression::GetType() const { return type_; }

// ComparisonExpression implementation
ComparisonExpression::ComparisonExpression(std::unique_ptr<AbstractExpression> left, std::string op, std::unique_ptr<AbstractExpression> right)
    : left_(std::move(left)), op_(std::move(op)), right_(std::move(right)) {}

Value ComparisonExpression::Evaluate(const Tuple *tuple, const Schema *schema) const {
    Value lhs = left_->Evaluate(tuple, schema);
    Value rhs = right_->Evaluate(tuple, schema);
    if (op_ == "=") return Value(static_cast<int64_t>(lhs == rhs ? 1 : 0));
    if (op_ == "!=" || op_ == "<>") return Value(static_cast<int64_t>(lhs != rhs ? 1 : 0));
    if (op_ == "<") return Value(static_cast<int64_t>(lhs < rhs ? 1 : 0));
    if (op_ == ">") return Value(static_cast<int64_t>(lhs > rhs ? 1 : 0));
    if (op_ == "<=") return Value(static_cast<int64_t>(lhs <= rhs ? 1 : 0));
    if (op_ == ">=") return Value(static_cast<int64_t>(lhs >= rhs ? 1 : 0));
    throw std::runtime_error("Unsupported comparison operator: " + op_);
}

std::unique_ptr<AbstractExpression> ComparisonExpression::Clone() const {
    return std::make_unique<ComparisonExpression>(left_->Clone(), op_, right_->Clone());
}

std::string ComparisonExpression::ToString() const {
    return "(" + left_->ToString() + " " + op_ + " " + right_->ToString() + ")";
}

const AbstractExpression* ComparisonExpression::GetLeft() const { return left_.get(); }
const std::string& ComparisonExpression::GetOp() const { return op_; }
const AbstractExpression* ComparisonExpression::GetRight() const { return right_.get(); }

// LogicalExpression implementation
LogicalExpression::LogicalExpression(std::unique_ptr<AbstractExpression> left, std::string op, std::unique_ptr<AbstractExpression> right)
    : left_(std::move(left)), op_(std::move(op)), right_(std::move(right)) {}

Value LogicalExpression::Evaluate(const Tuple *tuple, const Schema *schema) const {
    Value lhs = left_->Evaluate(tuple, schema);
    Value rhs = right_->Evaluate(tuple, schema);
    bool l_bool = lhs.GetInt() != 0;
    bool r_bool = rhs.GetInt() != 0;
    if (op_ == "AND") return Value(static_cast<int64_t>((l_bool && r_bool) ? 1 : 0));
    if (op_ == "OR") return Value(static_cast<int64_t>((l_bool || r_bool) ? 1 : 0));
    throw std::runtime_error("Unsupported logical operator: " + op_);
}

std::unique_ptr<AbstractExpression> LogicalExpression::Clone() const {
    return std::make_unique<LogicalExpression>(left_->Clone(), op_, right_->Clone());
}

std::string LogicalExpression::ToString() const {
    return "(" + left_->ToString() + " " + op_ + " " + right_->ToString() + ")";
}

const AbstractExpression* LogicalExpression::GetLeft() const { return left_.get(); }
const std::string& LogicalExpression::GetOp() const { return op_; }
const AbstractExpression* LogicalExpression::GetRight() const { return right_.get(); }

// SeqScanPlanNode implementation
SeqScanPlanNode::SeqScanPlanNode(Schema schema, std::string table_name, std::unique_ptr<AbstractExpression> filter_predicate)
    : schema_(std::move(schema)), table_name_(std::move(table_name)), filter_predicate_(std::move(filter_predicate)) {}

PlanType SeqScanPlanNode::GetType() const { return PlanType::SeqScan; }
const Schema* SeqScanPlanNode::GetOutputSchema() const { return &schema_; }
const std::vector<std::unique_ptr<AbstractPlanNode>>& SeqScanPlanNode::GetChildren() const { return children_; }
const std::string& SeqScanPlanNode::GetTableName() const { return table_name_; }
const AbstractExpression* SeqScanPlanNode::GetFilterPredicate() const { return filter_predicate_.get(); }

std::unique_ptr<AbstractPlanNode> SeqScanPlanNode::Clone() const {
    return std::make_unique<SeqScanPlanNode>(schema_, table_name_, filter_predicate_ ? filter_predicate_->Clone() : nullptr);
}

// IndexScanPlanNode implementation
IndexScanPlanNode::IndexScanPlanNode(Schema schema, std::string table_name, std::string index_name, std::unique_ptr<AbstractExpression> scan_predicate)
    : schema_(std::move(schema)), table_name_(std::move(table_name)), index_name_(std::move(index_name)), scan_predicate_(std::move(scan_predicate)) {}

PlanType IndexScanPlanNode::GetType() const { return PlanType::IndexScan; }
const Schema* IndexScanPlanNode::GetOutputSchema() const { return &schema_; }
const std::vector<std::unique_ptr<AbstractPlanNode>>& IndexScanPlanNode::GetChildren() const { return children_; }
const std::string& IndexScanPlanNode::GetTableName() const { return table_name_; }
const std::string& IndexScanPlanNode::GetIndexName() const { return index_name_; }
const AbstractExpression* IndexScanPlanNode::GetScanPredicate() const { return scan_predicate_.get(); }

std::unique_ptr<AbstractPlanNode> IndexScanPlanNode::Clone() const {
    return std::make_unique<IndexScanPlanNode>(schema_, table_name_, index_name_, scan_predicate_ ? scan_predicate_->Clone() : nullptr);
}

// InsertPlanNode implementation
InsertPlanNode::InsertPlanNode(std::string table_name, std::vector<std::vector<std::unique_ptr<AbstractExpression>>> values)
    : table_name_(std::move(table_name)), values_(std::move(values)) {}

PlanType InsertPlanNode::GetType() const { return PlanType::Insert; }
const Schema* InsertPlanNode::GetOutputSchema() const { return nullptr; }
const std::vector<std::unique_ptr<AbstractPlanNode>>& InsertPlanNode::GetChildren() const { return children_; }
const std::string& InsertPlanNode::GetTableName() const { return table_name_; }
const std::vector<std::vector<std::unique_ptr<AbstractExpression>>>& InsertPlanNode::GetValues() const { return values_; }

std::unique_ptr<AbstractPlanNode> InsertPlanNode::Clone() const {
    std::vector<std::vector<std::unique_ptr<AbstractExpression>>> vals;
    for (const auto &row : values_) {
        std::vector<std::unique_ptr<AbstractExpression>> r;
        for (const auto &expr : row) {
            r.push_back(expr->Clone());
        }
        vals.push_back(std::move(r));
    }
    return std::make_unique<InsertPlanNode>(table_name_, std::move(vals));
}

// NestedLoopJoinPlanNode implementation
NestedLoopJoinPlanNode::NestedLoopJoinPlanNode(Schema schema, std::unique_ptr<AbstractPlanNode> left, std::unique_ptr<AbstractPlanNode> right, std::unique_ptr<AbstractExpression> join_predicate)
    : schema_(std::move(schema)), join_predicate_(std::move(join_predicate)) {
    children_.push_back(std::move(left));
    children_.push_back(std::move(right));
}

PlanType NestedLoopJoinPlanNode::GetType() const { return PlanType::NestedLoopJoin; }
const Schema* NestedLoopJoinPlanNode::GetOutputSchema() const { return &schema_; }
const std::vector<std::unique_ptr<AbstractPlanNode>>& NestedLoopJoinPlanNode::GetChildren() const { return children_; }
const AbstractExpression* NestedLoopJoinPlanNode::GetJoinPredicate() const { return join_predicate_.get(); }

std::unique_ptr<AbstractPlanNode> NestedLoopJoinPlanNode::Clone() const {
    return std::make_unique<NestedLoopJoinPlanNode>(schema_, children_[0]->Clone(), children_[1]->Clone(), join_predicate_ ? join_predicate_->Clone() : nullptr);
}

// FilterPlanNode implementation
FilterPlanNode::FilterPlanNode(std::unique_ptr<AbstractPlanNode> child, std::unique_ptr<AbstractExpression> predicate)
    : predicate_(std::move(predicate)) {
    schema_ = *child->GetOutputSchema();
    children_.push_back(std::move(child));
}

PlanType FilterPlanNode::GetType() const { return PlanType::Filter; }
const Schema* FilterPlanNode::GetOutputSchema() const { return &schema_; }
const std::vector<std::unique_ptr<AbstractPlanNode>>& FilterPlanNode::GetChildren() const { return children_; }
const AbstractExpression* FilterPlanNode::GetPredicate() const { return predicate_.get(); }

std::unique_ptr<AbstractPlanNode> FilterPlanNode::Clone() const {
    return std::make_unique<FilterPlanNode>(children_[0]->Clone(), predicate_->Clone());
}
