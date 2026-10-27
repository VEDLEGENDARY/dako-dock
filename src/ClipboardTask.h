#pragma once
#include "ClipboardTransfer.h"
#include "ModuleRegistry.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace clipboard {
// A single bounded transfer. UI owners consume completion before starting another.
class Task {
    HANDLE cancel_ = nullptr;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::optional<Reply> reply_;
    std::atomic<size_t> bytes_ = 0;
    std::atomic<uint64_t> cpu_ = 0;
    TransferActivity activity_;
public:
    Task() = default;
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    ~Task() { Stop(); }
    bool Busy() const { return worker_.joinable(); }
    size_t Bytes() const { return bytes_.load() + activity_.CommittedBytes(); }
    uint64_t Cpu() const { return cpu_.load() + activity_.Cpu(); }
    void Cancel() { if (cancel_) SetEvent(cancel_); }
    bool Start(TransferOp operation, Payload payload, HWND window, const std::wstring &owner,
               DWORD timeout = 1000, DWORD delay = 0) {
        if (Busy()) return false;
        cancel_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!cancel_) return false;
        bytes_ = TransferLimit + payload.bytes.capacity();
        try {
            worker_ = std::thread([this, operation, payload = std::move(payload), window, owner, timeout, delay] {
                SetThreadDescription(GetCurrentThread(), L"Dako.Module.ClipboardTransfer");
                Reply result;
                try {
                    if (delay && WaitForSingleObject(cancel_, delay) == WAIT_OBJECT_0) result.status = TransferStatus::Cancelled;
                    else result = Transfer(operation, payload, cancel_, timeout, owner, &activity_);
                } catch (...) {}
                FILETIME created{}, exit{}, kernel{}, user{};
                ULARGE_INTEGER k{}, u{};
                if (GetThreadTimes(GetCurrentThread(), &created, &exit, &kernel, &user)) {
                    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
                    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
                }
                cpu_.fetch_add(k.QuadPart + u.QuadPart);
                {
                    std::lock_guard lock(mutex_);
                    reply_ = std::move(result);
                }
                if (window) PostMessageW(window, modules::Changed, 0, 0);
            });
        } catch (...) {
            CloseHandle(cancel_); cancel_ = nullptr; bytes_ = 0; return false;
        }
        return true;
    }
    std::optional<Reply> Consume() {
        std::optional<Reply> result;
        {
            std::lock_guard lock(mutex_);
            if (!reply_) return {};
            result = std::move(reply_);
            reply_.reset();
        }
        worker_.join();
        CloseHandle(cancel_); cancel_ = nullptr; bytes_ = 0;
        return result;
    }
    void Stop() noexcept {
        if (cancel_) SetEvent(cancel_);
        if (worker_.joinable()) worker_.join();
        if (cancel_) CloseHandle(cancel_);
        cancel_ = nullptr;
        std::lock_guard lock(mutex_);
        reply_.reset(); bytes_ = 0;
    }
};
}
