#include "executor.hpp"
#include <stdexcept>

// SeqScanExecutor implementation
SeqScanExecutor::SeqScanExecutor(const SeqScanPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm)
    : plan_(plan), catalog_(catalog), bpm_(bpm), table_metadata_(nullptr), curr_page_id_(INVALID_PAGE_ID), curr_slot_idx_(0) {}

void SeqScanExecutor::Init() {
    table_metadata_ = catalog_->GetTable(plan_->GetTableName());
    if (table_metadata_ == nullptr) {
        throw std::runtime_error("Table not found in catalog: " + plan_->GetTableName());
    }
    curr_page_id_ = table_metadata_->GetFirstPageId();
    curr_slot_idx_ = 0;
}

bool SeqScanExecutor::Next(Tuple *tuple, RID *rid) {
    while (curr_page_id_ != INVALID_PAGE_ID) {
        Page *page = bpm_->FetchPage(curr_page_id_);
        if (page == nullptr) return false;
        page->RLatch();
        TablePage table_page(page);
        
        while (curr_slot_idx_ < table_page.GetNumSlots()) {
            Tuple temp_tuple;
            if (table_page.GetTuple(curr_slot_idx_, &temp_tuple)) {
                rid->page_id = curr_page_id_;
                rid->slot_offset = curr_slot_idx_;
                curr_slot_idx_++;
                page->RUnlatch();
                bpm_->UnpinPage(curr_page_id_, false);
                
                // If there's a filter predicate, check it
                if (plan_->GetFilterPredicate() != nullptr) {
                    Value val = plan_->GetFilterPredicate()->Evaluate(&temp_tuple, &table_metadata_->GetSchema());
                    if (val.GetInt() != 0) {
                        *tuple = std::move(temp_tuple);
                        return true;
                    }
                } else {
                    *tuple = std::move(temp_tuple);
                    return true;
                }
                
                // Fetch the page again to continue scanning
                page = bpm_->FetchPage(curr_page_id_);
                page->RLatch();
            } else {
                curr_slot_idx_++;
            }
        }
        
        page_id_t next_page_id = table_page.GetNextPageId();
        page->RUnlatch();
        bpm_->UnpinPage(curr_page_id_, false);
        curr_page_id_ = next_page_id;
        curr_slot_idx_ = 0;
    }
    return false;
}

const Schema* SeqScanExecutor::GetOutputSchema() const {
    return plan_->GetOutputSchema();
}

// IndexScanExecutor implementation
IndexScanExecutor::IndexScanExecutor(const IndexScanPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm)
    : plan_(plan), catalog_(catalog), bpm_(bpm), table_metadata_(nullptr), index_metadata_(nullptr),
      is_point_lookup_(false), rids_idx_(0), it_(nullptr) {}

void IndexScanExecutor::Init() {
    table_metadata_ = catalog_->GetTable(plan_->GetTableName());
    index_metadata_ = catalog_->GetIndex(plan_->GetIndexName());
    is_point_lookup_ = false;
    rids_.clear();
    rids_idx_ = 0;
    it_ = nullptr;

    auto bplus_tree = index_metadata_->GetIndex();
    auto scan_pred = plan_->GetScanPredicate();

    int64_t key_val = 0;
    bool has_key = false;
    std::string op = "";

    if (scan_pred != nullptr) {
        const ComparisonExpression *comp = nullptr;
        if (auto c = dynamic_cast<const ComparisonExpression*>(scan_pred)) {
            comp = c;
        } else if (auto log = dynamic_cast<const LogicalExpression*>(scan_pred)) {
            if (log->GetOp() == "AND") {
                if (auto c = dynamic_cast<const ComparisonExpression*>(log->GetLeft())) {
                    comp = c;
                } else if (auto c = dynamic_cast<const ComparisonExpression*>(log->GetRight())) {
                    comp = c;
                }
            }
        }

        if (comp != nullptr) {
            auto left = comp->GetLeft();
            auto right = comp->GetRight();
            
            auto col_expr = dynamic_cast<const ColumnValueExpression*>(left);
            auto const_expr = dynamic_cast<const ConstantValueExpression*>(right);
            
            if (col_expr == nullptr || const_expr == nullptr) {
                col_expr = dynamic_cast<const ColumnValueExpression*>(right);
                const_expr = dynamic_cast<const ConstantValueExpression*>(left);
            }

            if (col_expr != nullptr && const_expr != nullptr) {
                bool is_indexed_col = false;
                for (auto attr : index_metadata_->GetKeyAttrs()) {
                    if (col_expr->GetColIdx() == attr) {
                        is_indexed_col = true;
                        break;
                    }
                }
                if (is_indexed_col) {
                    Value val = const_expr->Evaluate(nullptr, nullptr);
                    key_val = val.GetInt();
                    has_key = true;
                    op = comp->GetOp();
                }
            }
        }
    }

    if (has_key) {
        if (op == "=") {
            is_point_lookup_ = true;
            bplus_tree->GetValue(key_val, &rids_);
            rids_idx_ = 0;
        } else if (op == ">=" || op == ">") {
            is_point_lookup_ = false;
            it_ = std::make_unique<BPlusTreeIterator>(bplus_tree->Begin(key_val));
        } else {
            is_point_lookup_ = false;
            it_ = std::make_unique<BPlusTreeIterator>(bplus_tree->Begin());
        }
    } else {
        is_point_lookup_ = false;
        it_ = std::make_unique<BPlusTreeIterator>(bplus_tree->Begin());
    }
}

bool IndexScanExecutor::Next(Tuple *tuple, RID *rid) {
    if (is_point_lookup_) {
        if (rids_idx_ < rids_.size()) {
            *rid = rids_[rids_idx_++];
            return table_metadata_->GetTableHeap()->GetTuple(*rid, tuple);
        }
        return false;
    } else {
        if (it_ == nullptr || it_->IsEnd()) {
            return false;
        }
        bool success = false;
        while (!it_->IsEnd()) {
            auto pair = **it_;
            *rid = pair.second;
            success = table_metadata_->GetTableHeap()->GetTuple(*rid, tuple);
            ++(*it_);
            if (success) {
                if (plan_->GetScanPredicate() != nullptr) {
                    Value val = plan_->GetScanPredicate()->Evaluate(tuple, &table_metadata_->GetSchema());
                    if (val.GetInt() == 0) {
                        success = false;
                        continue;
                    }
                }
                break;
            }
        }
        return success;
    }
}

const Schema* IndexScanExecutor::GetOutputSchema() const {
    return plan_->GetOutputSchema();
}

// InsertExecutor implementation
InsertExecutor::InsertExecutor(const InsertPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm, std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), catalog_(catalog), bpm_(bpm), child_(std::move(child)), table_metadata_(nullptr),
      output_schema_({Column("count", TypeID::INT)}), inserted_count_(0), executed_(false) {}

void InsertExecutor::Init() {
    table_metadata_ = catalog_->GetTable(plan_->GetTableName());
    if (table_metadata_ == nullptr) {
        throw std::runtime_error("Table not found in catalog: " + plan_->GetTableName());
    }
    table_indexes_ = catalog_->GetTableIndexes(plan_->GetTableName());
    if (child_ != nullptr) {
        child_->Init();
    }
    inserted_count_ = 0;
    executed_ = false;
}

bool InsertExecutor::Next(Tuple *tuple, RID *rid) {
    if (executed_) return false;
    
    if (child_ != nullptr) {
        Tuple child_tuple;
        RID child_rid;
        while (child_->Next(&child_tuple, &child_rid)) {
            RID new_rid;
            if (table_metadata_->GetTableHeap()->InsertTuple(child_tuple, &new_rid)) {
                for (auto idx_meta : table_indexes_) {
                    uint32_t key_attr = idx_meta->GetKeyAttrs()[0];
                    Value val = child_tuple.GetValue(table_metadata_->GetSchema(), key_attr);
                    idx_meta->GetIndex()->Insert(val.GetInt(), new_rid);
                }
                inserted_count_++;
            }
        }
    } else {
        for (const auto &row : plan_->GetValues()) {
            std::vector<Value> vals;
            for (const auto &expr : row) {
                vals.push_back(expr->Evaluate(nullptr, nullptr));
            }
            Tuple new_tuple(vals, table_metadata_->GetSchema());
            RID new_rid;
            if (table_metadata_->GetTableHeap()->InsertTuple(new_tuple, &new_rid)) {
                for (auto idx_meta : table_indexes_) {
                    uint32_t key_attr = idx_meta->GetKeyAttrs()[0];
                    Value val = new_tuple.GetValue(table_metadata_->GetSchema(), key_attr);
                    idx_meta->GetIndex()->Insert(val.GetInt(), new_rid);
                }
                inserted_count_++;
            }
        }
    }

    *tuple = Tuple({Value(static_cast<int64_t>(inserted_count_))}, output_schema_);
    rid->page_id = INVALID_PAGE_ID;
    rid->slot_offset = 0;
    executed_ = true;
    return true;
}

const Schema* InsertExecutor::GetOutputSchema() const {
    return &output_schema_;
}

// NestedLoopJoinExecutor implementation
NestedLoopJoinExecutor::NestedLoopJoinExecutor(const NestedLoopJoinPlanNode *plan, std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right)
    : plan_(plan), left_(std::move(left)), right_(std::move(right)), outer_rid_({INVALID_PAGE_ID, 0}), has_outer_tuple_(false) {}

void NestedLoopJoinExecutor::Init() {
    left_->Init();
    right_->Init();
    has_outer_tuple_ = left_->Next(&outer_tuple_, &outer_rid_);
}

bool NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) {
    const Schema *outer_schema = left_->GetOutputSchema();
    const Schema *inner_schema = right_->GetOutputSchema();
    
    while (has_outer_tuple_) {
        Tuple inner_tuple;
        RID inner_rid;
        while (right_->Next(&inner_tuple, &inner_rid)) {
            std::vector<Value> joined_values;
            for (uint32_t i = 0; i < outer_schema->GetColumnCount(); ++i) {
                joined_values.push_back(outer_tuple_.GetValue(*outer_schema, i));
            }
            for (uint32_t i = 0; i < inner_schema->GetColumnCount(); ++i) {
                joined_values.push_back(inner_tuple.GetValue(*inner_schema, i));
            }
            Tuple joined_tuple(joined_values, *plan_->GetOutputSchema());
            
            if (plan_->GetJoinPredicate() == nullptr) {
                *tuple = std::move(joined_tuple);
                rid->page_id = outer_rid_.page_id;
                rid->slot_offset = outer_rid_.slot_offset;
                return true;
            } else {
                Value res = plan_->GetJoinPredicate()->Evaluate(&joined_tuple, plan_->GetOutputSchema());
                if (res.GetInt() != 0) {
                    *tuple = std::move(joined_tuple);
                    rid->page_id = outer_rid_.page_id;
                    rid->slot_offset = outer_rid_.slot_offset;
                    return true;
                }
            }
        }
        
        right_->Init();
        has_outer_tuple_ = left_->Next(&outer_tuple_, &outer_rid_);
    }
    return false;
}

const Schema* NestedLoopJoinExecutor::GetOutputSchema() const {
    return plan_->GetOutputSchema();
}

// FilterExecutor implementation
FilterExecutor::FilterExecutor(const FilterPlanNode *plan, std::unique_ptr<AbstractExecutor> child)
    : plan_(plan), child_(std::move(child)) {}

void FilterExecutor::Init() {
    child_->Init();
}

bool FilterExecutor::Next(Tuple *tuple, RID *rid) {
    while (child_->Next(tuple, rid)) {
        Value res = plan_->GetPredicate()->Evaluate(tuple, plan_->GetOutputSchema());
        if (res.GetInt() != 0) {
            return true;
        }
    }
    return false;
}

const Schema* FilterExecutor::GetOutputSchema() const {
    return plan_->GetOutputSchema();
}

// BuildExecutor helper
std::unique_ptr<AbstractExecutor> BuildExecutor(const AbstractPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm) {
    if (plan->GetType() == PlanType::SeqScan) {
        return std::make_unique<SeqScanExecutor>(static_cast<const SeqScanPlanNode*>(plan), catalog, bpm);
    }
    if (plan->GetType() == PlanType::IndexScan) {
        return std::make_unique<IndexScanExecutor>(static_cast<const IndexScanPlanNode*>(plan), catalog, bpm);
    }
    if (plan->GetType() == PlanType::Insert) {
        return std::make_unique<InsertExecutor>(static_cast<const InsertPlanNode*>(plan), catalog, bpm);
    }
    if (plan->GetType() == PlanType::Filter) {
        auto filter = static_cast<const FilterPlanNode*>(plan);
        auto child = BuildExecutor(filter->GetChildren()[0].get(), catalog, bpm);
        return std::make_unique<FilterExecutor>(filter, std::move(child));
    }
    if (plan->GetType() == PlanType::NestedLoopJoin) {
        auto join = static_cast<const NestedLoopJoinPlanNode*>(plan);
        auto left = BuildExecutor(join->GetChildren()[0].get(), catalog, bpm);
        auto right = BuildExecutor(join->GetChildren()[1].get(), catalog, bpm);
        return std::make_unique<NestedLoopJoinExecutor>(join, std::move(left), std::move(right));
    }
    throw std::runtime_error("Unsupported plan node type");
}
