#include "buffer_pool_manager.hpp"

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    pages_ = std::make_unique<Page[]>(pool_size_);
    replacer_ = std::make_unique<LRUReplacer>(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

Page *BufferPoolManager::FetchPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return nullptr;
    }
    LockGuard<Mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        frame_id_t frame_id = iter->second;
        Page *page = &pages_[frame_id];
        page->SetPinCount(page->GetPinCount() + 1);
        replacer_->Pin(frame_id);
        return page;
    }
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else if (replacer_->Victim(&frame_id)) {
        Page *victim_page = &pages_[frame_id];
        if (victim_page->IsDirty()) {
            disk_manager_->WritePage(victim_page->GetPageId(), victim_page->GetData());
            victim_page->SetDirty(false);
        }
        page_table_.erase(victim_page->GetPageId());
    }
    if (frame_id == INVALID_FRAME_ID) {
        return nullptr;
    }
    Page *page = &pages_[frame_id];
    page->ResetMemory();
    disk_manager_->ReadPage(page_id, page->GetData());
    page->SetPageId(page_id);
    page->SetPinCount(1);
    page->SetDirty(false);
    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);
    return page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }
    LockGuard<Mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    auto pin_count = page->GetPinCount();
    if (pin_count <= 0) {
        return false;
    }
    pin_count--;
    page->SetPinCount(pin_count);
    if (pin_count == 0) {
        replacer_->Unpin(frame_id);
    }
    if (is_dirty) {
        page->SetDirty(true);
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }
    LockGuard<Mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    disk_manager_->WritePage(page->GetPageId(), page->GetData());
    page->SetDirty(false);
    return true;
}

Page *BufferPoolManager::NewPage(page_id_t &page_id) {
    LockGuard<Mutex> lock(latch_);
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
    } else if (replacer_->Victim(&frame_id)) {
        Page *victim_page = &pages_[frame_id];
        if (victim_page->IsDirty()) {
            disk_manager_->WritePage(victim_page->GetPageId(), victim_page->GetData());
            victim_page->SetDirty(false);
        }
        page_table_.erase(victim_page->GetPageId());
    }
    if (frame_id == INVALID_FRAME_ID) {
        page_id = INVALID_PAGE_ID;
        return nullptr;
    }
    page_id = disk_manager_->AllocatePage();
    Page *page = &pages_[frame_id];
    page->ResetMemory();
    page->SetPageId(page_id);
    page->SetPinCount(1);
    page->SetDirty(false);
    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);
    return page;
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }
    LockGuard<Mutex> lock(latch_);
    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        disk_manager_->DeallocatePage(page_id);
        return true;
    }
    frame_id_t frame_id = iter->second;
    Page *page = &pages_[frame_id];
    if (page->GetPinCount() > 0) {
        return false;
    }
    page_table_.erase(page_id);
    replacer_->Pin(frame_id);
    page->ResetMemory();
    free_list_.push_back(frame_id);
    disk_manager_->DeallocatePage(page_id);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    LockGuard<Mutex> lock(latch_);
    for (auto const& pair : page_table_) {
        page_id_t page_id = pair.first;
        frame_id_t frame_id = pair.second;
        Page *page = &pages_[frame_id];
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
            page->SetDirty(false);
        }
    }
}
