#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "../src/FrameClock.h"

constexpr UINT Frame = WM_APP + 11;
void Require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
bool Receive(HWND window, MSG &message, DWORD timeout) {
    ULONGLONG end = GetTickCount64() + timeout;
    do {
        if (PeekMessageW(&message, window, Frame, Frame, PM_REMOVE))
            return true;
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < end);
    return false;
}
int main() {
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = DefWindowProcW;
    type.lpszClassName = L"Dako.FrameClock.Tests";
    RegisterClassW(&type);
    HWND window =
        CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    Require(window != nullptr, "message-only target exists");
    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(ready != nullptr, "test frame event exists");
    FrameClock clock;
    Require(!clock.Start(window, Frame, nullptr) && !clock.Active(), "missing frame handle uses timer fallback");
    Require(clock.Start(window, Frame, ready) && clock.Start(window, Frame, ready), "repeated start reuses one worker");
    MSG message{};
    Require(!Receive(window, message, 20), "no request before frame readiness");
    SetEvent(ready);
    Require(Receive(window, message, 1000), "ready frame wakes the UI");
    WPARAM token = message.wParam;
    Require(clock.Accepts(token), "current generation is actionable");
    SetEvent(ready);
    Require(!Receive(window, message, 20), "one outstanding request until presentation acknowledgement");
    clock.Presented();
    Require(Receive(window, message, 1000), "presentation unlocks next ready frame");
    ULONGLONG start = GetTickCount64();
    clock.Stop();
    Require(GetTickCount64() - start < 500 && !clock.Active() && !clock.Accepts(token),
            "stop cancels acknowledgement wait and stale generation");
    ResetEvent(ready);
    Require(clock.Start(window, Frame, ready), "clock restarts after settling");
    start = GetTickCount64();
    clock.Stop();
    Require(GetTickCount64() - start < 500, "stop cancels readiness wait");
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    for (int i = 0; i < 50; ++i) {
        Require(clock.Start(window, Frame, ready), "repeated animation starts");
        clock.Stop();
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    Require(after <= before + 2, "repeated active/idle cycles release handles and threads");
    CloseHandle(ready);
    DestroyWindow(window);
    std::puts(
        "PASS: frame readiness, one-frame backpressure, generation rejection, cancellation and 50-cycle cleanup.");
}
