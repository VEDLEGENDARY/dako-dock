#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

// No periodic timer. Cancellation drains callbacks; queued stale messages carry a rejected generation.
class OneShotDeadline {
    HWND owner_;
    UINT message_;
    PTP_TIMER timer_ = nullptr;
    std::atomic<WPARAM> token_ = 0;

  public:
    OneShotDeadline(HWND owner, UINT message) : owner_(owner), message_(message) {}
    OneShotDeadline(const OneShotDeadline &) = delete;
    ~OneShotDeadline() {
        Cancel();
        if (timer_)
            CloseThreadpoolTimer(timer_);
    }
    void Cancel() {
        if (!timer_)
            return;
        SetThreadpoolTimer(timer_, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer_, TRUE);
    }
    bool Arm(uint64_t milliseconds, WPARAM token) {
        Cancel();
        if (!milliseconds || milliseconds > 86400000)
            return false;
        if (!timer_)
            timer_ = CreateThreadpoolTimer(
                [](PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
                    auto self = static_cast<OneShotDeadline *>(context);
                    PostMessageW(self->owner_, self->message_, self->token_.load(), 0);
                },
                this, nullptr);
        if (!timer_)
            return false;
        token_ = token;
        LARGE_INTEGER ticks{};
        ticks.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000);
        FILETIME due{ticks.LowPart, static_cast<DWORD>(ticks.HighPart)};
        SetThreadpoolTimer(timer_, &due, 0, 0);
        return true;
    }
};
