#pragma once

#include "b_plus_tree_page.hpp"
#include "buffer_pool_manager.hpp"

class BPlusTreeInternalPage : public BPlusTreePage {
public:
    static constexpr size_t INTERNAL_CAPACITY = 250;

    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int32_t max_size = INTERNAL_CAPACITY);

    int64_t KeyAt(int index) const;
    void SetKeyAt(int index, int64_t key);

    page_id_t ValueAt(int index) const;
    void SetValueAt(int index, page_id_t value);

    page_id_t Lookup(int64_t key) const;
    int ValueIndex(page_id_t value) const;

    void PopulateAll(page_id_t val0, int64_t key1, page_id_t val1);
    void InsertNodeAfter(page_id_t old_value, int64_t new_key, page_id_t new_value);

    void MoveHalfTo(BPlusTreeInternalPage *recipient, BufferPoolManager *bpm);
    void CopyNFrom(BPlusTreeInternalPage *src, int32_t start_index, int32_t num_elements, BufferPoolManager *bpm);

private:
    int32_t internal_padding_[2]; // Header padding to align to 32 bytes (24 bytes base + 8 bytes padding)
    int64_t keys_[INTERNAL_CAPACITY];
    page_id_t values_[INTERNAL_CAPACITY];
};
