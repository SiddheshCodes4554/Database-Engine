#pragma once

#include "common.hpp"

class Page {
public:
    Page();
    ~Page() = default;

    page_id_t GetPageId() const;
    void SetPageId(page_id_t page_id);

    lsn_t GetLSN() const;
    void SetLSN(lsn_t lsn);

    int32_t GetPinCount() const;
    void SetPinCount(int32_t pin_count);

    void RLatch();
    void RUnlatch();
    void WLatch();
    void WUnlatch();

    char *GetData();
    const char *GetData() const;

    char *GetPayload();
    const char *GetPayload() const;

    bool IsDirty() const;
    void SetDirty(bool is_dirty);

    void ResetMemory();

    static constexpr size_t HEADER_SIZE = sizeof(page_id_t) + sizeof(lsn_t) + sizeof(int32_t);
    static constexpr size_t PAYLOAD_SIZE = PAGE_SIZE - HEADER_SIZE;

private:
    char data_[PAGE_SIZE];
    bool is_dirty_{false};
    SharedMutex rwlatch_;
};
