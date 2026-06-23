#pragma once

#include "common.hpp"
#include "page.hpp"
#include "buffer_pool_manager.hpp"
#include "b_plus_tree_leaf_page.hpp"
#include <utility>
#include <cassert>

class BPlusTreeIterator {
public:
    BPlusTreeIterator(BufferPoolManager *bpm, page_id_t page_id, int index)
        : bpm_(bpm), curr_page_id_(page_id), curr_index_(index), curr_page_(nullptr) {
        if (curr_page_id_ != INVALID_PAGE_ID) {
            curr_page_ = bpm_->FetchPage(curr_page_id_);
            if (curr_page_ != nullptr) {
                curr_page_->RLatch();
            }
        }
    }

    BPlusTreeIterator(const BPlusTreeIterator &other)
        : bpm_(other.bpm_), curr_page_id_(other.curr_page_id_), curr_index_(other.curr_index_), curr_page_(nullptr) {
        if (curr_page_id_ != INVALID_PAGE_ID) {
            curr_page_ = bpm_->FetchPage(curr_page_id_);
            if (curr_page_ != nullptr) {
                curr_page_->RLatch();
            }
        }
    }

    BPlusTreeIterator& operator=(const BPlusTreeIterator &other) {
        if (this != &other) {
            Release();
            bpm_ = other.bpm_;
            curr_page_id_ = other.curr_page_id_;
            curr_index_ = other.curr_index_;
            if (curr_page_id_ != INVALID_PAGE_ID) {
                curr_page_ = bpm_->FetchPage(curr_page_id_);
                if (curr_page_ != nullptr) {
                    curr_page_->RLatch();
                }
            } else {
                curr_page_ = nullptr;
            }
        }
        return *this;
    }

    BPlusTreeIterator(BPlusTreeIterator &&other) noexcept
        : bpm_(other.bpm_), curr_page_id_(other.curr_page_id_), curr_index_(other.curr_index_), curr_page_(other.curr_page_) {
        other.curr_page_ = nullptr;
        other.curr_page_id_ = INVALID_PAGE_ID;
    }

    BPlusTreeIterator& operator=(BPlusTreeIterator &&other) noexcept {
        if (this != &other) {
            Release();
            bpm_ = other.bpm_;
            curr_page_id_ = other.curr_page_id_;
            curr_index_ = other.curr_index_;
            curr_page_ = other.curr_page_;
            other.curr_page_ = nullptr;
            other.curr_page_id_ = INVALID_PAGE_ID;
        }
        return *this;
    }

    ~BPlusTreeIterator() {
        Release();
    }

    bool IsEnd() const {
        return curr_page_id_ == INVALID_PAGE_ID;
    }

    std::pair<int64_t, RID> operator*() const {
        assert(curr_page_ != nullptr);
        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page_->GetPayload());
        return {leaf->KeyAt(curr_index_), leaf->ValueAt(curr_index_)};
    }

    BPlusTreeIterator& operator++() {
        if (curr_page_id_ == INVALID_PAGE_ID) {
            return *this;
        }
        auto *leaf = reinterpret_cast<BPlusTreeLeafPage *>(curr_page_->GetPayload());
        curr_index_++;
        if (curr_index_ >= leaf->GetSize()) {
            page_id_t next_page_id = leaf->GetNextPageId();
            Release();
            curr_page_id_ = next_page_id;
            curr_index_ = 0;
            if (curr_page_id_ != INVALID_PAGE_ID) {
                curr_page_ = bpm_->FetchPage(curr_page_id_);
                if (curr_page_ != nullptr) {
                    curr_page_->RLatch();
                }
            }
        }
        return *this;
    }

    bool operator==(const BPlusTreeIterator &other) const {
        return curr_page_id_ == other.curr_page_id_ && curr_index_ == other.curr_index_;
    }

    bool operator!=(const BPlusTreeIterator &other) const {
        return !(*this == other);
    }

private:
    void Release() {
        if (curr_page_ != nullptr) {
            curr_page_->RUnlatch();
            bpm_->UnpinPage(curr_page_id_, false);
            curr_page_ = nullptr;
            curr_page_id_ = INVALID_PAGE_ID;
        }
    }

    BufferPoolManager *bpm_;
    page_id_t curr_page_id_;
    int32_t curr_index_;
    Page *curr_page_;
};
