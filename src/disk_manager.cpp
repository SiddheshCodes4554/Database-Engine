#include "disk_manager.hpp"
#include <cstring>

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file) {
    db_io_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
    if (!db_io_.is_open()) {
        db_io_.clear();
        db_io_.open(db_file, std::ios::out | std::ios::binary | std::ios::trunc);
        db_io_.close();
        db_io_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
        if (!db_io_.is_open()) {
            throw DiskException("Failed to open/create database file: " + db_file);
        }
    }
    db_io_.seekp(0, std::ios::end);
    auto file_size = db_io_.tellp();
    if (file_size == 0) {
        std::memcpy(header_.magic, "DBMS", 4);
        header_.num_pages = 0;
        header_.free_list_size = 0;
        std::memset(header_.free_list, 0, sizeof(header_.free_list));
        WriteHeader();
    } else {
        ReadHeader();
    }
}

DiskManager::~DiskManager() {
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

void DiskManager::ReadHeader() {
    db_io_.seekg(0, std::ios::beg);
    db_io_.read(reinterpret_cast<char*>(&header_), sizeof(DiskHeader));
    if (db_io_.gcount() != sizeof(DiskHeader)) {
        throw DiskException("Failed to read database header page.");
    }
    if (std::memcmp(header_.magic, "DBMS", 4) != 0) {
        throw DiskException("Database header magic mismatch. File is not a valid database.");
    }
}

void DiskManager::WriteHeader() {
    db_io_.seekp(0, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(&header_), sizeof(DiskHeader));
    db_io_.flush();
    if (db_io_.bad()) {
        throw DiskException("Failed to write database header page.");
    }
}

page_id_t DiskManager::AllocatePage() {
    LockGuard<Mutex> guard(db_io_latch_);
    page_id_t allocated_id;
    if (header_.free_list_size > 0) {
        header_.free_list_size--;
        allocated_id = header_.free_list[header_.free_list_size];
        header_.free_list[header_.free_list_size] = 0;
        WriteHeader();
    } else {
        header_.num_pages++;
        allocated_id = header_.num_pages;
        WriteHeader();
        char empty_page[PAGE_SIZE];
        std::memset(empty_page, 0, PAGE_SIZE);
        std::memcpy(empty_page, &allocated_id, sizeof(page_id_t));
        db_io_.seekp(allocated_id * PAGE_SIZE, std::ios::beg);
        db_io_.write(empty_page, PAGE_SIZE);
        db_io_.flush();
        if (db_io_.bad()) {
            throw DiskException("Failed to initialize allocated page on disk.");
        }
    }
    return allocated_id;
}

void DiskManager::DeallocatePage(page_id_t page_id) {
    LockGuard<Mutex> guard(db_io_latch_);
    if (page_id <= 0 || page_id > header_.num_pages) {
        throw DiskException("Invalid page ID for deallocation: " + std::to_string(page_id));
    }
    for (int i = 0; i < header_.free_list_size; ++i) {
        if (header_.free_list[i] == page_id) {
            return;
        }
    }
    if (header_.free_list_size < 1021) {
        header_.free_list[header_.free_list_size] = page_id;
        header_.free_list_size++;
        WriteHeader();
        char empty_page[PAGE_SIZE];
        std::memset(empty_page, 0, PAGE_SIZE);
        db_io_.seekp(page_id * PAGE_SIZE, std::ios::beg);
        db_io_.write(empty_page, PAGE_SIZE);
        db_io_.flush();
    } else {
        throw DiskException("Free list overflow. Cannot deallocate page " + std::to_string(page_id));
    }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    LockGuard<Mutex> guard(db_io_latch_);
    if (page_id <= 0 || page_id > header_.num_pages) {
        throw DiskException("WritePage: Page ID " + std::to_string(page_id) + " out of bounds.");
    }
    db_io_.seekp(page_id * PAGE_SIZE, std::ios::beg);
    db_io_.write(page_data, PAGE_SIZE);
    db_io_.flush();
    if (db_io_.bad()) {
        throw DiskException("WritePage: Failed to write page " + std::to_string(page_id) + " to disk.");
    }
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    LockGuard<Mutex> guard(db_io_latch_);
    if (page_id <= 0 || page_id > header_.num_pages) {
        throw DiskException("ReadPage: Page ID " + std::to_string(page_id) + " out of bounds.");
    }
    db_io_.seekg(page_id * PAGE_SIZE, std::ios::beg);
    db_io_.read(page_data, PAGE_SIZE);
    if (db_io_.gcount() != PAGE_SIZE) {
        throw DiskException("ReadPage: Failed to read page " + std::to_string(page_id) + " from disk.");
    }
}

int32_t DiskManager::GetNumPages() {
    LockGuard<Mutex> guard(db_io_latch_);
    return header_.num_pages;
}
