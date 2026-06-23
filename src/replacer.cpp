#include "replacer.hpp"

LRUReplacer::LRUReplacer(size_t num_pages) : capacity_(num_pages) {}

bool LRUReplacer::Victim(frame_id_t *frame_id) {
    LockGuard<Mutex> lock(latch_);
    if (lru_list_.empty()) {
        return false;
    }
    *frame_id = lru_list_.back();
    lru_map_.erase(*frame_id);
    lru_list_.pop_back();
    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    LockGuard<Mutex> lock(latch_);
    auto iter = lru_map_.find(frame_id);
    if (iter != lru_map_.end()) {
        lru_list_.erase(iter->second);
        lru_map_.erase(iter);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    LockGuard<Mutex> lock(latch_);
    if (lru_map_.find(frame_id) == lru_map_.end()) {
        lru_list_.push_front(frame_id);
        lru_map_[frame_id] = lru_list_.begin();
    }
}

size_t LRUReplacer::Size() {
    LockGuard<Mutex> lock(latch_);
    return lru_list_.size();
}
