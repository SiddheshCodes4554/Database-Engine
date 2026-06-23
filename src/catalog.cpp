#include "catalog.hpp"

// Value implementation
Value::Value() : type_(TypeID::INT), int_val_(0) {}
Value::Value(int64_t val) : type_(TypeID::INT), int_val_(val) {}
Value::Value(std::string val) : type_(TypeID::VARCHAR), str_val_(std::move(val)), int_val_(0) {}

TypeID Value::GetType() const { return type_; }
int64_t Value::GetInt() const { return int_val_; }
const std::string& Value::GetStr() const { return str_val_; }

bool Value::operator==(const Value &o) const {
    if (type_ != o.type_) return false;
    return type_ == TypeID::INT ? int_val_ == o.int_val_ : str_val_ == o.str_val_;
}
bool Value::operator!=(const Value &o) const { return !(*this == o); }
bool Value::operator<(const Value &o) const {
    if (type_ != o.type_) throw std::runtime_error("Type mismatch in comparison");
    return type_ == TypeID::INT ? int_val_ < o.int_val_ : str_val_ < o.str_val_;
}
bool Value::operator>(const Value &o) const {
    if (type_ != o.type_) throw std::runtime_error("Type mismatch in comparison");
    return type_ == TypeID::INT ? int_val_ > o.int_val_ : str_val_ > o.str_val_;
}
bool Value::operator<=(const Value &o) const { return !(*this > o); }
bool Value::operator>=(const Value &o) const { return !(*this < o); }

std::string Value::ToString() const {
    return type_ == TypeID::INT ? std::to_string(int_val_) : str_val_;
}

// Column implementation
Column::Column(std::string name, TypeID type, uint32_t length)
    : name_(std::move(name)), type_(type), length_(length), offset_(0) {}

const std::string& Column::GetName() const { return name_; }
TypeID Column::GetType() const { return type_; }
uint32_t Column::GetLength() const { return length_; }
uint32_t Column::GetOffset() const { return offset_; }
void Column::SetOffset(uint32_t offset) { offset_ = offset; }

// Schema implementation
Schema::Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
    uint32_t curr_offset = 0;
    for (auto &col : columns_) {
        col.SetOffset(curr_offset);
        if (col.GetType() == TypeID::INT) {
            curr_offset += sizeof(int64_t);
        } else {
            curr_offset += sizeof(uint32_t) + col.GetLength();
        }
    }
    tuple_size_ = curr_offset;
}

const std::vector<Column>& Schema::GetColumns() const { return columns_; }
uint32_t Schema::GetTupleSize() const { return tuple_size_; }
uint32_t Schema::GetColumnCount() const { return columns_.size(); }
int Schema::GetColIdx(const std::string &name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].GetName() == name) return static_cast<int>(i);
    }
    return -1;
}
const Column& Schema::GetColumn(uint32_t col_idx) const {
    return columns_[col_idx];
}

// Tuple implementation
Tuple::Tuple(std::vector<char> data) : data_(std::move(data)) {}
Tuple::Tuple(const std::vector<Value> &values, const Schema &schema) {
    data_.resize(schema.GetTupleSize(), 0);
    for (uint32_t i = 0; i < values.size() && i < schema.GetColumnCount(); ++i) {
        const auto &val = values[i];
        const auto &col = schema.GetColumn(i);
        char *dest = data_.data() + col.GetOffset();
        if (col.GetType() == TypeID::INT) {
            int64_t int_val = val.GetInt();
            std::memcpy(dest, &int_val, sizeof(int64_t));
        } else {
            std::string str_val = val.GetStr();
            uint32_t len = static_cast<uint32_t>(str_val.size());
            if (len > col.GetLength()) len = col.GetLength();
            std::memcpy(dest, &len, sizeof(uint32_t));
            std::memcpy(dest + sizeof(uint32_t), str_val.data(), len);
        }
    }
}

Value Tuple::GetValue(const Schema &schema, uint32_t col_idx) const {
    const auto &col = schema.GetColumn(col_idx);
    const char *src = data_.data() + col.GetOffset();
    if (col.GetType() == TypeID::INT) {
        int64_t val;
        std::memcpy(&val, src, sizeof(int64_t));
        return Value(val);
    } else {
        uint32_t len;
        std::memcpy(&len, src, sizeof(uint32_t));
        std::string val(src + sizeof(uint32_t), len);
        return Value(val);
    }
}

const std::vector<char>& Tuple::GetData() const { return data_; }
bool Tuple::IsNull() const { return data_.empty(); }

// TablePage implementation
TablePage::TablePage(Page *page) : page_(page) {}

void TablePage::Init() {
    char *payload = page_->GetPayload();
    page_id_t next_id = INVALID_PAGE_ID;
    uint32_t zero = 0;
    uint32_t initial_free = Page::PAYLOAD_SIZE;
    std::memcpy(payload, &next_id, sizeof(page_id_t));
    std::memcpy(payload + sizeof(page_id_t), &zero, sizeof(uint32_t));
    std::memcpy(payload + sizeof(page_id_t) + sizeof(uint32_t), &initial_free, sizeof(uint32_t));
    page_->SetDirty(true);
}

page_id_t TablePage::GetNextPageId() const {
    page_id_t val;
    std::memcpy(&val, page_->GetPayload(), sizeof(page_id_t));
    return val;
}

void TablePage::SetNextPageId(page_id_t next_page_id) {
    std::memcpy(page_->GetPayload(), &next_page_id, sizeof(page_id_t));
    page_->SetDirty(true);
}

uint32_t TablePage::GetNumSlots() const {
    uint32_t val;
    std::memcpy(&val, page_->GetPayload() + sizeof(page_id_t), sizeof(uint32_t));
    return val;
}

void TablePage::SetNumSlots(uint32_t num_slots) {
    std::memcpy(page_->GetPayload() + sizeof(page_id_t), &num_slots, sizeof(uint32_t));
    page_->SetDirty(true);
}

uint32_t TablePage::GetFreeSpacePointer() const {
    uint32_t val;
    std::memcpy(&val, page_->GetPayload() + sizeof(page_id_t) + sizeof(uint32_t), sizeof(uint32_t));
    return val;
}

void TablePage::SetFreeSpacePointer(uint32_t fsp) {
    std::memcpy(page_->GetPayload() + sizeof(page_id_t) + sizeof(uint32_t), &fsp, sizeof(uint32_t));
    page_->SetDirty(true);
}

TablePage::Slot TablePage::GetSlot(uint32_t slot_idx) const {
    Slot slot;
    const char *addr = page_->GetPayload() + sizeof(page_id_t) + 8 + slot_idx * sizeof(Slot);
    std::memcpy(&slot.offset, addr, sizeof(uint32_t));
    std::memcpy(&slot.size, addr + sizeof(uint32_t), sizeof(uint32_t));
    return slot;
}

void TablePage::SetSlot(uint32_t slot_idx, Slot slot) {
    char *addr = page_->GetPayload() + sizeof(page_id_t) + 8 + slot_idx * sizeof(Slot);
    std::memcpy(addr, &slot.offset, sizeof(uint32_t));
    std::memcpy(addr + sizeof(uint32_t), &slot.size, sizeof(uint32_t));
    page_->SetDirty(true);
}

bool TablePage::InsertTuple(const Tuple &tuple, RID *rid) {
    uint32_t num_slots = GetNumSlots();
    uint32_t fsp = GetFreeSpacePointer();
    uint32_t tuple_size = static_cast<uint32_t>(tuple.GetData().size());
    uint32_t slot_array_end = sizeof(page_id_t) + 8 + (num_slots + 1) * sizeof(Slot);
    if (fsp < slot_array_end || fsp - slot_array_end < tuple_size) {
        return false;
    }
    uint32_t new_offset = fsp - tuple_size;
    std::memcpy(page_->GetPayload() + new_offset, tuple.GetData().data(), tuple_size);
    SetFreeSpacePointer(new_offset);
    SetSlot(num_slots, Slot{new_offset, tuple_size});
    SetNumSlots(num_slots + 1);
    rid->page_id = page_->GetPageId();
    rid->slot_offset = num_slots;
    return true;
}

bool TablePage::GetTuple(uint32_t slot_idx, Tuple *tuple) const {
    if (slot_idx >= GetNumSlots()) return false;
    Slot slot = GetSlot(slot_idx);
    if (slot.size == 0) return false;
    std::vector<char> bytes(slot.size);
    std::memcpy(bytes.data(), page_->GetPayload() + slot.offset, slot.size);
    *tuple = Tuple(std::move(bytes));
    return true;
}

// TableHeap implementation
TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id) {
    if (first_page_id_ == INVALID_PAGE_ID) {
        Page *page = bpm_->NewPage(first_page_id_);
        if (page == nullptr) {
            throw std::runtime_error("Failed to allocate first page for TableHeap");
        }
        TablePage table_page(page);
        table_page.Init();
        bpm_->UnpinPage(first_page_id_, true);
    }
}

page_id_t TableHeap::GetFirstPageId() const { return first_page_id_; }

bool TableHeap::InsertTuple(const Tuple &tuple, RID *rid) {
    page_id_t curr_page_id = first_page_id_;
    while (curr_page_id != INVALID_PAGE_ID) {
        Page *page = bpm_->FetchPage(curr_page_id);
        if (page == nullptr) return false;
        page->WLatch();
        TablePage table_page(page);
        if (table_page.InsertTuple(tuple, rid)) {
            page->WUnlatch();
            bpm_->UnpinPage(curr_page_id, true);
            return true;
        }
        page_id_t next_page_id = table_page.GetNextPageId();
        page->WUnlatch();
        bpm_->UnpinPage(curr_page_id, false);

        if (next_page_id == INVALID_PAGE_ID) {
            page_id_t new_page_id;
            Page *new_page = bpm_->NewPage(new_page_id);
            if (new_page == nullptr) return false;
            new_page->WLatch();
            TablePage new_table_page(new_page);
            new_table_page.Init();

            Page *old_page = bpm_->FetchPage(curr_page_id);
            old_page->WLatch();
            TablePage old_table_page(old_page);
            old_table_page.SetNextPageId(new_page_id);
            old_page->WUnlatch();
            bpm_->UnpinPage(curr_page_id, true);

            bool success = new_table_page.InsertTuple(tuple, rid);
            new_page->WUnlatch();
            bpm_->UnpinPage(new_page_id, success);
            return success;
        }
        curr_page_id = next_page_id;
    }
    return false;
}

bool TableHeap::GetTuple(const RID &rid, Tuple *tuple) const {
    if (rid.page_id == INVALID_PAGE_ID) return false;
    Page *page = bpm_->FetchPage(rid.page_id);
    if (page == nullptr) return false;
    page->RLatch();
    TablePage table_page(page);
    bool success = table_page.GetTuple(rid.slot_offset, tuple);
    page->RUnlatch();
    bpm_->UnpinPage(rid.page_id, false);
    return success;
}

// TableMetadata implementation
TableMetadata::TableMetadata(std::string name, Schema schema, std::unique_ptr<TableHeap> table_heap, page_id_t first_page_id)
    : name_(std::move(name)), schema_(std::move(schema)), table_heap_(std::move(table_heap)), first_page_id_(first_page_id) {}

const std::string& TableMetadata::GetName() const { return name_; }
const Schema& TableMetadata::GetSchema() const { return schema_; }
TableHeap* TableMetadata::GetTableHeap() const { return table_heap_.get(); }
page_id_t TableMetadata::GetFirstPageId() const { return first_page_id_; }

// IndexMetadata implementation
IndexMetadata::IndexMetadata(std::string name, std::string table_name, Schema schema, std::vector<uint32_t> key_attrs, std::unique_ptr<BPlusTree<IntComparator>> index)
    : name_(std::move(name)), table_name_(std::move(table_name)), schema_(std::move(schema)), key_attrs_(std::move(key_attrs)), index_(std::move(index)) {}

const std::string& IndexMetadata::GetName() const { return name_; }
const std::string& IndexMetadata::GetTableName() const { return table_name_; }
const Schema& IndexMetadata::GetSchema() const { return schema_; }
const std::vector<uint32_t>& IndexMetadata::GetKeyAttrs() const { return key_attrs_; }
BPlusTree<IntComparator>* IndexMetadata::GetIndex() const { return index_.get(); }

// Catalog implementation
Catalog::Catalog(BufferPoolManager *bpm) : bpm_(bpm) {}

bool Catalog::CreateTable(const std::string &name, const Schema &schema) {
    if (tables_.find(name) != tables_.end()) return false;
    auto heap = std::make_unique<TableHeap>(bpm_);
    page_id_t first_page_id = heap->GetFirstPageId();
    tables_.emplace(name, std::make_unique<TableMetadata>(name, schema, std::move(heap), first_page_id));
    return true;
}

TableMetadata* Catalog::GetTable(const std::string &name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return it->second.get();
}

bool Catalog::CreateIndex(const std::string &index_name, const std::string &table_name, const std::vector<uint32_t> &key_attrs) {
    if (indexes_.find(index_name) != indexes_.end()) return false;
    auto tbl = GetTable(table_name);
    if (tbl == nullptr) return false;

    std::vector<Column> key_cols;
    for (auto attr : key_attrs) {
        key_cols.push_back(tbl->GetSchema().GetColumn(attr));
    }
    Schema key_schema(key_cols);

    auto index = std::make_unique<BPlusTree<IntComparator>>(index_name, bpm_);
    indexes_.emplace(index_name, std::make_unique<IndexMetadata>(index_name, table_name, key_schema, key_attrs, std::move(index)));
    return true;
}

IndexMetadata* Catalog::GetIndex(const std::string &index_name) const {
    auto it = indexes_.find(index_name);
    if (it == indexes_.end()) return nullptr;
    return it->second.get();
}

std::vector<IndexMetadata*> Catalog::GetTableIndexes(const std::string &table_name) const {
    std::vector<IndexMetadata*> result;
    for (const auto &pair : indexes_) {
        if (pair.second->GetTableName() == table_name) {
            result.push_back(pair.second.get());
        }
    }
    return result;
}
