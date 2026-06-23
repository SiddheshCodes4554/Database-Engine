#pragma once

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using page_id_t = int32_t;
using frame_id_t = int32_t;
using lsn_t = int32_t;

constexpr size_t PAGE_SIZE = 4096;
constexpr page_id_t INVALID_PAGE_ID = -1;
constexpr frame_id_t INVALID_FRAME_ID = -1;

class DiskException : public std::runtime_error {
public:
    explicit DiskException(const std::string &message) : std::runtime_error("Disk Error: " + message) {}
};

class BufferPoolException : public std::runtime_error {
public:
    explicit BufferPoolException(const std::string &message) : std::runtime_error("Buffer Pool Error: " + message) {}
};

#ifdef _WIN32
class Mutex {
public:
    Mutex() { InitializeCriticalSection(&cs_); }
    ~Mutex() { DeleteCriticalSection(&cs_); }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
};

class SharedMutex {
public:
    SharedMutex() : readers_(0), write_locked_(false) {
        InitializeCriticalSection(&cs_);
    }
    ~SharedMutex() {
        DeleteCriticalSection(&cs_);
    }
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    void lock() {
        while (true) {
            EnterCriticalSection(&cs_);
            if (readers_ == 0 && !write_locked_) {
                write_locked_ = true;
                LeaveCriticalSection(&cs_);
                break;
            }
            LeaveCriticalSection(&cs_);
            Sleep(1);
        }
    }

    void unlock() {
        EnterCriticalSection(&cs_);
        write_locked_ = false;
        LeaveCriticalSection(&cs_);
    }

    void lock_shared() {
        while (true) {
            EnterCriticalSection(&cs_);
            if (!write_locked_) {
                readers_++;
                LeaveCriticalSection(&cs_);
                break;
            }
            LeaveCriticalSection(&cs_);
            Sleep(1);
        }
    }

    void unlock_shared() {
        EnterCriticalSection(&cs_);
        if (readers_ > 0) {
            readers_--;
        }
        LeaveCriticalSection(&cs_);
    }

private:
    CRITICAL_SECTION cs_;
    int32_t readers_;
    bool write_locked_;
};
#else
#include <mutex>
#include <shared_mutex>
using Mutex = std::mutex;
using SharedMutex = std::shared_mutex;
#endif

template<typename T>
class LockGuard {
public:
    explicit LockGuard(T &mutex) : mutex_(mutex) { mutex_.lock(); }
    ~LockGuard() { mutex_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    T &mutex_;
};
