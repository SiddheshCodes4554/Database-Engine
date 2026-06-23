#include "page.hpp"
#include <cstring>

Page::Page() {
    ResetMemory();
}

page_id_t Page::GetPageId() const {
    page_id_t page_id;
    std::memcpy(&page_id, data_, sizeof(page_id_t));
    return page_id;
}

void Page::SetPageId(page_id_t page_id) {
    std::memcpy(data_, &page_id, sizeof(page_id_t));
}

lsn_t Page::GetLSN() const {
    lsn_t lsn;
    std::memcpy(&lsn, data_ + sizeof(page_id_t), sizeof(lsn_t));
    return lsn;
}

void Page::SetLSN(lsn_t lsn) {
    std::memcpy(data_ + sizeof(page_id_t), &lsn, sizeof(lsn_t));
}

int32_t Page::GetPinCount() const {
    int32_t pin_count;
    std::memcpy(&pin_count, data_ + sizeof(page_id_t) + sizeof(lsn_t), sizeof(int32_t));
    return pin_count;
}

void Page::SetPinCount(int32_t pin_count) {
    std::memcpy(data_ + sizeof(page_id_t) + sizeof(lsn_t), &pin_count, sizeof(int32_t));
}

void Page::RLatch() {
    rwlatch_.lock_shared();
}

void Page::RUnlatch() {
    rwlatch_.unlock_shared();
}

void Page::WLatch() {
    rwlatch_.lock();
}

void Page::WUnlatch() {
    rwlatch_.unlock();
}

char *Page::GetData() {
    return data_;
}

const char *Page::GetData() const {
    return data_;
}

char *Page::GetPayload() {
    return data_ + HEADER_SIZE;
}

const char *Page::GetPayload() const {
    return data_ + HEADER_SIZE;
}

bool Page::IsDirty() const {
    return is_dirty_;
}

void Page::SetDirty(bool is_dirty) {
    is_dirty_ = is_dirty;
}

void Page::ResetMemory() {
    std::memset(data_, 0, PAGE_SIZE);
    SetPageId(INVALID_PAGE_ID);
    SetLSN(0);
    SetPinCount(0);
    is_dirty_ = false;
}
