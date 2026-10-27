#pragma once
#include "AwakeRuleModel.h"
#include <atomic>
#include <array>

namespace awake {
enum class State { Stopped, Starting, Waiting, Active, Failed };
// A worker owns every native registration and its execution request. No UI-thread polling timer.
class Worker {
    Rules rules_;
    HANDLE thread_ = nullptr, stop_ = nullptr;
    HWND owner_ = nullptr;
    std::atomic<HWND> window_ = nullptr;
    std::atomic<State> state_ = State::Stopped;
    std::atomic<uint64_t> hookTicks_ = 0, callbacks_ = 0;
    std::atomic<bool> slow_ = false;
    std::atomic<unsigned> resources_ = 0;
    uint64_t releasedCpu_ = 0;
    LONGLONG frequency_ = 1;
    bool check_ = false, requested_ = false, suspended_ = false, changed_ = true, failed_ = false;
    EXECUTION_STATE previous_ = 0;
    Fact ac_ = Fact::Unknown, external_ = Fact::Unknown;
    std::array<HANDLE, 8> processes_{};
    static thread_local Worker *current_;
    static DWORD WINAPI Run(void *);
    static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK Event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void Loop();
    void ObserveProcess(DWORD);
    void ScanProcesses();
    void ReadPower();
    void ReadDisplays();
    bool Apply(bool);
    void Status(State);
public:
    Worker() = default;
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;
    ~Worker() { Stop(); }
    bool Start(const Rules &, HWND owner, bool resourceCheck = false);
    void Stop() noexcept;
    bool Running() const { return thread_ != nullptr; }
    bool Finished() const { return thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0; }
    State Status() const { return state_.load(); }
    uint64_t Cpu() const noexcept;
    double HookMicroseconds() const noexcept;
    size_t Bytes() const noexcept;
    // Checks send synthetic facts through the real worker message loop; they never request power state.
    bool CheckFacts(Fact, Fact, Fact);
    unsigned Resources() const { return resources_.load(); }
    uint64_t Callbacks() const { return callbacks_.load(); }
private:
    Fact checkApp_ = Fact::Unknown;
};
}
