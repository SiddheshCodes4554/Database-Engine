#pragma once

#include "common.hpp"
#include <string>
#include <fstream>

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    void WritePage(page_id_t page_id, const char *page_data);
    void ReadPage(page_id_t page_id, char *page_data);

    page_id_t AllocatePage();
    void DeallocatePage(page_id_t page_id);

    int32_t GetNumPages();

private:
    void ReadHeader();
    void WriteHeader();

    std::string file_name_;
    std::fstream db_io_;
    Mutex db_io_latch_;

    struct DiskHeader {
        char magic[4];
        int32_t num_pages;
        int32_t free_list_size;
        page_id_t free_list[1021];
    } header_;
};
