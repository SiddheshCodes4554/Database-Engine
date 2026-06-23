#include "b_plus_tree_leaf_page.hpp"
#include <cassert>
#include <cstring>

void BPlusTreeLeafPage::Init(page_id_t page_id, page_id_t parent_id, int32_t max_size) {
    SetPageType(BPlusTreePageType::LEAF_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
    SetNextPageId(INVALID_PAGE_ID);
}

page_id_t BPlusTreeLeafPage::GetNextPageId() const {
    return next_page_id_;
}

void BPlusTreeLeafPage::SetNextPageId(page_id_t next_page_id) {
    next_page_id_ = next_page_id;
}

int64_t BPlusTreeLeafPage::KeyAt(int index) const {
    assert(index >= 0 && index < GetMaxSize());
    return keys_[index];
}

void BPlusTreeLeafPage::SetKeyAt(int index, int64_t key) {
    assert(index >= 0 && index < GetMaxSize());
    keys_[index] = key;
}

RID BPlusTreeLeafPage::ValueAt(int index) const {
    assert(index >= 0 && index < GetMaxSize());
    return values_[index];
}

void BPlusTreeLeafPage::SetValueAt(int index, RID value) {
    assert(index >= 0 && index < GetMaxSize());
    values_[index] = value;
}

int BPlusTreeLeafPage::KeyIndex(int64_t key) const {
    int low = 0, high = GetSize() - 1;
    int target_idx = GetSize();
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (keys_[mid] >= key) {
            target_idx = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return target_idx;
}

bool BPlusTreeLeafPage::Lookup(int64_t key, RID *result) const {
    int idx = KeyIndex(key);
    if (idx < GetSize() && keys_[idx] == key) {
        *result = values_[idx];
        return true;
    }
    return false;
}

int BPlusTreeLeafPage::Insert(int64_t key, const RID &value) {
    int idx = KeyIndex(key);
    int32_t size = GetSize();
    if (idx < size && keys_[idx] == key) {
        return size;
    }
    for (int i = size; i > idx; --i) {
        keys_[i] = keys_[i - 1];
        values_[i] = values_[i - 1];
    }
    keys_[idx] = key;
    values_[idx] = value;
    IncreaseSize(1);
    return GetSize();
}

void BPlusTreeLeafPage::MoveHalfTo(BPlusTreeLeafPage *recipient) {
    int32_t size = GetSize();
    int32_t start_index = size / 2;
    int32_t num_elements = size - start_index;
    recipient->CopyNFrom(this, start_index, num_elements);
    recipient->SetNextPageId(GetNextPageId());
    SetNextPageId(recipient->GetPageId());
    SetSize(start_index);
}

void BPlusTreeLeafPage::CopyNFrom(BPlusTreeLeafPage *src, int32_t start_index, int32_t num_elements) {
    int32_t current_size = GetSize();
    for (int i = 0; i < num_elements; ++i) {
        int src_idx = start_index + i;
        int dest_idx = current_size + i;
        keys_[dest_idx] = src->keys_[src_idx];
        values_[dest_idx] = src->values_[src_idx];
    }
    IncreaseSize(num_elements);
}
