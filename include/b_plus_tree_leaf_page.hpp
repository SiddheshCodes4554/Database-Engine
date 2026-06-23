#pragma once

#include "b_plus_tree_page.hpp"

class BPlusTreeLeafPage : public BPlusTreePage {
public:
    static constexpr size_t LEAF_CAPACITY = 250;

    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int32_t max_size = LEAF_CAPACITY);

    page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next_page_id);

    int64_t KeyAt(int index) const;
    void SetKeyAt(int index, int64_t key);

    RID ValueAt(int index) const;
    void SetValueAt(int index, RID value);

    int KeyIndex(int64_t key) const;
    bool Lookup(int64_t key, RID *result) const;
    int Insert(int64_t key, const RID &value);

    void MoveHalfTo(BPlusTreeLeafPage *recipient);
    void CopyNFrom(BPlusTreeLeafPage *src, int32_t start_index, int32_t num_elements);

private:
    page_id_t next_page_id_;
    int32_t leaf_padding_; // Padding to align header to 32 bytes (24 bytes base + 4 bytes next_page_id + 4 bytes padding)
    int64_t keys_[LEAF_CAPACITY];
    RID values_[LEAF_CAPACITY];
};
