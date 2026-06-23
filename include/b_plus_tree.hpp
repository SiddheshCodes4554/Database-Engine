#pragma once

#include "common.hpp"
#include "page.hpp"
#include "buffer_pool_manager.hpp"
#include "b_plus_tree_page.hpp"
#include "b_plus_tree_internal_page.hpp"
#include "b_plus_tree_leaf_page.hpp"
#include "b_plus_tree_iterator.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

struct IntComparator {
    int operator()(int64_t lhs, int64_t rhs) const {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    }
};

template <typename KeyComparator = IntComparator>
class BPlusTree {
public:
    BPlusTree(std::string name, BufferPoolManager *bpm, int32_t leaf_max_size = BPlusTreeLeafPage::LEAF_CAPACITY, int32_t internal_max_size = BPlusTreeInternalPage::INTERNAL_CAPACITY)
        : index_name_(name), bpm_(bpm), root_page_id_(INVALID_PAGE_ID), leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {}

    bool IsEmpty() {
        LockGuard<Mutex> lock(root_latch_);
        return root_page_id_ == INVALID_PAGE_ID;
    }

    bool GetValue(int64_t key, std::vector<RID> *result) {
        root_latch_.lock();
        if (root_page_id_ == INVALID_PAGE_ID) {
            root_latch_.unlock();
            return false;
        }
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->RLatch();
        root_latch_.unlock();

        while (true) {
            auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->Lookup(key);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->RLatch();
            curr_page->RUnlatch();
            bpm_->UnpinPage(curr_page->GetPageId(), false);
            curr_page = child_page;
        }

        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page->GetPayload());
        RID val;
        bool found = leaf->Lookup(key, &val);
        if (found) {
            result->push_back(val);
        }
        curr_page->RUnlatch();
        bpm_->UnpinPage(curr_page->GetPageId(), false);
        return found;
    }

    bool Insert(int64_t key, const RID &value) {
        root_latch_.lock();
        bool root_latched = true;

        if (root_page_id_ == INVALID_PAGE_ID) {
            Page *root_page = bpm_->NewPage(root_page_id_);
            if (root_page == nullptr) {
                root_latch_.unlock();
                return false;
            }
            auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(root_page->GetPayload());
            leaf->Init(root_page_id_, INVALID_PAGE_ID, leaf_max_size_);
            leaf->Insert(key, value);
            bpm_->UnpinPage(root_page_id_, true);
            root_latch_.unlock();
            return true;
        }

        std::vector<Page *> locked_path;
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->WLatch();
        locked_path.push_back(curr_page);

        auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
        if (IsSafe(tree_page, OpType::INSERT)) {
            root_latch_.unlock();
            root_latched = false;
        }

        while (true) {
            tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->Lookup(key);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->WLatch();
            locked_path.push_back(child_page);

            auto *child_tree_page = reinterpret_cast<BPlusTreePage *>(child_page->GetPayload());
            if (IsSafe(child_tree_page, OpType::INSERT)) {
                for (size_t i = 0; i < locked_path.size() - 1; ++i) {
                    locked_path[i]->WUnlatch();
                    bpm_->UnpinPage(locked_path[i]->GetPageId(), false);
                }
                locked_path.erase(locked_path.begin(), locked_path.end() - 1);
                if (root_latched) {
                    root_latch_.unlock();
                    root_latched = false;
                }
            }
            curr_page = child_page;
        }

        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page->GetPayload());
        leaf->Insert(key, value);
        curr_page->SetDirty(true);

        if (leaf->GetSize() >= leaf->GetMaxSize()) {
            page_id_t new_leaf_id;
            Page *new_leaf_page = bpm_->NewPage(new_leaf_id);
            if (new_leaf_page != nullptr) {
                auto *new_leaf = reinterpret_cast<BPlusTreeLeafPage *>(new_leaf_page->GetPayload());
                new_leaf->Init(new_leaf_id, leaf->GetParentPageId(), leaf_max_size_);
                leaf->MoveHalfTo(new_leaf);
                int64_t split_key = new_leaf->KeyAt(0);
                InsertIntoParent(curr_page, split_key, new_leaf_page, locked_path, root_latched);
                bpm_->UnpinPage(new_leaf_id, true);
            }
        } else {
            for (auto *page : locked_path) {
                page->WUnlatch();
                bpm_->UnpinPage(page->GetPageId(), true);
            }
            if (root_latched) {
                root_latch_.unlock();
                root_latched = false;
            }
        }
        return true;
    }

    bool Remove(int64_t key) {
        root_latch_.lock();
        bool root_latched = true;

        if (root_page_id_ == INVALID_PAGE_ID) {
            root_latch_.unlock();
            return false;
        }

        std::vector<Page *> locked_path;
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->WLatch();
        locked_path.push_back(curr_page);

        auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
        if (IsSafe(tree_page, OpType::REMOVE)) {
            root_latch_.unlock();
            root_latched = false;
        }

        while (true) {
            tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->Lookup(key);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->WLatch();
            locked_path.push_back(child_page);

            auto *child_tree_page = reinterpret_cast<BPlusTreePage *>(child_page->GetPayload());
            if (IsSafe(child_tree_page, OpType::REMOVE)) {
                for (size_t i = 0; i < locked_path.size() - 1; ++i) {
                    locked_path[i]->WUnlatch();
                    bpm_->UnpinPage(locked_path[i]->GetPageId(), false);
                }
                locked_path.erase(locked_path.begin(), locked_path.end() - 1);
                if (root_latched) {
                    root_latch_.unlock();
                    root_latched = false;
                }
            }
            curr_page = child_page;
        }

        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page->GetPayload());
        int idx = leaf->KeyIndex(key);
        if (idx >= leaf->GetSize() || leaf->KeyAt(idx) != key) {
            for (auto *page : locked_path) {
                page->WUnlatch();
                bpm_->UnpinPage(page->GetPageId(), false);
            }
            if (root_latched) {
                root_latch_.unlock();
            }
            return false;
        }

        int32_t size = leaf->GetSize();
        for (int i = idx; i < size - 1; ++i) {
            leaf->SetKeyAt(i, leaf->KeyAt(i + 1));
            leaf->SetValueAt(i, leaf->ValueAt(i + 1));
        }
        leaf->SetSize(size - 1);
        curr_page->SetDirty(true);

        if (leaf->GetSize() == 0) {
            if (leaf->IsRootPage()) {
                root_page_id_ = INVALID_PAGE_ID;
                curr_page->WUnlatch();
                bpm_->UnpinPage(curr_page->GetPageId(), false);
                bpm_->DeletePage(curr_page->GetPageId());
                if (root_latched) {
                    root_latch_.unlock();
                }
            } else {
                UpdateLeafChainBeforeDeletion(curr_page->GetPageId(), leaf->GetNextPageId());
                RemoveChild(curr_page, locked_path, root_latched);
            }
        } else {
            for (auto *page : locked_path) {
                page->WUnlatch();
                bpm_->UnpinPage(page->GetPageId(), true);
            }
            if (root_latched) {
                root_latch_.unlock();
            }
        }
        return true;
    }

    void RemoveRange(int64_t key_start, int64_t key_end) {
        std::vector<int64_t> keys_to_remove;
        BPlusTreeIterator it = Begin(key_start);
        while (!it.IsEnd()) {
            auto pair = *it;
            if (pair.first > key_end) {
                break;
            }
            keys_to_remove.push_back(pair.first);
            ++it;
        }
        for (int64_t key : keys_to_remove) {
            Remove(key);
        }
    }

    BPlusTreeIterator Begin() {
        root_latch_.lock();
        if (root_page_id_ == INVALID_PAGE_ID) {
            root_latch_.unlock();
            return End();
        }
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->RLatch();
        root_latch_.unlock();

        while (true) {
            auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->ValueAt(0);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->RLatch();
            curr_page->RUnlatch();
            bpm_->UnpinPage(curr_page->GetPageId(), false);
            curr_page = child_page;
        }

        page_id_t leaf_id = curr_page->GetPageId();
        curr_page->RUnlatch();
        bpm_->UnpinPage(leaf_id, false);
        return BPlusTreeIterator(bpm_, leaf_id, 0);
    }

    BPlusTreeIterator Begin(int64_t key) {
        root_latch_.lock();
        if (root_page_id_ == INVALID_PAGE_ID) {
            root_latch_.unlock();
            return End();
        }
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->RLatch();
        root_latch_.unlock();

        while (true) {
            auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->Lookup(key);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->RLatch();
            curr_page->RUnlatch();
            bpm_->UnpinPage(curr_page->GetPageId(), false);
            curr_page = child_page;
        }

        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page->GetPayload());
        int idx = leaf->KeyIndex(key);
        page_id_t leaf_id = curr_page->GetPageId();
        page_id_t next_id = leaf->GetNextPageId();
        curr_page->RUnlatch();
        bpm_->UnpinPage(leaf_id, false);

        if (idx < leaf->GetSize()) {
            return BPlusTreeIterator(bpm_, leaf_id, idx);
        }
        return BPlusTreeIterator(bpm_, next_id, 0);
    }

    BPlusTreeIterator End() {
        return BPlusTreeIterator(bpm_, INVALID_PAGE_ID, 0);
    }

    page_id_t GetRootPageId() {
        LockGuard<Mutex> lock(root_latch_);
        return root_page_id_;
    }

private:
    enum class OpType { INSERT, REMOVE };

    bool IsSafe(BPlusTreePage *page, OpType op) {
        if (op == OpType::INSERT) {
            if (page->IsLeafPage()) {
                return page->GetSize() < page->GetMaxSize() - 1;
            }
            return page->GetSize() < page->GetMaxSize();
        } else {
            return page->GetSize() > 1;
        }
    }

    void UpdateLeafChainBeforeDeletion(page_id_t deleted_id, page_id_t next_id) {
        root_latch_.lock();
        if (root_page_id_ == INVALID_PAGE_ID) {
            root_latch_.unlock();
            return;
        }
        Page *curr_page = bpm_->FetchPage(root_page_id_);
        curr_page->RLatch();
        root_latch_.unlock();

        while (true) {
            auto *tree_page = reinterpret_cast<BPlusTreePage *>(curr_page->GetPayload());
            if (tree_page->IsLeafPage()) {
                break;
            }
            auto *internal = reinterpret_cast<BPlusTreeInternalPage *>(tree_page);
            page_id_t child_id = internal->ValueAt(0);
            Page *child_page = bpm_->FetchPage(child_id);
            child_page->RLatch();
            curr_page->RUnlatch();
            bpm_->UnpinPage(curr_page->GetPageId(), false);
            curr_page = child_page;
        }

        while (curr_page != nullptr) {
            auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page->GetPayload());
            page_id_t next_leaf_id = leaf->GetNextPageId();
            if (next_leaf_id == deleted_id) {
                curr_page->RUnlatch();
                curr_page->WLatch();
                leaf->SetNextPageId(next_id);
                curr_page->WUnlatch();
                bpm_->UnpinPage(curr_page->GetPageId(), true);
                return;
            }
            page_id_t curr_id = curr_page->GetPageId();
            curr_page->RUnlatch();
            bpm_->UnpinPage(curr_id, false);
            if (next_leaf_id == INVALID_PAGE_ID) {
                break;
            }
            curr_page = bpm_->FetchPage(next_leaf_id);
            curr_page->RLatch();
        }
    }

    void InsertIntoParent(Page *child_page, int64_t key, Page *new_child_page, std::vector<Page *> &locked_path, bool &root_latched) {
        auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetPayload());
        if (child->IsRootPage()) {
            page_id_t new_root_id;
            Page *new_root_page = bpm_->NewPage(new_root_id);
            if (new_root_page != nullptr) {
                auto *new_root = reinterpret_cast<BPlusTreeInternalPage *>(new_root_page->GetPayload());
                new_root->Init(new_root_id, INVALID_PAGE_ID, internal_max_size_);
                new_root->PopulateAll(child_page->GetPageId(), key, new_child_page->GetPageId());
                child->SetParentPageId(new_root_id);
                auto *new_child = reinterpret_cast<BPlusTreePage *>(new_child_page->GetPayload());
                new_child->SetParentPageId(new_root_id);
                root_page_id_ = new_root_id;
                bpm_->UnpinPage(new_root_id, true);
            }
            for (auto *page : locked_path) {
                page->WUnlatch();
                bpm_->UnpinPage(page->GetPageId(), true);
            }
            if (root_latched) {
                root_latch_.unlock();
                root_latched = false;
            }
            return;
        }

        int path_idx = -1;
        for (size_t i = 0; i < locked_path.size(); ++i) {
            if (locked_path[i] == child_page) {
                path_idx = i;
                break;
            }
        }
        assert(path_idx > 0);

        Page *parent_page = locked_path[path_idx - 1];
        auto *parent = reinterpret_cast<BPlusTreeInternalPage *>(parent_page->GetPayload());
        parent->InsertNodeAfter(child_page->GetPageId(), key, new_child_page->GetPageId());
        parent_page->SetDirty(true);

        if (parent->GetSize() > parent->GetMaxSize()) {
            page_id_t new_parent_id;
            Page *new_parent_page = bpm_->NewPage(new_parent_id);
            if (new_parent_page != nullptr) {
                auto *new_parent = reinterpret_cast<BPlusTreeInternalPage *>(new_parent_page->GetPayload());
                new_parent->Init(new_parent_id, parent->GetParentPageId(), internal_max_size_);
                int32_t split_index = parent->GetSize() / 2;
                int64_t parent_split_key = parent->KeyAt(split_index);
                parent->MoveHalfTo(new_parent, bpm_);
                InsertIntoParent(parent_page, parent_split_key, new_parent_page, locked_path, root_latched);
                bpm_->UnpinPage(new_parent_id, true);
            }
        } else {
            for (auto *page : locked_path) {
                page->WUnlatch();
                bpm_->UnpinPage(page->GetPageId(), true);
            }
            if (root_latched) {
                root_latch_.unlock();
                root_latched = false;
            }
        }
    }

    void RemoveChild(Page *child_page, std::vector<Page *> &locked_path, bool &root_latched) {
        int path_idx = -1;
        for (size_t i = 0; i < locked_path.size(); ++i) {
            if (locked_path[i] == child_page) {
                path_idx = i;
                break;
            }
        }
        assert(path_idx > 0);

        Page *parent_page = locked_path[path_idx - 1];
        auto *parent = reinterpret_cast<BPlusTreeInternalPage *>(parent_page->GetPayload());
        int child_idx = parent->ValueIndex(child_page->GetPageId());
        assert(child_idx != -1);

        int32_t parent_size = parent->GetSize();
        if (child_idx == 0) {
            for (int i = 0; i < parent_size - 1; ++i) {
                parent->SetValueAt(i, parent->ValueAt(i + 1));
                if (i > 0) {
                    parent->SetKeyAt(i, parent->KeyAt(i + 1));
                }
            }
        } else {
            for (int i = child_idx; i < parent_size - 1; ++i) {
                parent->SetValueAt(i, parent->ValueAt(i + 1));
                parent->SetKeyAt(i, parent->KeyAt(i + 1));
            }
        }
        parent->SetSize(parent_size - 1);
        parent_page->SetDirty(true);

        child_page->WUnlatch();
        bpm_->UnpinPage(child_page->GetPageId(), false);
        bpm_->DeletePage(child_page->GetPageId());

        if (parent->GetSize() == 0) {
            if (parent->IsRootPage()) {
                root_page_id_ = INVALID_PAGE_ID;
                parent_page->WUnlatch();
                bpm_->UnpinPage(parent_page->GetPageId(), false);
                bpm_->DeletePage(parent_page->GetPageId());
                if (root_latched) {
                    root_latch_.unlock();
                    root_latched = false;
                }
            } else {
                RemoveChild(parent_page, locked_path, root_latched);
            }
        } else if (parent->GetSize() == 1 && parent->IsRootPage()) {
            page_id_t new_root_id = parent->ValueAt(0);
            Page *child_node_page = bpm_->FetchPage(new_root_id);
            if (child_node_page != nullptr) {
                child_node_page->WLatch();
                auto *child_node = reinterpret_cast<BPlusTreePage *>(child_node_page->GetPayload());
                child_node->SetParentPageId(INVALID_PAGE_ID);
                child_node_page->WUnlatch();
                bpm_->UnpinPage(new_root_id, true);
            }
            root_page_id_ = new_root_id;
            parent_page->WUnlatch();
            bpm_->UnpinPage(parent_page->GetPageId(), false);
            bpm_->DeletePage(parent_page->GetPageId());
            if (root_latched) {
                root_latch_.unlock();
                root_latched = false;
            }
        } else {
            for (auto *page : locked_path) {
                if (page != child_page) {
                    page->WUnlatch();
                    bpm_->UnpinPage(page->GetPageId(), true);
                }
            }
            if (root_latched) {
                root_latch_.unlock();
                root_latched = false;
            }
        }
    }

    std::string index_name_;
    BufferPoolManager *bpm_;
    page_id_t root_page_id_;
    int32_t leaf_max_size_;
    int32_t internal_max_size_;
    Mutex root_latch_;
};
