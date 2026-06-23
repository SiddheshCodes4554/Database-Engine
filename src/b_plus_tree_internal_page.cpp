#include "b_plus_tree_internal_page.hpp"
#include <cstring>
#include <cassert>

void BPlusTreeInternalPage::Init(page_id_t page_id, page_id_t parent_id, int32_t max_size) {
    SetPageType(BPlusTreePageType::INTERNAL_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
}

int64_t BPlusTreeInternalPage::KeyAt(int index) const {
    assert(index >= 0 && index < GetMaxSize());
    return keys_[index];
}

void BPlusTreeInternalPage::SetKeyAt(int index, int64_t key) {
    assert(index >= 0 && index < GetMaxSize());
    keys_[index] = key;
}

page_id_t BPlusTreeInternalPage::ValueAt(int index) const {
    assert(index >= 0 && index < GetMaxSize());
    return values_[index];
}

void BPlusTreeInternalPage::SetValueAt(int index, page_id_t value) {
    assert(index >= 0 && index < GetMaxSize());
    values_[index] = value;
}

page_id_t BPlusTreeInternalPage::Lookup(int64_t key) const {
    int32_t size = GetSize();
    assert(size >= 1);
    if (size == 1) {
        return values_[0];
    }
    int low = 1, high = size - 1;
    int target_idx = 0;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (keys_[mid] <= key) {
            target_idx = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return values_[target_idx];
}

int BPlusTreeInternalPage::ValueIndex(page_id_t value) const {
    int32_t size = GetSize();
    for (int i = 0; i < size; ++i) {
        if (values_[i] == value) {
            return i;
        }
    }
    return -1;
}

void BPlusTreeInternalPage::PopulateAll(page_id_t val0, int64_t key1, page_id_t val1) {
    SetValueAt(0, val0);
    SetKeyAt(1, key1);
    SetValueAt(1, val1);
    SetSize(2);
}

void BPlusTreeInternalPage::InsertNodeAfter(page_id_t old_value, int64_t new_key, page_id_t new_value) {
    int index = ValueIndex(old_value);
    assert(index != -1);
    int32_t size = GetSize();
    for (int i = size; i > index + 1; --i) {
        keys_[i] = keys_[i - 1];
        values_[i] = values_[i - 1];
    }
    keys_[index + 1] = new_key;
    values_[index + 1] = new_value;
    IncreaseSize(1);
}

void BPlusTreeInternalPage::MoveHalfTo(BPlusTreeInternalPage *recipient, BufferPoolManager *bpm) {
    int32_t size = GetSize();
    int32_t start_index = size / 2;
    int32_t num_elements = size - start_index;
    recipient->CopyNFrom(this, start_index, num_elements, bpm);
    SetSize(start_index);
}

void BPlusTreeInternalPage::CopyNFrom(BPlusTreeInternalPage *src, int32_t start_index, int32_t num_elements, BufferPoolManager *bpm) {
    int32_t current_size = GetSize();
    for (int i = 0; i < num_elements; ++i) {
        int src_idx = start_index + i;
        int dest_idx = current_size + i;
        keys_[dest_idx] = src->keys_[src_idx];
        values_[dest_idx] = src->values_[src_idx];
        page_id_t child_id = src->values_[src_idx];
        Page *page = bpm->FetchPage(child_id);
        if (page != nullptr) {
            auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetPayload());
            tree_page->SetParentPageId(GetPageId());
            bpm->UnpinPage(child_id, true);
        }
    }
    IncreaseSize(num_elements);
}
