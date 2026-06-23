#include "common.hpp"
#include "page.hpp"
#include "disk_manager.hpp"
#include "replacer.hpp"
#include "buffer_pool_manager.hpp"
#include "b_plus_tree.hpp"
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

void TestBPlusTreeSimple() {
    std::cout << "Running TestBPlusTreeSimple..." << std::endl;
    const std::string db_file = "test_tree_simple.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(10, &dm);
        BPlusTree<> tree("index1", &bpm, 3, 3);

        assert(tree.IsEmpty());

        RID r1{1, 10};
        RID r2{2, 20};
        RID r3{3, 30};

        tree.Insert(10, r1);
        tree.Insert(20, r2);
        tree.Insert(30, r3);

        assert(!tree.IsEmpty());

        std::vector<RID> res;
        assert(tree.GetValue(10, &res));
        assert(res.size() == 1);
        assert(res[0] == r1);

        res.clear();
        assert(tree.GetValue(20, &res));
        assert(res.size() == 1);
        assert(res[0] == r2);

        res.clear();
        assert(tree.GetValue(30, &res));
        assert(res.size() == 1);
        assert(res[0] == r3);

        res.clear();
        assert(!tree.GetValue(40, &res));
    }

    CleanDbFile(db_file);
    std::cout << "TestBPlusTreeSimple PASSED." << std::endl;
}

void TestBPlusTreeSplits() {
    std::cout << "Running TestBPlusTreeSplits..." << std::endl;
    const std::string db_file = "test_tree_splits.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        BPlusTree<> tree("index2", &bpm, 4, 4);

        for (int i = 1; i <= 100; ++i) {
            tree.Insert(i, RID{i, static_cast<uint32_t>(i * 10)});
        }

        for (int i = 1; i <= 100; ++i) {
            std::vector<RID> res;
            assert(tree.GetValue(i, &res));
            assert(res.size() == 1);
            assert(res[0].page_id == i);
            assert(res[0].slot_offset == static_cast<uint32_t>(i * 10));
        }

        int expected_key = 1;
        BPlusTreeIterator it = tree.Begin();
        while (!it.IsEnd()) {
            auto pair = *it;
            assert(pair.first == expected_key);
            assert(pair.second.page_id == expected_key);
            expected_key++;
            ++it;
        }
        assert(expected_key == 101);

        int start_key = 45;
        BPlusTreeIterator it2 = tree.Begin(start_key);
        while (!it2.IsEnd()) {
            auto pair = *it2;
            assert(pair.first == start_key);
            start_key++;
            ++it2;
        }
        assert(start_key == 101);
    }

    CleanDbFile(db_file);
    std::cout << "TestBPlusTreeSplits PASSED." << std::endl;
}

void TestBPlusTreeDelete() {
    std::cout << "Running TestBPlusTreeDelete..." << std::endl;
    const std::string db_file = "test_tree_delete.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(50, &dm);
        BPlusTree<> tree("index3", &bpm, 4, 4);

        for (int i = 1; i <= 20; ++i) {
            tree.Insert(i, RID{i, static_cast<uint32_t>(i * 10)});
        }

        for (int i = 2; i <= 20; i += 2) {
            assert(tree.Remove(i));
        }

        for (int i = 1; i <= 20; ++i) {
            std::vector<RID> res;
            bool found = tree.GetValue(i, &res);
            if (i % 2 == 0) {
                assert(!found);
            } else {
                assert(found);
                assert(res[0].page_id == i);
            }
        }

        tree.RemoveRange(5, 15);

        std::vector<int64_t> remaining_keys;
        BPlusTreeIterator it = tree.Begin();
        while (!it.IsEnd()) {
            remaining_keys.push_back((*it).first);
            ++it;
        }

        assert(remaining_keys.size() == 4);
        assert(remaining_keys[0] == 1);
        assert(remaining_keys[1] == 3);
        assert(remaining_keys[2] == 17);
        assert(remaining_keys[3] == 19);
    }

    CleanDbFile(db_file);
    std::cout << "TestBPlusTreeDelete PASSED." << std::endl;
}

void TestBPlusTreeConcurrent() {
    std::cout << "Running TestBPlusTreeConcurrent..." << std::endl;
    const std::string db_file = "test_tree_concurrent.db";
    CleanDbFile(db_file);

    {
        DiskManager dm(db_file);
        BufferPoolManager bpm(100, &dm);
        BPlusTree<> tree("index_concurrent", &bpm, 8, 8);

        auto inserter = [&](int start_val, int count) {
            for (int i = 0; i < count; ++i) {
                int val = start_val + i;
                tree.Insert(val, RID{val, 100});
            }
        };

        std::vector<Thread> threads;
        threads.push_back(Thread([&]() { inserter(1, 100); }));
        threads.push_back(Thread([&]() { inserter(101, 100); }));
        threads.push_back(Thread([&]() { inserter(201, 100); }));
        threads.push_back(Thread([&]() { inserter(301, 100); }));

        for (size_t i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }

        for (int i = 1; i <= 400; ++i) {
            std::vector<RID> res;
            assert(tree.GetValue(i, &res));
            assert(res.size() == 1);
            assert(res[0].page_id == i);
        }
    }

    CleanDbFile(db_file);
    std::cout << "TestBPlusTreeConcurrent PASSED." << std::endl;
}

int main() {
    try {
        TestBPlusTreeSimple();
        TestBPlusTreeSplits();
        TestBPlusTreeDelete();
        TestBPlusTreeConcurrent();
        std::cout << "\nALL B+ TREE TESTS PASSED SUCCESSFULLY!" << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
