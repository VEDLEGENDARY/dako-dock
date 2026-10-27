#include "../src/Island.h"
#include <cstdio>
#include <stdexcept>

void RequireRead(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void ReadPump(DWORD milliseconds) {
    auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < end);
}
std::vector<std::wstring> ReadWallpaperState() {
    std::vector<std::wstring> values;
    ComPtr<IDesktopWallpaper> desktop;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop)))) {
        DESKTOP_WALLPAPER_POSITION fit = DWPOS_CENTER;
        if (SUCCEEDED(desktop->GetPosition(&fit)))
            values.push_back(std::to_wstring(fit));
        for (const auto &display : DesktopCatalog::ReadDisplays(desktop.Get()))
            values.push_back(display.id + L"|" + display.path);
    }
    return values;
}
std::array<DWORD, 2> ReadThemeState() {
    std::array<DWORD, 2> values{MAXDWORD, MAXDWORD};
    const wchar_t *names[]{L"AppsUseLightTheme", L"SystemUsesLightTheme"};
    for (int i = 0; i < 2; ++i) {
        DWORD size = sizeof(DWORD);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", names[i],
                     RRF_RT_REG_DWORD, nullptr, &values[i], &size);
    }
    return values;
}
int wmain(int argc, wchar_t **argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    int result = 0;
    try {
        std::filesystem::path directory = argc > 1 ? argv[1] : L"build/tests/live/reports";
        std::filesystem::create_directories(directory);
        std::ofstream report(directory / L"read-only-controls.txt");
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"Dako.ReadOnlyControls.Test";
        RegisterClassW(&type);
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Read-only native capabilities", WS_POPUP,
                                      0, 0, 1, 1, nullptr, nullptr, type.hInstance, nullptr);
        RequireRead(window != nullptr, "read-only notification window");
        auto wallpaperBefore = ReadWallpaperState();
        auto themeBefore = ReadThemeState();
        {
            controls::Service service(window);
            HardwareStats hardware(window);
            auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
            service.Refresh(monitor);
            hardware.Request();
            auto started = GetTickCount64();
            bool requestedSecond = false;
            controls::Snapshot state;
            HardwareSnapshot stats;
            do {
                ReadPump(20);
                state = service.Read();
                stats = hardware.Snapshot();
                if (!requestedSecond && stats.history.Size() && GetTickCount64() - started >= 1100) {
                    hardware.Request();
                    requestedSecond = true;
                }
            } while (GetTickCount64() - started < 20000 && (state.monitor != monitor || stats.history.Size() < 2));
            RequireRead(state.monitor == monitor, "native capabilities complete their read-only request");
            RequireRead(stats.history.Size() >= 2 && stats.totalGiB > 0,
                        "native worker collects history and physical memory");
            RequireRead(!stats.top.empty() && !stats.memoryTop.empty(),
                        "native process rankings have readable samples");
            for (size_t i = 1; i < stats.top.size(); ++i)
                RequireRead(!stats.top[i].cpuValid || !stats.top[i - 1].cpuValid ||
                                stats.top[i - 1].cpu >= stats.top[i].cpu,
                            "CPU-ranked process order");
            for (size_t i = 1; i < stats.memoryTop.size(); ++i)
                RequireRead(stats.memoryTop[i - 1].workingMiB >= stats.memoryTop[i].workingMiB,
                            "memory-ranked process order");
            for (const auto &process : stats.top)
                RequireRead(!process.cpuValid || (process.cpu >= 0 && process.cpu <= 100),
                            "CPU deltas stay normalized to total logical capacity");
            report << "brightness_supported=" << state.brightnessSupported
                   << "\naudio_output_count=" << state.outputs.size() << "\nradios_available=" << state.radiosAvailable
                   << "\nradios_off=" << state.radiosOff << "\nfixed_volume_count=" << stats.volumes.size()
                   << "\nhistory_samples=" << stats.history.Size() << "\ncpu_rank_count=" << stats.top.size()
                   << "\nmemory_rank_count=" << stats.memoryTop.size() << "\nbattery_present=" << stats.batteryPresent
                   << "\nbattery_seconds=" << stats.batterySeconds << "\nuptime_seconds=" << stats.uptimeSeconds
                   << "\n";
            auto count = stats.history.Size();
            ReadPump(2200);
            RequireRead(hardware.Snapshot().history.Size() == count,
                        "stats worker blocks without visible-view requests");
        }
        RequireRead(themeBefore == ReadThemeState() && wallpaperBefore == ReadWallpaperState(),
                    "read-only capabilities and stats leave theme, wallpaper and fit unchanged");
        DestroyWindow(window);
        report << "settings_unchanged=PASS\nrequest_only_idle=PASS\nPASS\n";
        std::puts("PASS: native read-only control capabilities, CPU/memory rankings, history and blocked idle worker.");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        result = 1;
    }
    winrt::uninit_apartment();
    return result;
}
