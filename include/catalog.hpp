#pragma once

#include "common.hpp"
#include "page.hpp"
#include "buffer_pool_manager.hpp"
#include "b_plus_tree.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <stdexcept>
#include <algorithm>

enum class TypeID {
    INT,
    VARCHAR
};

class Value {
public:
    Value();
    explicit Value(int64_t val);
    explicit Value(std::string val);

    TypeID GetType() const;
    int64_t GetInt() const;
    const std::string& GetStr() const;

    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const;
    bool operator<(const Value &o) const;
    bool operator>(const Value &o) const;
    bool operator<=(const Value &o) const;
    bool operator>=(const Value &o) const;

    std::string ToString() const;

private:
    TypeID type_;
    std::string str_val_;
    int64_t int_val_;
};

class Column {
public:
    Column(std::string name, TypeID type, uint32_t length = 0);

    const std::string& GetName() const;
    TypeID GetType() const;
    uint32_t GetLength() const;
    uint32_t GetOffset() const;
    void SetOffset(uint32_t offset);

private:
    std::string name_;
    TypeID type_;
    uint32_t length_;
    uint32_t offset_;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns);

    const std::vector<Column>& GetColumns() const;
    uint32_t GetTupleSize() const;
    uint32_t GetColumnCount() const;
    int GetColIdx(const std::string &name) const;
    const Column& GetColumn(uint32_t col_idx) const;

private:
    std::vector<Column> columns_;
    uint32_t tuple_size_{0};
};

class Tuple {
public:
    Tuple() = default;
    explicit Tuple(std::vector<char> data);
    Tuple(const std::vector<Value> &values, const Schema &schema);

    Value GetValue(const Schema &schema, uint32_t col_idx) const;
    const std::vector<char>& GetData() const;
    bool IsNull() const;

private:
    std::vector<char> data_;
};

class TablePage {
public:
    explicit TablePage(Page *page);

    void Init();
    page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next_page_id);
    uint32_t GetNumSlots() const;
    void SetNumSlots(uint32_t num_slots);
    uint32_t GetFreeSpacePointer() const;
    void SetFreeSpacePointer(uint32_t fsp);

    struct Slot {
        uint32_t offset;
        uint32_t size;
    };

    Slot GetSlot(uint32_t slot_idx) const;
    void SetSlot(uint32_t slot_idx, Slot slot);

    bool InsertTuple(const Tuple &tuple, RID *rid);
    bool GetTuple(uint32_t slot_idx, Tuple *tuple) const;

private:
    Page *page_;
};

class TableHeap {
public:
    TableHeap(BufferPoolManager *bpm, page_id_t first_page_id = INVALID_PAGE_ID);

    page_id_t GetFirstPageId() const;
    bool InsertTuple(const Tuple &tuple, RID *rid);
    bool GetTuple(const RID &rid, Tuple *tuple) const;

private:
    BufferPoolManager *bpm_;
    page_id_t first_page_id_;
};

class TableMetadata {
public:
    TableMetadata(std::string name, Schema schema, std::unique_ptr<TableHeap> table_heap, page_id_t first_page_id);

    const std::string& GetName() const;
    const Schema& GetSchema() const;
    TableHeap* GetTableHeap() const;
    page_id_t GetFirstPageId() const;

private:
    std::string name_;
    Schema schema_;
    std::unique_ptr<TableHeap> table_heap_;
    page_id_t first_page_id_;
};

class IndexMetadata {
public:
    IndexMetadata(std::string name, std::string table_name, Schema schema, std::vector<uint32_t> key_attrs, std::unique_ptr<BPlusTree<IntComparator>> index);

    const std::string& GetName() const;
    const std::string& GetTableName() const;
    const Schema& GetSchema() const;
    const std::vector<uint32_t>& GetKeyAttrs() const;
    BPlusTree<IntComparator>* GetIndex() const;

private:
    std::string name_;
    std::string table_name_;
    Schema schema_;
    std::vector<uint32_t> key_attrs_;
    std::unique_ptr<BPlusTree<IntComparator>> index_;
};

class Catalog {
public:
    explicit Catalog(BufferPoolManager *bpm);

    bool CreateTable(const std::string &name, const Schema &schema);
    TableMetadata* GetTable(const std::string &name) const;

    bool CreateIndex(const std::string &index_name, const std::string &table_name, const std::vector<uint32_t> &key_attrs);
    IndexMetadata* GetIndex(const std::string &index_name) const;
    std::vector<IndexMetadata*> GetTableIndexes(const std::string &table_name) const;

private:
    BufferPoolManager *bpm_;
    mutable std::unordered_map<std::string, std::unique_ptr<TableMetadata>> tables_;
    mutable std::unordered_map<std::string, std::unique_ptr<IndexMetadata>> indexes_;
};
