#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace privacy {
constexpr UINT Changed = WM_APP + 70;
enum class Result { Pending, Allowed, Blocked, Succeeded, Failed };
struct Event {
    uint64_t id = 0;
    FILETIME time{};
    std::wstring owner, capability, target, reason;
    Result result = Result::Pending;
};
struct Resource { std::wstring owner, capability, reason; };
struct Snapshot {
    bool offline = false;
    size_t requests = 0;
    uint64_t omitted = 0;
    std::vector<Event> events;
    std::vector<Resource> active;
};
class Runtime {
    mutable std::mutex mutex_;
    std::deque<Event> events_;
    std::map<std::pair<std::wstring, uintptr_t>, Resource> active_;
    uint64_t next_ = 0, omitted_ = 0;
    size_t requests_ = 0;
    std::atomic<bool> offline_{false};
    std::atomic<uint64_t> generation_{0};
    std::atomic<HWND> observer_{nullptr};
    std::atomic<bool> pending_{false};
    void Notify() {
        HWND window = observer_.load();
        if (window && !pending_.exchange(true) && !PostMessageW(window, Changed, 0, 0)) pending_ = false;
    }
    uint64_t Append(std::wstring owner, std::wstring capability, std::wstring target,
                    std::wstring reason, Result result) {
        Event item{++next_, {}, std::move(owner), std::move(capability), std::move(target),
                   std::move(reason), result};
        GetSystemTimeAsFileTime(&item.time);
        if (events_.size() == 128) { events_.pop_front(); ++omitted_; }
        events_.push_back(std::move(item));
        return next_;
    }
public:
    uint64_t Reserve() { std::scoped_lock lock(mutex_); return ++next_; }
    void Observe(HWND window) { observer_ = window; pending_ = false; }
    void Acknowledge() { pending_ = false; }
    bool Offline() const { return offline_.load(); }
    uint64_t Generation() const { return generation_.load(); }
    void SetOffline(bool value) {
        {
            std::scoped_lock lock(mutex_);
            if (offline_.exchange(value) == value) return;
            ++generation_;
        }
        Notify();
    }
    uint64_t Begin(std::wstring owner, std::wstring capability, std::wstring target, std::wstring reason) {
        uint64_t id;
        {
            std::scoped_lock lock(mutex_);
            id = Append(std::move(owner), std::move(capability), std::move(target), std::move(reason), Result::Pending);
        }
        Notify();
        return id;
    }
    void Finish(uint64_t id, bool success) {
        {
            std::scoped_lock lock(mutex_);
            for (auto &item : events_)
                if (item.id == id) { item.result = success ? Result::Succeeded : Result::Failed; break; }
        }
        Notify();
    }
    // Returns zero while offline. Targets are host names only, never request URLs or bodies.
    uint64_t BeginNetwork(const std::wstring &host, const std::wstring &reason, uint64_t &generation) {
        uint64_t id = 0;
        {
            std::scoped_lock lock(mutex_);
            generation = generation_;
            const bool blocked = offline_;
            auto event = Append(L"today", L"network", host, reason, blocked ? Result::Blocked : Result::Pending);
            if (!blocked) { ++requests_; id = event; }
        }
        Notify();
        return id;
    }
    void EndNetwork(uint64_t id, bool success, bool cancelled = false) {
        if (!id) return;
        {
            std::scoped_lock lock(mutex_);
            if (requests_) --requests_;
            for (auto &item : events_)
                if (item.id == id) {
                    item.result = cancelled ? Result::Blocked : success ? Result::Succeeded : Result::Failed;
                    break;
                }
        }
        Notify();
    }
    void Watch(const std::wstring &kind, uintptr_t handle, std::wstring owner, std::wstring reason) {
        if (!handle) return;
        {
            std::scoped_lock lock(mutex_);
            active_[{kind, handle}] = {std::move(owner), kind, std::move(reason)};
        }
        Notify();
    }
    void Unwatch(const std::wstring &kind, uintptr_t handle) {
        {
            std::scoped_lock lock(mutex_);
            active_.erase({kind, handle});
        }
        Notify();
    }
    Snapshot Read() const {
        std::scoped_lock lock(mutex_);
        Snapshot result{offline_, requests_, omitted_, {events_.begin(), events_.end()}, {}};
        for (const auto &[key, resource] : active_) { (void)key; result.active.push_back(resource); }
        return result;
    }
};
inline Runtime &Policy() { static Runtime value; return value; }
class Write {
    uint64_t id_;
    bool ok_ = false;
public:
    Write(const std::wstring &owner, const std::wstring &kind, const std::wstring &target,
          const std::wstring &reason) : id_(Policy().Begin(owner, kind, target, reason)) {}
    ~Write() { Policy().Finish(id_, ok_); }
    Write(const Write &) = delete;
    Write &operator=(const Write &) = delete;
    bool Complete(bool ok) { ok_ = ok; return ok; }
};
class Network {
    uint64_t id_ = 0, generation_ = 0;
    bool success_ = false;
public:
    Network(const std::wstring &host, const std::wstring &reason) {
        id_ = Policy().BeginNetwork(host, reason, generation_);
    }
    ~Network() { Policy().EndNetwork(id_, success_, !Allowed()); }
    Network(const Network &) = delete;
    Network &operator=(const Network &) = delete;
    bool Allowed() const { return id_ && !Policy().Offline() && generation_ == Policy().Generation(); }
    void Succeeded() { success_ = true; }
};
}
