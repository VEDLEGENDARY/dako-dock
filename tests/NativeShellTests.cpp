#include "../src/DesktopShell.h"
#include <cstdio>
desktop::Shell *activeShell = nullptr;
LRESULT CALLBACK TestProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == desktop::ShellChanged && activeShell) {
        activeShell->WindowEvent(static_cast<DWORD>(wparam), reinterpret_cast<HWND>(lparam));
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void Require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ExitProcess(1);
    }
}
void Settle(DWORD milliseconds) {
    ULONGLONG until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(20);
    } while (GetTickCount64() < until);
}
std::vector<desktop::Area> Areas() {
    std::vector<desktop::Area> areas;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(monitor, &info))
                reinterpret_cast<std::vector<desktop::Area> *>(context)->push_back({info.rcMonitor, info.rcWork});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&areas));
    return areas;
}
DWORD State() {
    APPBARDATA bar{sizeof(bar)};
    return static_cast<DWORD>(SHAppBarMessage(ABM_GETSTATE, &bar));
}
bool Restored(DWORD state, const std::vector<desktop::Area> &areas) {
    auto current = Areas();
    if (State() != state || current.size() != areas.size())
        return false;
    for (size_t i = 0; i < areas.size(); ++i)
        if (!EqualRect(&current[i].work, &areas[i].work))
            return false;
    return true;
}
int wmain(int argc, wchar_t **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1 && !wcscmp(argv[1], L"--shell-guard")) {
        std::wstring args;
        for (int i = 2; i < argc; ++i)
            args += std::wstring(argv[i]) + L" ";
        return desktop::Guard(args.c_str());
    }
    bool crash = argc > 1 && !wcscmp(argv[1], L"--crash");
    DWORD original = State();
    auto originalAreas = Areas();
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = TestProcedure;
    type.lpszClassName = L"Dako.Shell.Tests";
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&type);
    if (argc > 1 && !wcscmp(argv[1], L"--probe")) {
        HWND probe = CreateWindowExW(0, type.lpszClassName, L"Dako maximize check", WS_OVERLAPPEDWINDOW, 100, 100, 320,
                                     240, nullptr, nullptr, type.hInstance, nullptr);
        ShowWindow(probe, SW_SHOWNOACTIVATE);
        for (int i = 0; i < 100 && !IsZoomed(probe); ++i)
            Settle(20);
        Require(IsZoomed(probe) != FALSE, "new app starts normally maximized");
        RECT maximized{};
        GetWindowRect(probe, &maximized);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(probe, MONITOR_DEFAULTTONEAREST), &monitor);
        Require(maximized.left <= monitor.rcWork.left && maximized.top <= monitor.rcWork.top &&
                    maximized.right >= monitor.rcWork.right && maximized.bottom >= monitor.rcWork.bottom,
                "standard maximize covers the work area below the fixed rail");
        ShowWindow(probe, SW_RESTORE);
        SetWindowPos(probe, nullptr, 100, 100, 320, 240, SWP_NOZORDER | SWP_NOACTIVATE);
        Settle(500);
        RECT manual{};
        GetWindowRect(probe, &manual);
        Require(!IsZoomed(probe) && manual.right - manual.left == 320 && manual.bottom - manual.top == 240,
                "manual resize is respected");
        DestroyWindow(probe);
        return 0;
    }
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Dako hidden test", WS_OVERLAPPEDWINDOW, 100,
                                 100, 320, 240, nullptr, nullptr, type.hInstance, nullptr);
    Require(owner != nullptr, "hidden test window created");
    ShowWindow(owner, SW_MAXIMIZE);
    Settle(100);
    desktop::Shell shell;
    activeShell = &shell;
    shell.Initialize(owner);
    Require(!shell.MaximizeNew(), "window management is opt-in");
    shell.SetMaximizeNew(true);
    Require(shell.Start(), "recovery process ready before suppression");
    Settle(400);
    shell.Reconcile();
    Settle(200);
    bool visible = false;
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            if (desktop::IsTaskbar(window) && IsWindowVisible(window))
                *reinterpret_cast<bool *>(context) = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&visible));
    Require(!visible, "taskbar windows hidden");
    for (const auto &area : Areas()) {
        RECT bounds = area.monitor;
        HMONITOR monitor = MonitorFromRect(&bounds, MONITOR_DEFAULTTONULL);
        RECT expected = desktop::WorkArea(monitor, MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST));
        if (!EqualRect(&expected, &area.work))
            std::fprintf(stderr, "Work area actual: %ld,%ld,%ld,%ld; expected: %ld,%ld,%ld,%ld\n", area.work.left,
                         area.work.top, area.work.right, area.work.bottom, expected.left, expected.top, expected.right,
                         expected.bottom);
        Require(EqualRect(&expected, &area.work), "fixed rail reserved; remaining taskbar space reclaimed");
    }
    RECT visibleBounds{};
    MONITORINFO currentMonitor{sizeof(currentMonitor)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &currentMonitor);
    Require(
        SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_EXTENDED_FRAME_BOUNDS, &visibleBounds, sizeof(visibleBounds))) &&
            visibleBounds.top >= currentMonitor.rcWork.top,
        "already-maximized app clears the resting notch rail");
    ShowWindow(owner, SW_HIDE);
    shell.UpdateBrowserCutout(owner);
    RECT browserBounds{};
    GetWindowRect(owner, &browserBounds);
    HRGN clipped = CreateRectRgn(0, 0, 0, 0);
    int regionType = GetWindowRgn(owner, clipped);
    int center = (currentMonitor.rcMonitor.left + currentMonitor.rcMonitor.right) / 2 - browserBounds.left;
    Require(regionType != ERROR && !PtInRegion(clipped, center, currentMonitor.rcMonitor.top - browserBounds.top + 2) &&
                PtInRegion(clipped, 20, currentMonitor.rcMonitor.top - browserBounds.top + 2),
            "browser region skips only the notch, with no full-width gap");
    DeleteObject(clipped);
    shell.UpdateBrowserCutout(nullptr);
    clipped = CreateRectRgn(0, 0, 0, 0);
    Require(GetWindowRgn(owner, clipped) == ERROR, "browser's original default region restored");
    DeleteObject(clipped);
    if (crash) {
        if (argc > 2) {
            HWND target = reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10));
            shell.UpdateBrowserCutout(target);
            HRGN test = CreateRectRgn(0, 0, 0, 0);
            Require(GetWindowRgn(target, test) != ERROR, "foreign test browser clipped before abrupt exit");
            DeleteObject(test);
        }
        ExitProcess(0);
    } // Deliberately bypass destructors; the guard must restore Explorer.
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring probeCommand = L"\"" + std::wstring(executable) + L"\" --probe";
    STARTUPINFOW probeStartup{sizeof(probeStartup)};
    PROCESS_INFORMATION probeChild{};
    Require(CreateProcessW(executable, probeCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &probeStartup, &probeChild) != FALSE,
            "maximize probe started");
    CloseHandle(probeChild.hThread);
    for (int i = 0; i < 500 && WaitForSingleObject(probeChild.hProcess, 0) == WAIT_TIMEOUT; ++i)
        Settle(20);
    DWORD probeExit = 1;
    GetExitCodeProcess(probeChild.hProcess, &probeExit);
    CloseHandle(probeChild.hProcess);
    Require(probeExit == 0, "new-window maximize and manual resize checks");
    shell.Stop();
    shell.RemoveTray();
    Settle(400);
    Require(Restored(original, originalAreas), "normal exit restores taskbar state and work areas");
    DestroyWindow(owner);
    HWND recoveryTarget =
        CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Dako recovery region test", WS_OVERLAPPEDWINDOW, 100,
                        100, 320, 240, nullptr, nullptr, type.hInstance, nullptr);
    ShowWindow(recoveryTarget, SW_MAXIMIZE);
    ShowWindow(recoveryTarget, SW_HIDE);
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --crash " +
                           std::to_wstring(reinterpret_cast<ULONG_PTR>(recoveryTarget));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    Require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &child) != FALSE,
            "crash recovery child started");
    CloseHandle(child.hThread);
    for (int i = 0; i < 500 && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT; ++i)
        Settle(20);
    Require(WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0, "crash child terminated");
    DWORD exit = 1;
    GetExitCodeProcess(child.hProcess, &exit);
    CloseHandle(child.hProcess);
    Require(exit == 0, "crash child verified suppression");
    auto originalRegionRestored = [&] {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        bool restored = GetWindowRgn(recoveryTarget, region) == ERROR;
        DeleteObject(region);
        return restored;
    };
    for (int i = 0; i < 100 && (!Restored(original, originalAreas) || !originalRegionRestored()); ++i)
        Settle(100);
    Require(Restored(original, originalAreas), "guard restores after abrupt process exit");
    HRGN recovered = CreateRectRgn(0, 0, 0, 0);
    Require(GetWindowRgn(recoveryTarget, recovered) == ERROR,
            "guard restores original region of a surviving foreign process window");
    DeleteObject(recovered);
    DestroyWindow(recoveryTarget);
    std::puts("PASS: taskbar suppression, full work area, browser notch-only cutout and restoration, maximize/manual "
              "resize, normal exit and foreign-window recovery after abrupt exit.");
    return 0;
}
