#pragma once

#include "common.hpp"
#include "page.hpp"
#include "disk_manager.hpp"
#include "replacer.hpp"
#include <unordered_map>
#include <list>
#include <memory>

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
    ~BufferPoolManager();

    Page *FetchPage(page_id_t page_id);
    bool UnpinPage(page_id_t page_id, bool is_dirty);
    bool FlushPage(page_id_t page_id);
    Page *NewPage(page_id_t &page_id);
    bool DeletePage(page_id_t page_id);
    void FlushAllPages();

    size_t GetPoolSize() const { return pool_size_; }
    Page *GetPages() { return pages_.get(); }

private:
    size_t pool_size_;
    DiskManager *disk_manager_;
    std::unique_ptr<Page[]> pages_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::unique_ptr<LRUReplacer> replacer_;
    std::list<frame_id_t> free_list_;
    Mutex latch_;
};
