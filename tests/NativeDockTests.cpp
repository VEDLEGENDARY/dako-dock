#include "../src/DesktopShell.h"
#include "../src/DockShell.h"
#include "../src/WindowList.h"
#include <oleacc.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <dbghelp.h>

LONG WINAPI DockCrash(EXCEPTION_POINTERS *exception) {
    auto process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    std::fprintf(stderr, "bar crash code=%08lx pc=%llx address=%llx\n", exception->ExceptionRecord->ExceptionCode,
                 context.Rip, exception->ExceptionRecord->ExceptionInformation[1]);
    for (int i = 0; i < 32; ++i) {
        char buffer[sizeof(SYMBOL_INFO) + 512]{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            std::fprintf(stderr, "  %s", symbol->Name);
        IMAGEHLP_LINE64 line{sizeof(line)};
        DWORD offset = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &offset, &line))
            std::fprintf(stderr, " %s:%lu", line.FileName, line.LineNumber);
        std::fprintf(stderr, "\n");
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

desktop::WindowList *observedWindows = nullptr;
bool probeMode = false;
void RequireDock(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
LRESULT CALLBACK DockTestProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == desktop::WindowEventMessage && observedWindows) {
        observedWindows->Request();
        return 0;
    }
    if (message == WM_DESTROY && probeMode) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void PumpDock(DWORD duration) {
    ULONGLONG end = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < end);
}
template <class Predicate> bool Until(Predicate predicate, DWORD timeout = 10000) {
    ULONGLONG end = GetTickCount64() + timeout;
    while (GetTickCount64() < end) {
        if (predicate())
            return true;
        PumpDock(10);
    }
    return predicate();
}
std::vector<desktop::Area> MonitorAreas() {
    std::vector<desktop::Area> result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(monitor, &info))
                reinterpret_cast<std::vector<desktop::Area> *>(context)->push_back({info.rcMonitor, info.rcWork});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
DWORD TaskbarState() {
    APPBARDATA data{sizeof(data)};
    return static_cast<DWORD>(SHAppBarMessage(ABM_GETSTATE, &data));
}
bool RestoredDock(const std::vector<desktop::Area> &original, DWORD state) {
    if (TaskbarState() != state)
        return false;
    auto current = MonitorAreas();
    if (current.size() != original.size())
        return false;
    for (size_t i = 0; i < original.size(); ++i)
        if (!EqualRect(&original[i].work, &current[i].work))
            return false;
    return true;
}
std::vector<HWND> DockWindows() {
    std::vector<HWND> result;
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            wchar_t name[64]{};
            GetClassNameW(window, name, 64);
            if (process == GetCurrentProcessId() && !wcscmp(name, L"Dako.Native.Dock"))
                reinterpret_cast<std::vector<HWND> *>(context)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Child {
    HANDLE process = nullptr;
    DWORD id = 0;
    ~Child() {
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
                TerminateProcess(process, 9);
            WaitForSingleObject(process, 10000);
            CloseHandle(process);
        }
    }
    void Start(const std::wstring &arguments) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring command = L"\"" + std::wstring(executable) + L"\" " + arguments;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        RequireDock(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &startup, &child) != FALSE,
                    "bar test child starts");
        process = child.hProcess;
        id = child.dwProcessId;
        CloseHandle(child.hThread);
    }
    HWND Window() const {
        struct Search {
            DWORD id;
            HWND window = nullptr;
        } data{id};
        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(context);
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                if (process == search.id && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
                    search.window = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&data));
        return data.window;
    }
};
struct Counts {
    DWORD handles = 0, gdi = 0, user = 0;
};
Counts CountResources() {
    Counts count;
    GetProcessHandleCount(GetCurrentProcess(), &count.handles);
    count.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    count.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    return count;
}
int wmain(int argc, wchar_t **argv) {
    SetUnhandledExceptionFilter(DockCrash);
    if (argc > 1 && !wcscmp(argv[1], L"--shell-guard")) {
        std::wstring args;
        for (int i = 2; i < argc; ++i)
            args += std::wstring(argv[i]) + L" ";
        return desktop::Guard(args.c_str());
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment))
        return 1;
    struct ApartmentLifetime {
        ~ApartmentLifetime() { CoUninitialize(); }
    } apartmentLifetime;
    try {
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = DockTestProcedure;
        type.lpszClassName = L"Dako.Dock.Tests";
        type.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        RegisterClassW(&type);
        bool probe = argc > 1 && !wcscmp(argv[1], L"--probe");
        HWND owner = CreateWindowExW(WS_EX_APPWINDOW, type.lpszClassName,
                                     probe ? L"Fixture running application" : L"Dako bar harness", WS_OVERLAPPEDWINDOW,
                                     40, 40, 640, 480, nullptr, nullptr, type.hInstance, nullptr);
        RequireDock(owner != nullptr, "bar test owner created");
        if (probe) {
            probeMode = true;
            ShowWindow(owner, SW_SHOWNOACTIVATE);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            return 0;
        }
        bool crash = argc > 2 && !wcscmp(argv[1], L"--crash-dock");
        if (crash) {
            desktop::Shell recovery;
            recovery.Initialize(owner);
            bool desktop = argc > 3 && !wcscmp(argv[3], L"desktop");
            if (desktop)
                RequireDock(recovery.Start(true), "crash child Explorer guard ready");
            desktop::DockShell bars;
            RequireDock(bars.Start(owner, &recovery, !desktop, 1, {}), "crash child registers dock");
            HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2]);
            RequireDock(ready != nullptr, "crash child ready event opened");
            SetEvent(ready);
            CloseHandle(ready);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            return 3;
        }
        auto original = MonitorAreas();
        DWORD state = TaskbarState();
        desktop::Shell recovery;
        recovery.Initialize(owner);
        int opened = 0;
        HMONITOR openedMonitor = nullptr;
        std::wstring launched;
        std::pair<size_t, size_t> reordered{99, 99};
        desktop::DockShell::Callbacks callbacks;
        callbacks.open = [&](int action, HMONITOR monitor) {
            opened = action;
            openedMonitor = monitor;
        };
        callbacks.launch = [&](const std::wstring &key) { launched = key; };
        callbacks.reorder = [&](size_t from, size_t to) { reordered = {from, to}; };
        desktop::DockShell bars;
        RequireDock(!bars.Start(owner, &recovery, false, 1, callbacks), "no reservation without desktop opt-in");
        RequireDock(RestoredDock(original, state), "ordinary startup keeps Explorer and work areas");
        RequireDock(recovery.Start(true), "desktop consent starts recovery before suppression");
        RequireDock(bars.Start(owner, &recovery, false, 1, callbacks), "desktop dock registration");
        PumpDock(300);
        RequireDock(bars.Count() == original.size(), "one bar per connected monitor");
        RequireDock(bars.CompositionAvailable(), "dock uses transparent DirectComposition");
        for (const auto &area : MonitorAreas()) {
            RECT monitor = area.monitor;
            HMONITOR handle = MonitorFromRect(&monitor, MONITOR_DEFAULTTONULL);
            RECT bar = bars.Bounds(handle);
            RequireDock(bar.right - bar.left == monitor.right - monitor.left, "bar spans monitor width");
            RequireDock(std::abs(area.work.bottom - bar.top) <= 2, "AppBar reservation ends above the bar");
        }
        ShowWindow(owner, SW_MAXIMIZE);
        PumpDock(300);
        RECT visible{};
        RequireDock(SUCCEEDED(DwmGetWindowAttribute(owner, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible))),
                    "maximized test window frame can be measured");
        HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        RECT outer{}, client{};
        GetWindowRect(owner, &outer);
        GetClientRect(owner, &client);
        MapWindowPoints(owner, nullptr, reinterpret_cast<POINT *>(&client), 2);
        auto reserved = bars.Bounds(monitor);
        std::fprintf(
            stderr,
            "maximized visible=%ld,%ld,%ld,%ld outer=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld bar=%ld,%ld,%ld,%ld\n",
            visible.left, visible.top, visible.right, visible.bottom, outer.left, outer.top, outer.right, outer.bottom,
            client.left, client.top, client.right, client.bottom, reserved.left, reserved.top, reserved.right,
            reserved.bottom);
        RequireDock(visible.bottom <= bars.Bounds(monitor).top + 1, "maximized window content clears the bar");
        ShowWindow(owner, SW_HIDE);
        auto nativeBars = DockWindows();
        RequireDock(!nativeBars.empty(), "registered bar HWNDs are visible to tests");
        HWND barWindow = nativeBars.front();
        auto foregroundMonitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
        for (HWND candidate : nativeBars)
            if (MonitorFromWindow(candidate, MONITOR_DEFAULTTONEAREST) == foregroundMonitor)
                barWindow = candidate;
        auto pill = bars.PillBounds(MonitorFromWindow(barWindow, MONITOR_DEFAULTTONEAREST));
        RECT strip{};
        GetWindowRect(barWindow, &strip);
        POINT outside{strip.left + 1, strip.top + 20},
            inside{(pill.left + pill.right) / 2, (pill.top + pill.bottom) / 2};
        RequireDock(SendMessageW(barWindow, WM_NCHITTEST, 0, MAKELPARAM(outside.x, outside.y)) == HTTRANSPARENT,
                    "outside-pill non-client hit is transparent");
        RequireDock(WindowFromPoint(outside) != barWindow,
                    "outside-pill input reaches another process through window region");
        RequireDock(SendMessageW(barWindow, WM_NCHITTEST, 0, MAKELPARAM(inside.x, inside.y)) == HTCLIENT,
                    "inside capsule accepts input");
        HRGN nativeRegion = CreateRectRgn(0, 0, 0, 0);
        RequireDock(GetWindowRgn(barWindow, nativeRegion) != ERROR && !PtInRegion(nativeRegion, 1, 20),
                    "native capsule region excludes transparent strip for cross-thread routing");
        DeleteObject(nativeRegion);
        SendMessageW(barWindow, desktop::DockCallback, ABN_FULLSCREENAPP, TRUE);
        PumpDock(50);
        RequireDock(IsWindowVisible(barWindow)!=FALSE,"V4 Always show keeps the island above fullscreen");
        desktop::DockOptions fullscreenPolicy;fullscreenPolicy.fullscreenPolicy=2;bars.Options(fullscreenPolicy);bars.RefreshFullscreen();
        RequireDock(!IsWindowVisible(barWindow), "fullscreen AppBar notification hides the bar");
        SendMessageW(barWindow, desktop::DockCallback, ABN_FULLSCREENAPP, FALSE);
        PumpDock(50);
        RequireDock(IsWindowVisible(barWindow) != FALSE, "fullscreen exit restores the bar");
        bars.Options({});
        std::fprintf(stderr, "stage fullscreen PASS\n");
        Microsoft::WRL::ComPtr<IAccessible> accessible;
        RequireDock(SUCCEEDED(AccessibleObjectFromWindow(barWindow, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
                                                         reinterpret_cast<void **>(accessible.GetAddressOf()))),
                    "bar exposes a native MSAA provider");
        LONG children = 0;
        accessible->get_accChildCount(&children);
        RequireDock(children >= 4, "bar controls have MSAA children");
        VARIANT child{}, role{};
        child.vt = VT_I4;
        child.lVal = desktop::DockApps;
        BSTR name = nullptr;
        accessible->get_accName(child, &name);
        RequireDock(name && std::wstring(name) == L"Open Apps", "bar Apps control has an accessible name");
        SysFreeString(name);
        accessible->get_accRole(child, &role);
        RequireDock(role.lVal == ROLE_SYSTEM_PUSHBUTTON, "bar controls are buttons");

        std::fprintf(stderr, "stage MSAA PASS\n");
        SendMessageW(barWindow, desktop::DockInvoke, desktop::DockApps, 0);
        RequireDock(opened == desktop::DockApps &&
                        openedMonitor == MonitorFromWindow(barWindow, MONITOR_DEFAULTTONEAREST),
                    "bar navigation carries its own monitor anchor");
        desktop::PinEntry first;
        first.key = L"fixture:first";
        first.name = L"First pin";
        desktop::PinEntry second;
        second.key = L"fixture:second";
        second.name = L"Second pin";
        bars.SetData({first, second}, {}, {});
        PumpDock(20);
        float barScale = GetDpiForWindow(barWindow) / 96.0f;
        RECT pinBounds{};
        GetWindowRect(barWindow, &pinBounds);
        auto pinLayout = desktop::SolveDock((pinBounds.right - pinBounds.left) / barScale,
                                            desktop::BuildDockGroups({first, second}, {}, nullptr), {});
        auto firstHit = std::find_if(pinLayout.hits.begin(), pinLayout.hits.end(),
                                     [](const desktop::DockHit &hit) { return hit.action == desktop::DockGroupFirst; });
        auto secondHit = std::find_if(pinLayout.hits.begin(), pinLayout.hits.end(), [](const desktop::DockHit &hit) {
            return hit.action == desktop::DockGroupFirst + 1;
        });
        RequireDock(firstHit != pinLayout.hits.end() && secondHit != pinLayout.hits.end(),
                    "both fixture pins have hit bounds");
        LPARAM startPoint =
            MAKELPARAM(static_cast<int>(firstHit->rect.CenterX() * barScale), static_cast<int>(20 * barScale));
        LPARAM endPoint =
            MAKELPARAM(static_cast<int>(secondHit->rect.CenterX() * barScale), static_cast<int>(20 * barScale));
        SendMessageW(barWindow, WM_LBUTTONDOWN, MK_LBUTTON, startPoint);
        SendMessageW(barWindow, WM_MOUSEMOVE, MK_LBUTTON, endPoint);
        SendMessageW(barWindow, WM_LBUTTONUP, 0, endPoint);
        RequireDock(reordered.first == 0 && reordered.second == 1, "dragging a pin invokes ordered persistence");
        SendMessageW(barWindow, WM_MBUTTONUP, 0, startPoint);
        RequireDock(launched == first.key, "middle click requests another instance through the launch callback");
        SendMessageW(barWindow, desktop::DockInvoke, desktop::DockGroupFirst, 1);
        reordered = {99, 99};
        BYTE originalKeys[256]{}, keys[256]{};
        GetKeyboardState(originalKeys);
        memcpy(keys, originalKeys, sizeof(keys));
        keys[VK_CONTROL] |= 0x80;
        SetKeyboardState(keys);
        SendMessageW(barWindow, WM_KEYDOWN, VK_RIGHT, 0);
        SetKeyboardState(originalKeys);
        RequireDock(reordered.first == 0 && reordered.second == 1,
                    "keyboard pin reorder follows the same persistence path");

        std::fprintf(stderr, "stage pin interactions PASS\n");
        Child app;
        app.Start(L"--probe");
        RequireDock(Until([&] { return app.Window() != nullptr; }), "foreign app window opens");
        desktop::WindowList list;
        observedWindows = &list;
        RequireDock(list.Start(owner), "event-driven window list starts");
        RequireDock(Until([&] {
                        for (const auto &entry : list.Snapshot())
                            if (entry.process == app.id)
                                return true;
                        return false;
                    }),
                    "window list discovers the foreign app");
        HWND appWindow = app.Window();
        POINT routed{strip.left + 80, strip.top + 20};
        SetWindowPos(appWindow, HWND_TOPMOST, strip.left, strip.top - 40, 320, 160, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PumpDock(50);
        RequireDock(WindowFromPoint(routed) == appWindow,
                    "transparent dock margin routes to a real foreign process window");
        SetWindowPos(appWindow, HWND_NOTOPMOST, 40, 40, 640, 480, SWP_NOACTIVATE);
        SendMessageW(appWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Renamed fixture application"));
        RequireDock(Until([&] {
                        for (const auto &entry : list.Snapshot())
                            if (entry.process == app.id && entry.title == L"Renamed fixture application")
                                return true;
                        return false;
                    }),
                    "name-change events update app titles");
        std::fprintf(stderr, "stage window events PASS\n");
        auto source = desktop::WindowList::Inspect(appWindow);
        RequireDock(source.has_value(), "foreign visible window passes eligibility checks");
        bars.SetData({}, {*source}, {});
        PumpDock(100);
        monitor = source->monitor;
        RECT barBounds = bars.Bounds(monitor);
        for (HWND window : DockWindows())
            if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor)
                barWindow = window;
        UINT dpi = GetDpiForWindow(barWindow);
        float scale = dpi / 96.0f;
        auto groups = desktop::BuildDockGroups({}, {*source}, monitor);
        auto geometry = desktop::SolveDock((barBounds.right - barBounds.left) / scale, groups, {});
        auto target = std::find_if(geometry.hits.begin(), geometry.hits.end(),
                                   [](const desktop::DockHit &hit) { return hit.action == desktop::DockGroupFirst; });
        RequireDock(target != geometry.hits.end(), "running app appears in its monitor bar");
        SendMessageW(barWindow, WM_MOUSEMOVE, 0,
                     MAKELPARAM(static_cast<int>(target->rect.CenterX() * scale), static_cast<int>(20 * scale)));
        SendMessageW(barWindow, WM_MOUSEHOVER, 0, 0);
        RequireDock(bars.PreviewAvailable(), "production preview registers a real DWM thumbnail");
        SendMessageW(barWindow, desktop::DockInvoke, desktop::DockGroupFirst, 1);
        SendMessageW(barWindow, WM_KEYDOWN, VK_UP, 0);
        PumpDock(100);
        RequireDock(bars.PreviewAvailable(), "keyboard-focused preview survives pointer dismissal timer");
        bars.Flash(appWindow);
        PumpDock(100);
        std::fprintf(stderr, "stage thumbnail PASS\n");
        list.Stop();
        observedWindows = nullptr;
        bars.Stop();
        PumpDock(100);
        accessible->get_accChildCount(&children);
        RequireDock(children == 0, "MSAA provider safely disconnects after bar exit");
        accessible.Reset();
        recovery.Stop();
        RequireDock(Until([&] { return RestoredDock(original, state); }),
                    "normal AppBar exit restores work areas and Explorer");

        RequireDock(recovery.Start(true), "desktop-mode recovery starts before suppression");
        RequireDock(bars.Start(owner, &recovery, false, 1, {}), "desktop-mode bar registers after suppression");
        PumpDock(300);
        for (const auto &area : MonitorAreas()) {
            RECT rect = area.monitor;
            auto handle = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
            auto bar = bars.Bounds(handle);
            UINT x = 96, y = 96;
            GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &x, &y);
            RequireDock(area.monitor.bottom - area.work.bottom <= desktop::DipPixels(72, x) + 2,
                        "Explorer and Dako do not both reserve taskbar height");
            RequireDock(std::abs(area.work.bottom - bar.top) <= 2, "desktop-mode work area follows AppBar bounds");
        }
        int numberRegistrations = 0;
        for (UINT i = 0; i < 9; ++i) {
            if (RegisterHotKey(owner, 3000 + static_cast<int>(i), MOD_WIN | MOD_NOREPEAT, '1' + i)) {
                ++numberRegistrations;
                UnregisterHotKey(owner, 3000 + static_cast<int>(i));
            }
        }
        std::printf("win_number_registrations=%d/9\n", numberRegistrations);
        std::fflush(stdout);
        recovery.ExplorerRestarted();
        bars.ExplorerRestarted();
        PumpDock(300);
        RequireDock(bars.Count() == original.size(), "Explorer restart path recreates every AppBar registration");
        bars.Stop();
        recovery.Stop();
        RequireDock(Until([&] { return RestoredDock(original, state); }),
                    "desktop-mode exit restores Explorer and work areas");

        RequireDock(bars.Start(owner, nullptr, true, 1, {}),
                    "isolated preview bar starts without recovery or reservation");
        PumpDock(30);
        bars.Stop();
        auto baseline = CountResources();
        for (int cycle = 0; cycle < 50; ++cycle) {
            RequireDock(bars.Start(owner, nullptr, true, 1, {}), "preview bar lifecycle starts");
            PumpDock(20);
            bars.Stop();
        }
        PumpDock(50);
        auto after = CountResources();
        RequireDock(after.handles <= baseline.handles + 16 && after.gdi <= baseline.gdi + 4 &&
                        after.user <= baseline.user + 8,
                    "50 preview bar cycles release handles, windows and graphics resources");
        RequireDock(RestoredDock(original, state), "isolated previews never reserve desktop space");

        for (bool desktop : {false, true}) {
            std::wstring event = L"Local\\Dako.Dock.Crash." + std::to_wstring(GetCurrentProcessId()) + L"." +
                                 std::to_wstring(GetTickCount64());
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, event.c_str());
            RequireDock(ready != nullptr, "ready event created");
            Child crashChild;
            crashChild.Start(L"--crash-dock " + event + (desktop ? L" desktop" : L""));
            bool started = Until([&] { return WaitForSingleObject(ready, 0) == WAIT_OBJECT_0; });
            CloseHandle(ready);
            RequireDock(started, "crash child has registered its bars before forced kill");
            RequireDock(TerminateProcess(crashChild.process, 77) != FALSE, "only the test child is forcibly killed");
            RequireDock(WaitForSingleObject(crashChild.process, 10000) == WAIT_OBJECT_0, "crash child exits");
            RequireDock(Until([&] { return RestoredDock(original, state); }),
                        "recovery guard removes AppBars and restores work areas after forced kill");
        }
        PostMessageW(appWindow, WM_CLOSE, 0, 0);
        WaitForSingleObject(app.process, 5000);
        recovery.RemoveTray();
        DestroyWindow(owner);
        std::puts("PASS: per-monitor AppBars, maximized-window clearance, fullscreen callbacks, MSAA, window events, "
                  "real DWM previews, Explorer restart path, no double reservation, 50 preview cycles and forced-kill "
                  "recovery.");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
