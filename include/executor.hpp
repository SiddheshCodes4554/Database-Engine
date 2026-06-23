#pragma once

#include "catalog.hpp"
#include "plan.hpp"
#include "b_plus_tree_iterator.hpp"
#include <memory>
#include <vector>

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple *tuple, RID *rid) = 0;
    virtual const Schema* GetOutputSchema() const = 0;
};

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(const SeqScanPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm);
    void Init() override;
    bool Next(Tuple *tuple, RID *rid) override;
    const Schema* GetOutputSchema() const override;

private:
    const SeqScanPlanNode *plan_;
    Catalog *catalog_;
    BufferPoolManager *bpm_;
    TableMetadata *table_metadata_;
    page_id_t curr_page_id_;
    uint32_t curr_slot_idx_;
};

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(const IndexScanPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm);
    void Init() override;
    bool Next(Tuple *tuple, RID *rid) override;
    const Schema* GetOutputSchema() const override;

private:
    const IndexScanPlanNode *plan_;
    Catalog *catalog_;
    BufferPoolManager *bpm_;
    TableMetadata *table_metadata_;
    IndexMetadata *index_metadata_;
    bool is_point_lookup_;
    std::vector<RID> rids_;
    size_t rids_idx_;
    std::unique_ptr<BPlusTreeIterator> it_;
};

class InsertExecutor : public AbstractExecutor {
public:
    InsertExecutor(const InsertPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm, std::unique_ptr<AbstractExecutor> child = nullptr);
    void Init() override;
    bool Next(Tuple *tuple, RID *rid) override;
    const Schema* GetOutputSchema() const override;

private:
    const InsertPlanNode *plan_;
    Catalog *catalog_;
    BufferPoolManager *bpm_;
    std::unique_ptr<AbstractExecutor> child_;
    TableMetadata *table_metadata_;
    std::vector<IndexMetadata*> table_indexes_;
    Schema output_schema_;
    uint32_t inserted_count_;
    bool executed_;
};

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(const NestedLoopJoinPlanNode *plan, std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right);
    void Init() override;
    bool Next(Tuple *tuple, RID *rid) override;
    const Schema* GetOutputSchema() const override;

private:
    const NestedLoopJoinPlanNode *plan_;
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    Tuple outer_tuple_;
    RID outer_rid_;
    bool has_outer_tuple_;
};

class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(const FilterPlanNode *plan, std::unique_ptr<AbstractExecutor> child);
    void Init() override;
    bool Next(Tuple *tuple, RID *rid) override;
    const Schema* GetOutputSchema() const override;

private:
    const FilterPlanNode *plan_;
    std::unique_ptr<AbstractExecutor> child_;
};

std::unique_ptr<AbstractExecutor> BuildExecutor(const AbstractPlanNode *plan, Catalog *catalog, BufferPoolManager *bpm);
