#include "Island.h"
#include "WallpaperReveal.h"
#include "ClipboardTransfer.h"
#include "WindowsFileSearch.h"
#include "ShelfOle.h"
#include "SystemActionNative.h"

HWND FindNormalIsland() {
    HWND found = nullptr;
    EnumWindows(
        [](HWND handle, LPARAM context) -> BOOL {
            wchar_t name[64]{};
            GetClassNameW(handle, name, 64);
            if (wcscmp(name, WindowClass) || GetPropW(handle, L"Dako.IsolatedPreview") || !IsWindowVisible(handle))
                return TRUE;
            *reinterpret_cast<HWND *>(context) = handle;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    clipboard::SetExtraHandler([](clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
        auto result = files::HandleTransfer(operation, payload, error);
        if (result != clipboard::TransferStatus::Unsupported)
            return result;
        result = shelf::HandleTransfer(operation, payload, error);
        return result == clipboard::TransferStatus::Unsupported
                   ? systemactions::HandleTransfer(operation, payload, error)
                   : result;
    });
    constexpr std::wstring_view transfer = L"--clipboard-transfer=";
    if (std::wstring_view(command).starts_with(transfer))
        return clipboard::TransferHelper(command + transfer.size());
    if (!wcscmp(command, L"--clipboard-transfer-check")) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        return clipboard::CheckTransfer(
                   (std::filesystem::path(executable).parent_path() / L"clipboard-helper-check.txt").wstring())
                   ? 0
                   : 12;
    }
    effects::Register();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (std::wstring_view(command).starts_with(L"--module-measure=")) {
        wchar_t *end = nullptr;
        unsigned long seconds = wcstoul(command + 17, &end, 10);
        if (!end || *end || seconds < 2 || seconds > 60)
            return 2;
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        return modules::Measure((std::filesystem::path(executable).parent_path() / L"module-budgets.csv").wstring(),
                                static_cast<unsigned>(seconds))
                   ? 0
                   : 11;
    }
    if (!wcscmp(command, L"--module-check")) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        return modules::Check((std::filesystem::path(executable).parent_path() / L"module-check.txt").wstring()) ? 0
                                                                                                                 : 10;
    }
    if (!wcscmp(command, L"--effects-test")) {
        WallpaperReveal reveal;
        if (!reveal.VerifyGeometry())
            return 7;
        if (!reveal.Freeze(false, false))
            return 0;
        reveal.Begin();
        ULONGLONG deadline = GetTickCount64() + 2000;
        while (reveal.Running() && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return reveal.Running() ? 8 : 0;
    }
    if (wcsncmp(command, L"--shell-guard ", 14) == 0)
        return desktop::Guard(command + 14);
    if (std::wstring_view(command).starts_with(L"--exit-preview=")) {
        wchar_t *end = nullptr;
        ULONGLONG requested = _wcstoui64(command + 15, &end, 10);
        if (!requested || requested > MAXDWORD || !end || *end)
            return 2;
        struct Target {
            DWORD process;
            HWND window = nullptr;
        } selected{static_cast<DWORD>(requested)};
        EnumWindows(
            [](HWND handle, LPARAM context) -> BOOL {
                auto *target = reinterpret_cast<Target *>(context);
                DWORD process = 0;
                GetWindowThreadProcessId(handle, &process);
                if (process != target->process || !GetPropW(handle, L"Dako.IsolatedPreview"))
                    return TRUE;
                wchar_t name[64]{};
                GetClassNameW(handle, name, 64);
                if (wcscmp(name, WindowClass))
                    return TRUE;
                target->window = handle;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&selected));
        if (!selected.window)
            return 0;
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, selected.process);
        PostMessageW(selected.window, WM_CLOSE, 0, 0);
        DWORD wait = process ? WaitForSingleObject(process, 15000) : WAIT_FAILED;
        if (process)
            CloseHandle(process);
        return wait == WAIT_OBJECT_0 ? 0 : 6;
    }
    if (!wcscmp(command, L"--exit")) {
        HWND existing = FindNormalIsland();
        if (!existing)
            return 0;
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
        PostMessageW(existing, WM_CLOSE, 0, 0);
        DWORD wait = process ? WaitForSingleObject(process, 15000) : WAIT_FAILED;
        if (process)
            CloseHandle(process);
        return wait == WAIT_OBJECT_0 ? 0 : 6;
    }
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment))
        return 1;
    if (wcsncmp(command, L"--restore-session=", 18) == 0) {
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        bool ok = args && count == 2 && effects::Retry(std::wstring(args[1]).substr(18));
        if (args)
            LocalFree(args);
        CoUninitialize();
        return ok ? 0 : 3;
    }
    if (!wcscmp(command, L"--capability-check")) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        parity::CapabilityReport((std::filesystem::path(executable).parent_path() / L"capabilities.txt").wstring());
        CoUninitialize();
        return 0;
    }
    if (!wcscmp(command, L"--runtime-check")) {
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = DefWindowProcW;
        type.lpszClassName = L"Dako.Runtime.Check";
        RegisterClassW(&type);
        HWND target = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Dako runtime check", WS_POPUP, 0, 0, 1, 1,
                                      nullptr, nullptr, instance, nullptr);
        if (!target) {
            CoUninitialize();
            return 9;
        }
        unsigned accent = 0;
        HardwareSnapshot snapshot;
        int samples = 0;
        bool second = false;
        ULONGLONG start = GetTickCount64();
        {
            DesktopCatalog catalog(target);
            HardwareStats hardware(target);
            hardware.Request();
            while (GetTickCount64() - start < 8000 && (samples < 2 || accent == 0)) {
                if (samples > 0 && !second && GetTickCount64() - start > 1200) {
                    second = true;
                    hardware.Request();
                }
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == ThemeChanged)
                        accent = static_cast<unsigned>(message.wParam);
                    if (message.message == HardwareChanged) {
                        snapshot = hardware.Snapshot();
                        ++samples;
                    }
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }
        DestroyWindow(target);
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto path = std::filesystem::path(executable).parent_path() / L"runtime-check.txt";
        privacy::Write audit(L"diagnostics", L"file-write", path.wstring(), L"native-runtime-report");
        std::ofstream report(path, std::ios::trunc);
        report << "hardware_samples=" << samples << "\nphysical_memory_gib=" << snapshot.totalGiB
               << "\ntop_process_count=" << snapshot.top.size() << "\ngpu_counter_available=" << snapshot.gpuValid
               << "\nwallpaper_accent_rgb=" << accent << "\n";
        report.close();
        audit.Complete(static_cast<bool>(report));
        CoUninitialize();
        return samples >= 2 && snapshot.totalGiB > 0 && accent != 0 ? 0 : 9;
    }
    if (wcsstr(command, L"--restore-original-wallpaper")) {
        const auto original = DesktopCatalog::PreservePersonalWallpaper();
        const HRESULT restored = DesktopCatalog::ApplyWallpaper(original);
        const auto current = DesktopCatalog::CurrentWallpaperPath();
        const bool verified =
            SUCCEEDED(restored) && CompareStringOrdinal(current.c_str(), -1, original.c_str(), -1, TRUE) == CSTR_EQUAL;
        CoUninitialize();
        return verified ? 0 : 3;
    }
    int result = 0;
    {
        Island app;
        std::wstring renderDirectory;
        int argCount = 0;
        LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &argCount);
        if (args) {
            for (int i = 1; i < argCount; ++i) {
                const std::wstring arg = args[i];
                if (arg.starts_with(L"--render-dir="))
                    renderDirectory = arg.substr(13);
                if (arg.starts_with(L"--diagnostics="))
                    app.diagnosticsPath = arg.substr(14);
            }
            LocalFree(args);
        }
        wchar_t runningExecutable[32768]{};
        GetModuleFileNameW(nullptr, runningExecutable, 32768);
        app.desktopRequested = wcsstr(command, L"--desktop-mode") != nullptr;
        if (wcsstr(command, L"--layout=notch"))
            app.layoutOverride = L"notch";
        if (wcsstr(command, L"--layout=dock") || wcsstr(command, L"--layout=bar"))
            app.layoutOverride = L"dock";
        app.renderMode = !renderDirectory.empty();
        app.previewMode = wcsstr(command, L"--preview") != nullptr ||
                          CompareStringOrdinal(std::filesystem::path(runningExecutable).filename().c_str(), -1,
                                               L"Dako.preview.exe", -1, TRUE) == CSTR_EQUAL;
        app.PreviewTrace("startup");
        HANDLE singleton = nullptr;
        if (!app.renderMode && !app.previewMode) {
            singleton = CreateMutexW(nullptr, FALSE, L"Local\\Dako.Native.Island.Singleton");
            if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
                HWND existing = FindNormalIsland();
                if (existing) {
                    DWORD processId = 0;
                    GetWindowThreadProcessId(existing, &processId);
                    AllowSetForegroundWindow(processId);
                    PostMessageW(existing, RevealDock, wcsstr(command, L"--tab=stats") ? 1 : 0, 0);
                }
                CloseHandle(singleton);
                CoUninitialize();
                return 0;
            }
        }
        WNDCLASSW windowClass{};
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.lpszClassName = WindowClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&windowClass);
        bool inspectMode = wcsstr(command, L"--inspect") != nullptr;
        HWND window =
            CreateWindowExW(WS_EX_TOPMOST | (app.previewMode ? WS_EX_APPWINDOW : WS_EX_TOOLWINDOW), WindowClass,
                            app.previewMode ? L"Dako v4.0 — Isolated Preview" : L"Dako — Desktop Dock",
                            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 300, 100, nullptr, nullptr, instance, &app);
        if (!window) {
            MessageBoxW(nullptr, L"Dako could not create its native window.", L"Dako", MB_OK | MB_ICONERROR);
            result = 1;
        } else if (app.renderMode) {
            try {
                app.RenderSuite(renderDirectory);
                VerifyAccessibility(app);
                // VerifyAccessibility installed the production MSAA provider.
                LONG children = 0;
                Check(app.accessible->get_accChildCount(&children));
                VARIANT child{};
                child.vt = VT_I4;
                child.lVal = app.hits.empty() ? 0 : app.hits.front().child;
                BSTR name = nullptr;
                Check(app.accessible->get_accName(child, &name));
                bool valid = children == 1 && name && std::wstring(name) == L"Reveal dock";
                SysFreeString(name);
                if (!valid)
                    winrt::throw_hresult(E_UNEXPECTED);
            } catch (const winrt::hresult_error &error) {
                OutputDebugStringW(error.message().c_str());
                std::wofstream details(std::filesystem::path(renderDirectory) / L"failure.txt");
                details << error.message().c_str();
                result = 2;
            }
            DestroyWindow(window);
        } else {
            app.PreviewTrace("window-created");
            if (app.options.Bool(L"ReplaceTaskbar") && !app.options.Bool(L"SafeMode"))
                app.ConfirmDesktopChoice();
            app.shell.Initialize(window);
            app.StartDesktop();
            if (wcsstr(command, L"--features") || (!app.previewMode && app.moduleRegistry.FirstRun())) {
                app.featuresPage = app.moduleRegistry.FirstRun() ? 1 : 0;
                app.OpenFeatures();
                app.launchPreview = true;
            } else if (wcsstr(command, L"--tab=stats")) {
                app.SetTab(Tab::Stats);
                app.launchPreview = true;
            } else if ((wcsstr(command, L"--expanded") || inspectMode || app.previewMode) &&
                       !wcsstr(command, L"--resting")) {
                app.SetExpanded(true);
                app.launchPreview = true;
            }
            app.PreviewTrace("show");
            ShowWindow(window, app.previewMode ? SW_SHOW : SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            app.PreviewTrace("message-loop");
            MSG message{};
            BOOL status = 0;
            while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                if (message.message == WM_KEYDOWN && (message.wParam == VK_NEXT || message.wParam == VK_PRIOR) &&
                    (GetKeyState(VK_CONTROL) & 0x8000) && GetAncestor(message.hwnd, GA_ROOT) == window) {
                    app.Key(message.wParam);
                    continue;
                }
                if ((message.hwnd == app.taskEdit || message.hwnd == app.appSearch ||
                     message.hwnd == app.wifiPassword || message.hwnd == app.clipboardEdit) &&
                    message.message == WM_KEYDOWN) {
                    if (message.hwnd == app.clipboardEdit &&
                        (message.wParam == VK_RETURN || message.wParam == VK_DOWN || message.wParam == VK_UP)) {
                        SetFocus(window);
                        app.Key(message.wParam);
                        continue;
                    }
                    if (message.wParam == VK_RETURN && message.hwnd != app.clipboardEdit) {
                        if (message.hwnd == app.appSearch) {
                            if (app.flyout == Flyout::Palette) {
                                app.ExecutePalette(0);
                                continue;
                            }
                            for (size_t slot = 0; slot < app.AppSlots(); ++slot) {
                                size_t index = app.AppIndex(slot);
                                if (index < app.apps.size() && app.AppKey(app.apps[index]) == app.selectedAppKey) {
                                    app.Invoke(300 + static_cast<int>(slot));
                                    break;
                                }
                            }
                        } else
                            app.Invoke(message.hwnd == app.taskEdit ? AddTask : FlyoutConnect);
                        continue;
                    }
                    if (message.hwnd == app.appSearch && (message.wParam == VK_DOWN || message.wParam == VK_UP)) {
                        app.RebuildLayout();
                        for (size_t i = 0; i < app.hits.size(); ++i)
                            if (app.hits[i].id == (app.flyout == Flyout::Palette ? 2600 : 300)) {
                                app.FocusAt(static_cast<int>(i));
                                break;
                            }
                        continue;
                    }
                    if (message.wParam == VK_ESCAPE) {
                        SetFocus(window);
                        app.Key(VK_ESCAPE);
                        continue;
                    }
                    if (message.wParam == VK_F2) {
                        app.Key(VK_F2);
                        continue;
                    }
                    if (message.wParam == VK_TAB) {
                        app.Key(VK_TAB);
                        continue;
                    }
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            result = status < 0 ? 1 : static_cast<int>(message.wParam);
        }
        if (singleton)
            CloseHandle(singleton);
    }
    CoUninitialize();
    return result;
}
