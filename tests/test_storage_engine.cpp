#include "common.hpp"
#include "page.hpp"
#include "disk_manager.hpp"
#include "replacer.hpp"
#include "buffer_pool_manager.hpp"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <memory>
#include <cstdio>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
struct ThreadArgs {
    std::function<void()> func;
};

DWORD WINAPI ThreadProc(LPVOID lpParam) {
    auto args = std::unique_ptr<ThreadArgs>(static_cast<ThreadArgs*>(lpParam));
    args->func();
    return 0;
}

class Thread {
public:
    explicit Thread(std::function<void()> func) {
        auto args = new ThreadArgs{func};
        handle_ = CreateThread(nullptr, 0, ThreadProc, args, 0, nullptr);
    }
    ~Thread() {
        if (handle_) {
            CloseHandle(handle_);
        }
    }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (handle_) CloseHandle(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void join() {
        if (handle_) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }
private:
    HANDLE handle_{nullptr};
};
#else
#include <thread>
using Thread = std::thread;
#endif

void CleanDbFile(const std::string &db_file) {
    std::remove(db_file.c_str());
}

void TestPage() {
    std::cout << "Running TestPage..." << std::endl;
    Page page;
    page.ResetMemory();
    assert(page.GetPageId() == INVALID_PAGE_ID);
    assert(page.GetLSN() == 0);
    assert(page.GetPinCount() == 0);
    assert(!page.IsDirty());

    page.SetPageId(42);
    page.SetLSN(100);
    page.SetPinCount(3);
    assert(page.GetPageId() == 42);
    assert(page.GetLSN() == 100);
    assert(page.GetPinCount() == 3);

    page_id_t pid_layout;
    std::memcpy(&pid_layout, page.GetData(), sizeof(page_id_t));
    assert(pid_layout == 42);

    lsn_t lsn_layout;
    std::memcpy(&lsn_layout, page.GetData() + sizeof(page_id_t), sizeof(lsn_t));
    assert(lsn_layout == 100);

    int32_t pin_layout;
    std::memcpy(&pin_layout, page.GetData() + sizeof(page_id_t) + sizeof(lsn_t), sizeof(int32_t));
    assert(pin_layout == 3);

    const char *payload = "TestData";
    std::memcpy(page.GetPayload(), payload, strlen(payload) + 1);
    assert(std::strcmp(page.GetPayload(), "TestData") == 0);

    std::cout << "TestPage PASSED." << std::endl;
}

void TestDiskManager() {
    std::cout << "Running TestDiskManager..." << std::endl;
    const std::string db_file = "test_dm.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        assert(dm.GetNumPages() == 0);

        page_id_t p1 = dm.AllocatePage();
        page_id_t p2 = dm.AllocatePage();
        page_id_t p3 = dm.AllocatePage();

        assert(p1 == 1);
        assert(p2 == 2);
        assert(p3 == 3);
        assert(dm.GetNumPages() == 3);

        char data[PAGE_SIZE];
        std::memset(data, 0, PAGE_SIZE);
        std::memcpy(data + Page::HEADER_SIZE, "Page 1 contents", 16);
        dm.WritePage(p1, data);

        std::memset(data, 0, PAGE_SIZE);
        std::memcpy(data + Page::HEADER_SIZE, "Page 2 contents", 16);
        dm.WritePage(p2, data);

        dm.DeallocatePage(p2);
        
        page_id_t p2_reuse = dm.AllocatePage();
        assert(p2_reuse == 2);
    }

    {
        DiskManager dm(db_file);
        assert(dm.GetNumPages() == 3);

        char data[PAGE_SIZE];
        dm.ReadPage(1, data);
        assert(std::strcmp(data + Page::HEADER_SIZE, "Page 1 contents") == 0);
    }

    CleanDbFile(db_file);
    std::cout << "TestDiskManager PASSED." << std::endl;
}

void TestReplacer() {
    std::cout << "Running TestReplacer..." << std::endl;
    LRUReplacer replacer(3);
    frame_id_t victim;

    assert(!replacer.Victim(&victim));

    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    assert(replacer.Size() == 3);

    replacer.Pin(2);
    assert(replacer.Size() == 2);

    assert(replacer.Victim(&victim));
    assert(victim == 1);
    assert(replacer.Size() == 1);

    assert(replacer.Victim(&victim));
    assert(victim == 3);
    assert(replacer.Size() == 0);

    std::cout << "TestReplacer PASSED." << std::endl;
}

void TestBufferPoolManager() {
    std::cout << "Running TestBufferPoolManager..." << std::endl;
    const std::string db_file = "test_bpm.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(3, &dm);

        page_id_t p1, p2, p3, p4;
        Page *page1 = bpm.NewPage(p1);
        Page *page2 = bpm.NewPage(p2);
        Page *page3 = bpm.NewPage(p3);

        assert(page1 != nullptr);
        assert(page2 != nullptr);
        assert(page3 != nullptr);
        assert(p1 == 1);
        assert(p2 == 2);
        assert(p3 == 3);

        std::strcpy(page1->GetPayload(), "Payload1");
        std::strcpy(page2->GetPayload(), "Payload2");
        std::strcpy(page3->GetPayload(), "Payload3");

        assert(bpm.UnpinPage(p1, true));
        assert(bpm.UnpinPage(p2, false));
        assert(bpm.UnpinPage(p3, true));

        Page *p1_fetched = bpm.FetchPage(p1);
        assert(p1_fetched == page1);
        assert(p1_fetched->GetPinCount() == 1);
        assert(bpm.UnpinPage(p1, false));

        Page *page4 = bpm.NewPage(p4);
        assert(page4 != nullptr);
        assert(p4 == 4);

        Page *p2_fetched = bpm.FetchPage(p2);
        assert(p2_fetched != nullptr);
        assert(p2_fetched->GetPageId() == p2);
        assert(bpm.UnpinPage(p2, false));

        bpm.FlushAllPages();
    }

    {
        DiskManager dm(db_file);
        char buffer[PAGE_SIZE];
        dm.ReadPage(1, buffer);
        assert(std::strcmp(buffer + Page::HEADER_SIZE, "Payload1") == 0);
        dm.ReadPage(3, buffer);
        assert(std::strcmp(buffer + Page::HEADER_SIZE, "Payload3") == 0);
    }

    CleanDbFile(db_file);
    std::cout << "TestBufferPoolManager PASSED." << std::endl;
}

void TestConcurrentBPM() {
    std::cout << "Running TestConcurrentBPM..." << std::endl;
    const std::string db_file = "test_concurrent.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);

        std::vector<page_id_t> pages;
        for (int i = 0; i < 15; ++i) {
            page_id_t pid;
            Page *page = bpm.NewPage(pid);
            assert(page != nullptr);
            std::sprintf(page->GetPayload(), "Data %d", pid);
            pages.push_back(pid);
            bpm.UnpinPage(pid, true);
        }

        auto worker = [&](int thread_id) {
            for (int i = 0; i < 100; ++i) {
                page_id_t pid = pages[(thread_id + i) % pages.size()];
                Page *page = bpm.FetchPage(pid);
                if (page != nullptr) {
                    page->WLatch();
                    char temp[128];
                    std::sprintf(temp, "Data %d Thread %d", pid, thread_id);
                    std::strcpy(page->GetPayload(), temp);
                    page->WUnlatch();
                    bpm.UnpinPage(pid, true);
                }
            }
        };

        std::vector<Thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.push_back(Thread([worker, i]() { worker(i); }));
        }

        for (size_t i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }

        bpm.FlushAllPages();
    }

    CleanDbFile(db_file);
    std::cout << "TestConcurrentBPM PASSED." << std::endl;
}

int main() {
    try {
        TestPage();
        TestDiskManager();
        TestReplacer();
        TestBufferPoolManager();
        TestConcurrentBPM();
        std::cout << "\nALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
