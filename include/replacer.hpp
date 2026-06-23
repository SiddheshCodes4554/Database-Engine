#pragma once

#include "common.hpp"
#include <list>
#include <unordered_map>

class LRUReplacer {
public:
    explicit LRUReplacer(size_t num_pages);
    ~LRUReplacer() = default;

    bool Victim(frame_id_t *frame_id);
    void Pin(frame_id_t frame_id);
    void Unpin(frame_id_t frame_id);
    size_t Size();

private:
    Mutex latch_;
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    size_t capacity_;
};
