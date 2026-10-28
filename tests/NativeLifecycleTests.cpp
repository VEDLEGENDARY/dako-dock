// Exercise the production renderer/message loop with inert, deterministic data.
#include "../src/Island.h"
#include <cstdio>
#include <cstdlib>

void RequireLive(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        throw std::runtime_error(message);
    }
}
struct Resources {
    DWORD handles = 0, gdi = 0, user = 0;
    SIZE_T privateBytes = 0, workingSet = 0;
    ULONGLONG cpu = 0;
};
Resources ResourcesNow() {
    Resources r;
    auto process = GetCurrentProcess();
    GetProcessHandleCount(process, &r.handles);
    r.gdi = GetGuiResources(process, GR_GDIOBJECTS);
    r.user = GetGuiResources(process, GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
        r.privateBytes = memory.PrivateUsage;
        r.workingSet = memory.WorkingSetSize;
    }
    FILETIME created{}, exit{}, kernel{}, user{};
    GetProcessTimes(process, &created, &exit, &kernel, &user);
    ULARGE_INTEGER a{}, b{};
    a.LowPart = kernel.dwLowDateTime;
    a.HighPart = kernel.dwHighDateTime;
    b.LowPart = user.dwLowDateTime;
    b.HighPart = user.dwHighDateTime;
    r.cpu = a.QuadPart + b.QuadPart;
    return r;
}
void PumpFor(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 4, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < end);
}
void Settle(Island &app) {
    ULONGLONG end = GetTickCount64() + 3000;
    while (app.scheduler.active && GetTickCount64() < end)
        PumpFor(10);
    RequireLive(!app.scheduler.active && !app.frameClock.Active(), "motion settles and releases its frame thread");
    RequireLive(!app.previousPane, "settled transition releases its outgoing bitmap");
}
void SaveFrame(Island &app, const std::filesystem::path &path) {
    std::vector<unsigned char> pixels;
    UINT width = 0, height = 0;
    if (FAILED(app.composition.ReadPixels(pixels, width, height)))
        return;
    ComPtr<IWICImagingFactory> images;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images)));
    ComPtr<IWICStream> stream;
    Check(images->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(images->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format));
    Check(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    Check(frame->Commit());
    Check(encoder->Commit());
}
int wmain(int argc, wchar_t **argv) {
    bool quick = argc > 2 && std::wstring(argv[2]) == L"--quick",
         recordOnly = argc > 2 && std::wstring(argv[2]) == L"--record-only";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    try {
        std::filesystem::path directory = argc > 1 ? argv[1] : L"build/live-verification";
        std::filesystem::create_directories(directory / L"frames");
        std::ofstream report(directory / L"results.txt", std::ios::trunc);
        Island app;
        app.renderMode = true;
        app.previewMode = true;
        app.fixtureGeometry = true;
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = WindowProcedure;
        type.lpszClassName = WindowClass;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&type);
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WindowClass,
                                      L"Dako native verification - inert fixtures", WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                                      900, 760, nullptr, nullptr, type.hInstance, &app);
        RequireLive(window != nullptr, "fixture window created");
        app.dpi = 96;
        app.textScale = 1;
        app.envelopeWidth = 900;
        app.envelopeHeight = 760;
        app.opaqueMaterial = true;
        app.systemAppearance = false;
        app.ConfigureAppearance();
        for (int i = 0; i < 5; ++i)
            app.tasks.push_back({L"Fixture task " + std::to_wstring(i + 1), i == 1, app.nextTask++});
        for (int i = 0; i < 30; ++i)
            app.apps.emplace_back(L"Fixture application " + std::to_wstring(i + 1), L"fixture:" + std::to_wstring(i));
        app.appPreferences.favorites = {L"fixture:0", L"fixture:2"};
        app.FilterApps();
        app.wallpapers = DesktopCatalog::LoadWallpapers(
            (std::filesystem::path(argv[0]).parent_path() / L"assets/wallpapers").wstring());
        RequireLive(!app.wallpapers.empty(), "bundled fixture pictures loaded");
        app.system.wifi = L"Fixture network";
        app.system.wifiConnected = true;
        app.system.bluetooth = L"Fixture headphones";
        app.system.audioName = L"Fixture audio output";
        app.system.volume = .42f;
        app.system.cpuValid = app.system.memoryValid = app.system.diskValid = app.system.networkValid = true;
        app.system.totalCpu = 37;
        app.system.memoryLoad = 61;
        app.system.availableGiB = 12.5;
        app.system.diskRoot = L"C:\\";
        app.system.diskTotalGiB = 512;
        app.system.diskUsedGiB = 256;
        app.currentMedia.title = L"Fixture music";
        app.currentMedia.artist = L"Fixture artist";
        app.currentMedia.available = app.currentMedia.canPlay = app.currentMedia.canPrevious =
            app.currentMedia.canNext = true;
        app.currentMedia.duration = 180;
        app.currentMedia.position = 40;
        WifiNetwork network;
        network.name = L"Fixture secured network";
        network.secure = network.connectable = true;
        network.quality = 80;
        app.connections.networks = {network};
        BluetoothEntry device;
        device.name = L"Fixture headphones";
        device.native.Address.ullLong = 1;
        app.connections.devices = {device};
        app.connections.wifiAvailable = app.connections.wifiOn = app.connections.bluetoothAvailable =
            app.connections.bluetoothOn = true;
        app.clipboard = L"Fixture text only. No real clipboard is read.";
        app.clipboardState = L"Fixture text";
        app.hardwareState.cpu = L"Fixture processor";
        app.hardwareState.gpu = L"Fixture graphics";
        app.hardwareState.top = {{L"Fixture process", 256, 111}, {L"Fixture process", 128, 222}};
        VerifyAccessibility(app);
        app.tab = Tab::Controls;
        app.renderMode = false;
        app.reducedMotion = false;
        app.diagnosticsPath = (directory / L"timing.txt").wstring();
        KillTimer(window, InteractionTimer);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        app.SetExpanded(true);
        Settle(app);
        RequireLive(app.composition.Active(), "native composition is active");
        report << "composition=1\nframe_readiness=" << (app.composition.FrameReady() != nullptr)
               << "\nfallback_timer_ms=" << app.motionInterval << "\n";
        // Use the production mouse route without pumping a frame between clicks.
        // Previously Actionable() rejected all of these while springs were moving.
        auto click = [&](int id) {
            app.RebuildLayout();
            auto found = std::find_if(app.hits.begin(), app.hits.end(), [&](const Hit &h) { return h.id == id; });
            RequireLive(found != app.hits.end(), "rapid-click target exists");
            int x = static_cast<int>((app.PanelX() + (found->rect.left + found->rect.right) / 2 * app.textScale) *
                                     app.dpi / 96),
                y = static_cast<int>((app.PanelY() + (found->rect.top + found->rect.bottom) / 2 * app.textScale) *
                                     app.dpi / 96);
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
            SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        };
        for (int i = 0; i < 28; ++i) {
            int view = static_cast<int>(PrimaryTabs[(i * 3 + 1) % PrimaryTabs.size()]);
            click(100 + view);
            RequireLive(app.tab == static_cast<Tab>(view) && app.Actionable(),
                        "every rapid navigation click is accepted during motion");
            RequireLive(!app.previousPane, "rapid navigation never composites an outgoing pane");
        }
        click(101);
        bool running = app.focusRunning;
        click(FocusStart);
        RequireLive(app.focusRunning != running, "content buttons respond before geometry settles");
        click(FocusStart);
        app.EnsureTaskEdit();
        app.RebuildLayout();
        app.PositionInputs();
        RequireLive(IsWindowVisible(app.taskEdit), "native fields are available during panel motion");
        auto savedTasks = app.tasks;
        while (app.tasks.size() < 20)
            app.tasks.push_back({L"Keyboard task " + std::to_wstring(app.tasks.size()), false, app.nextTask++});
        app.RebuildLayout();
        app.FocusAt(app.IndexForKey(app.StableKey(219)));
        app.RebuildLayout();
        int lastTask = app.IndexForKey(app.StableKey(219));
        RequireLive(lastTask >= 0 && app.VisibleHit(app.hits[lastTask]) && app.taskScroll > 0,
                    "keyboard focus scrolls the twentieth task into its card");
        app.Invoke(239);
        RequireLive(app.tasks.size() == 19, "twentieth task delete is wired");
        app.tasks = savedTasks;
        app.taskScroll = 0;
        app.RebuildLayout();
        auto savedPins = app.appPreferences.favorites;
        app.ReorderPin(0, 1);
        RequireLive(app.appPreferences.favorites.size() == savedPins.size() &&
                        app.appPreferences.favorites[1] == savedPins[0],
                    "pinned app reorder preserves identities");
        app.appPreferences.favorites = savedPins;
        app.FilterApps(false);
        for (Tab primary : PrimaryTabs) {
            app.SetTab(primary);
            RequireLive(app.SurfaceHeight() == 390, "all seven tabs share the V4 fixed surface height");
        }
        app.SetTab(Tab::Today);
        report << "twenty_task_keyboard_scroll_delete=PASS\nfixed_seven_tab_height=PASS\n";
        RequireLive(app.HitAt(app.PanelX() - 8, app.PanelY() + 100) == -1,
                    "transparent envelope cannot intercept a control hit");
        app.SetTab(Tab::Wallpaper);
        app.wallpaperFocus.Snap(2.5f);
        app.RebuildLayout();
        for (const auto &a : app.hits)
            for (const auto &b : app.hits)
                if (a.id >= 500 && a.id < 564 && b.id > a.id && b.id < 564)
                    RequireLive(!layout::Intersects({a.rect.left, a.rect.top, a.rect.right, a.rect.bottom},
                                                    {b.rect.left, b.rect.top, b.rect.right, b.rect.bottom}),
                                "wallpaper grid cards retain non-overlapping hit bounds");
        app.wallpaperFocus.Snap(0);
        app.SetTab(Tab::Controls);
        Settle(app);
        RequireLive(Island::Contrast(theme::Text, theme::Surface) >= 7 &&
                        Island::Contrast(theme::Muted, theme::Card) >= 4.5 &&
                        Island::Contrast(theme::AccentInk, theme::Accent) >= 4.5 &&
                        Island::Contrast(theme::SelectedText, theme::AccentSoft) >= 4.5,
                    "portfolio theme text retains readable contrast");
        report << "rapid_mouse_navigation_28=PASS\ncontent_during_motion=PASS\nfields_during_motion=PASS\nwallpaper_"
                  "grid_gutters=PASS\nportfolio_contrast=PASS\n";
        app.system.wifiConnected = false;
        app.connections.devices[0].connected = true;
        InvalidateRect(window, nullptr, FALSE);
        PumpFor(40);
        Settle(app);
        RequireLive(app.tileState[Wifi].value == 0 && app.tileState[Bluetooth].value == 1,
                    "connected-state feedback settles independently of pointer hover");
        app.system.wifiConnected = true;
        app.connections.devices[0].connected = false;
        InvalidateRect(window, nullptr, FALSE);
        PumpFor(40);
        Settle(app);
        for (int i = 0; i < 1000; ++i)
            app.feedback[L"fixture-finished:" + std::to_wstring(i)] = {};
        app.ArmMotion();
        PumpFor(20);
        Settle(app);
        RequireLive(app.feedback.size() < 16, "settled one-shot feedback does not grow across distinct items");
        // A stalled frame and reduced-motion collapse must release the same resources.
        app.SetExpanded(false);
        app.lastMotion = steady_clock::now() - std::chrono::seconds(1);
        app.Transition();
        RequireLive(!app.scheduler.active && !app.target && !app.composition.Active(),
                    "stalled collapse releases GPU targets");
        app.SetExpanded(true);
        Settle(app);
        app.reducedMotion = true;
        app.SetExpanded(false);
        RequireLive(!app.target && !app.composition.Active(), "reduced-motion collapse releases GPU targets");
        app.reducedMotion = false;
        app.SetExpanded(true);
        Settle(app);
        report << "stalled_and_reduced_motion_gpu_release=PASS\n";
        // Record our own transparent swapchain only. Recording is excluded from timing.
        ULONGLONG lastCapture = 0;
        size_t frames = 0;
        app.beforePresent = [&] {
            auto now = GetTickCount64();
            if (now - lastCapture >= 32 && frames < 220) {
                lastCapture = now;
                SaveFrame(app, directory / L"frames" / (L"frame-" + std::to_wstring(10000 + frames++) + L".png"));
            }
        };
        for (int view : (quick ? std::vector<int>{0} : std::vector<int>{0, 1, 4, 2, 5, 0})) {
            app.SetTab(static_cast<Tab>(view));
            if (view == 4) {
                app.railPointer.goal = app.LayoutWidth() / 2;
                app.railStrength.goal = 1;
            }
            if (view == 5) {
                app.wallpaperParallax.goal = .7f;
                app.SetAccent(0x83B8DE);
            }
            PumpFor(420);
        }
        if (!quick) {
            app.OpenFlyout(Flyout::Wifi);
            app.selectedWifi = 0;
            PumpFor(420);
            app.OpenFlyout(Flyout::Bluetooth);
            app.selectedBluetooth = 0;
            PumpFor(420);
            app.SetTab(Tab::Controls);
            app.miniMode = true;
            app.SetExpanded(true);
            PumpFor(420);
            Settle(app);
            app.miniMode = false;
            app.SetExpanded(true);
        }
        Settle(app);
        app.beforePresent = {};
        report << "recorded_swapchain_frames=" << frames << "\n";
        app.frameIntervals.clear();
        app.paintTimes.clear();
        app.tab = Tab::Controls;
        app.SetExpanded(true);
        Settle(app);
        DXGI_FRAME_STATISTICS previous{};
        HRESULT statisticsResult = app.composition.Statistics(&previous);
        report << "frame_statistics_hresult=" << static_cast<unsigned long>(statisticsResult) << "\n";
        DEVMODEW displayMode{};
        displayMode.dmSize = sizeof(displayMode);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &displayMode))
            report << "display_refresh_hz=" << displayMode.dmDisplayFrequency << "\n";
        auto baseline = ResourcesNow();
        auto sample = [&](const char *name) {
            auto r = ResourcesNow();
            report << name << " handles=" << r.handles << " gdi=" << r.gdi << " user=" << r.user
                   << " private_mib=" << r.privateBytes / 1048576.0 << " working_set_mib=" << r.workingSet / 1048576.0
                   << std::endl;
            return r;
        };
        sample("warm_open");
        std::vector<double> presented, averaged;
        bool validStatistics = false;
        size_t disjoint = 0;
        UINT refresh = displayMode.dmDisplayFrequency ? displayMode.dmDisplayFrequency : 60;
        app.beforePresent = [&] {
            DXGI_FRAME_STATISTICS current{};
            HRESULT status = app.composition.Statistics(&current);
            if (FAILED(status) || !current.SyncQPCTime.QuadPart || !current.PresentCount ||
                !current.PresentRefreshCount) {
                validStatistics = false;
                ++disjoint;
                return;
            }
            if (validStatistics && current.PresentCount > previous.PresentCount &&
                current.PresentRefreshCount >= previous.PresentRefreshCount) {
                UINT images = current.PresentCount - previous.PresentCount,
                     vblanks = current.PresentRefreshCount - previous.PresentRefreshCount;
                double interval = 1000.0 * vblanks / refresh / images;
                if (vblanks) {
                    if (images == 1)
                        presented.push_back(interval);
                    else
                        averaged.push_back(interval);
                }
            }
            previous = current;
            validStatistics = true;
        };
        // 50 complete close/open cycles, all views, rapid reversals and live native EDITs.
        for (int cycle = 0; cycle < (recordOnly ? 0 : quick ? 1 : 50); ++cycle) {
            app.system.wifiConnected = cycle % 2 == 0;
            app.connections.devices[0].connected = cycle % 2 != 0;
            app.contentBlend.Snap(0);
            app.contentBlend.goal = 1;
            validStatistics = false;
            app.SetExpanded(true);
            for (int v : {0, 1, 4, 2, 5, 0}) {
                app.SetTab(static_cast<Tab>(v));
                PumpFor(24);
            }
            if (!quick) {
                app.OpenFlyout(Flyout::Wifi);
                app.selectedWifi = 0;
                PumpFor(24);
                app.OpenFlyout(Flyout::Bluetooth);
                app.selectedBluetooth = 0;
                PumpFor(24);
                app.SetTab(Tab::Controls);
                app.miniMode = true;
                app.SetExpanded(true);
                PumpFor(24);
                app.miniMode = false;
                app.SetTab(Tab::Controls);
            }
            Settle(app);
            app.SetExpanded(false);
            Settle(app);
            RequireLive(!app.target && !app.composition.Active(), "collapse releases GPU targets");
            if ((cycle + 1) % 10 == 0) {
                auto r = sample(("cycle_" + std::to_string(cycle + 1)).c_str());
                RequireLive(r.handles <= baseline.handles + 12 && r.gdi <= baseline.gdi + 4 &&
                                r.user <= baseline.user + 8,
                            "post-collapse OS resources stay bounded");
            }
        }
        app.beforePresent = {};
        app.SetTab(Tab::Stats);
        Settle(app);
        RequireLive(app.rowPositions.contains(L"process:111") && app.rowPositions.contains(L"process:222") &&
                        app.rowPositions[L"process:111"].value < app.rowPositions[L"process:222"].value,
                    "duplicate process labels retain distinct PID geometry");
        app.tasks.pop_back();
        app.SetTab(Tab::Today);
        Settle(app);
        app.EnsureTaskEdit();
        app.RebuildLayout();
        app.PositionInputs();
        HWND edit = app.taskEdit;
        SetWindowTextW(edit, L"Fixture draft - mixed scripts \u65e5\u672c\u8a9e");
        SendMessageW(edit, EM_SETSEL, 2, 8);
        for (float scale : {1.25f, 1.5f, 2.0f, 2.25f}) {
            app.textScale = scale;
            app.envelopeWidth = 683;
            app.envelopeHeight = 272;
            app.dpi = 192;
            app.RetargetLayout();
            app.SnapMotion();
            app.RebuildLayout();
            auto hit = std::find_if(app.hits.begin(), app.hits.end(), [](const Hit &h) { return h.id == 400; });
            RequireLive(hit != app.hits.end(), "composer remains in traversal");
            app.FocusAt(static_cast<int>(hit - app.hits.begin()));
            if (!IsWindowVisible(edit)) {
                auto index = app.IndexForKey(app.StableKey(400));
                if (index >= 0) {
                    const auto &h = app.hits[index];
                    std::fprintf(stderr,
                                 "input scale=%f scroll=%f top=%f bottom=%f clip=%f,%f enabled=%d transition=%d\n",
                                 scale, app.scroll, h.rect.top, h.rect.bottom, h.clip.top, h.clip.bottom, h.enabled,
                                 app.transitioning);
                }
            }
            RequireLive(IsWindowVisible(edit), "large-text composer scrolls completely into view");
            RECT actual{};
            GetWindowRect(edit, &actual);
            auto logical = app.hits[app.IndexForKey(app.StableKey(400))].rect;
            RequireLive(std::abs((actual.bottom - actual.top) - (logical.bottom - logical.top) * scale * 2) <= 1,
                        "native EDIT shares DPI and text-scale geometry");
            DWORD first = 0, last = 0;
            SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            RequireLive(first == 2 && last == 8, "text reflow retains native selection");
        }
        app.textScale = 1;
        app.dpi = 96;
        app.envelopeWidth = 900;
        app.envelopeHeight = 760;
        app.SetTab(Tab::Apps);
        Settle(app);
        app.EnsureAppSearch();
        app.RebuildLayout();
        auto search = std::find_if(app.hits.begin(), app.hits.end(), [](const Hit &h) { return h.id == 401; });
        RequireLive(search != app.hits.end(), "search participates in traversal");
        app.FocusAt(static_cast<int>(search - app.hits.begin()));
        SetWindowTextW(app.appSearch, L"Fixture application 30");
        SendMessageW(app.appSearch, EM_SETSEL, 3, 8);
        PumpFor(40);
        RequireLive(GetFocus() == app.appSearch && IsWindowVisible(app.appSearch),
                    "filter resize retains native search focus during motion");
        Settle(app);
        DWORD selectionStart = 0, selectionEnd = 0;
        SendMessageW(app.appSearch, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                     reinterpret_cast<LPARAM>(&selectionEnd));
        RequireLive(selectionStart == 3 && selectionEnd == 8 && app.appQuery == L"Fixture application 30",
                    "filter resize retains query and selection");
        app.textScale = 1;
        app.dpi = 96;
        app.envelopeWidth = 900;
        app.envelopeHeight = 760;
        app.RefreshGeometry();
        // Exercise the production actions with fixtures and prove the Windows write boundary stays closed.
        auto themeValues = [] {
            std::array<DWORD, 2> values{MAXDWORD, MAXDWORD};
            const wchar_t *names[]{L"AppsUseLightTheme", L"SystemUsesLightTheme"};
            for (int i = 0; i < 2; ++i) {
                DWORD size = sizeof(DWORD);
                RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                             names[i], RRF_RT_REG_DWORD, nullptr, &values[i], &size);
            }
            return values;
        };
        auto desktopImages = [] {
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
        };
        auto themeBefore = themeValues();
        auto wallpaperBefore = desktopImages();
        app.SetTab(Tab::Controls);
        Settle(app);
        app.controlState.brightnessSupported = true;
        app.controlState.brightness = 40;
        app.controlState.radiosAvailable = true;
        app.controlState.radiosOff = false;
        app.controlState.dark = false;
        app.RebuildLayout();
        app.Invoke(Dark);
        RequireLive(app.controlState.dark, "dark fixture toggles without touching Windows");
        app.Invoke(Airplane);
        RequireLive(app.controlState.radiosOff, "radio fixture toggles without accessing hardware");
        app.FocusAt(app.IndexForKey(app.StableKey(Brightness)));
        app.Key(VK_RIGHT);
        RequireLive(app.controlState.brightness == 45, "brightness supports five-percent keyboard steps in fixtures");
        app.controlState.brightnessSupported = false;
        app.RebuildLayout();
        RequireLive(app.IndexForKey(app.StableKey(Brightness)) < 0,
                    "unsupported brightness removes its hit and keyboard stop");
        app.controlState.outputs = {{L"fixture:out", L"Fixture output", true}};
        app.Invoke(Audio);
        Settle(app);
        RequireLive(app.flyout == Flyout::Audio && app.IndexForKey(app.StableKey(OutputSettings)) >= 0,
                    "audio output list keeps an explicit Windows Sound settings action");
        app.SetTab(Tab::Stats);
        Settle(app);
        app.Invoke(StatsSort);
        RequireLive(app.statsMemory, "process ranking can switch to memory");
        app.Invoke(StatsSort);
        app.SetTab(Tab::Wallpaper);
        Settle(app);
        app.wallpaperDisplays = {{L"fixture:a", L"Display 1", L"", {}}, {L"fixture:b", L"Display 2", L"", {}}};
        app.wallpaperTarget = L"fixture:a";
        app.Invoke(WallpaperTarget);
        RequireLive(app.wallpaperTarget == L"fixture:b", "wallpaper target cycles by stable display ID");
        auto priorFit = app.wallpaperFit;
        app.Invoke(WallpaperFit);
        RequireLive(app.wallpaperFit != priorFit, "fit selection stays pending until explicit Apply");
        app.Invoke(WallpaperApply);
        RequireLive(!app.wallpaperApplying && !app.nativeControls,
                    "fixture actions never start hardware workers, native wallpaper changes or desktop captures");
        RequireLive(themeValues() == themeBefore && desktopImages() == wallpaperBefore,
                    "fixture interactions leave Windows theme, images and fit unchanged");
        report << "v32_inert_controls_wallpaper_keyboard=PASS\nunsupported_brightness_hidden=PASS\nwindows_settings_"
                  "unchanged=PASS\n";
        app.OpenFeatures();
        Settle(app);
        RequireLive(app.flyout == Flyout::Features && !app.statsTicking && !app.connectionsTicking &&
                        !app.mediaTicking && app.moduleCosts.empty(),
                    "Features stops hidden sampling and does not sample costs on open");
        app.Invoke(2920);
        RequireLive(app.moduleRegistry.Enabled(L"windows"), "Features enables the module in preview memory");
        app.OpenPalette();
        app.paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
        app.BuildPalette();
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().moduleAction == L"windows.display",
                    "enabled module appears in the shared palette");
        app.InvokeModuleAction(L"windows.open");
        Settle(app);
        RequireLive(app.flyout == Flyout::Module && app.modulePanel.rows.size() >= 32 &&
                        app.IndexForKey(app.StableKey(5100)) >= 0 && !IsWindowVisible(app.appSearch),
                    "module opens inside the existing island and hides the palette edit field");
        app.modulePanel.rows.clear();
        app.RebuildLayout();
        int empty = app.IndexForKey(app.StableKey(7220));
        RequireLive(empty >= 0, "empty module view exposes a named readout");
        VARIANT emptyChild{}, emptyRole{};
        emptyChild.vt = VT_I4;
        emptyChild.lVal = app.hits[empty].child;
        Check(app.accessible->get_accRole(emptyChild, &emptyRole));
        RequireLive(emptyRole.lVal == ROLE_SYSTEM_STATICTEXT, "empty module view has a native readonly MSAA role");
        app.Key(VK_ESCAPE);
        Settle(app);
        app.OpenFeatures();
        app.Invoke(2920);
        app.OpenPalette();
        app.paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
        app.BuildPalette();
        RequireLive(app.paletteItems.empty(), "removing a module removes its palette actions immediately");
        app.OpenFeatures();
        app.Invoke(2904);
        RequireLive(app.moduleCosts.empty(), "cost page waits for an explicit sample");
        app.Invoke(2940);
        RequireLive(app.moduleCosts.contains(L"windows") && app.moduleCosts[L"windows"].ownedBytes == 0,
                    "cost refresh reports zero live allocations for a disabled module");
        app.Invoke(2903);
        bool offline = privacy::Policy().Offline();
        app.Invoke(2941);
        RequireLive(privacy::Policy().Offline() != offline && app.privacySnapshot.offline != offline,
                    "Offline toggle updates the policy and accessible view state");
        app.Invoke(2941);
        RequireLive(privacy::Policy().Offline() == offline, "fixture restores its original Offline policy");
        app.Key(VK_ESCAPE);
        Settle(app);
        RequireLive(app.flyout == Flyout::None, "Escape returns from Features to the island");
        report << "s0_features_palette_toggle_cost_sampling_keyboard=PASS\n";
        app.moduleRegistry.Enable(L"windows");
        app.OpenPalette();
        app.paletteQuery = L"ms-settings:privacy-webcam"; app.BuildPalette();
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().moduleAction.starts_with(L"windows.page-result-"),
                    "typed Settings link shares the palette catalog");
        auto ranksBeforeSettings = app.commandCatalog.Save();
        app.ExecutePalette(0); Settle(app);
        RequireLive(app.commandCatalog.Save() == ranksBeforeSettings, "typed Settings targets never enter learned ranking");
        app.OpenPalette(); Settle(app);
        std::wstring appUri = L"ms-settings:defaultapps?registeredAppUser=" + std::wstring(200, L'a') + L"%20Tools";
        auto editName = [&] {
            ComPtr<IAccessible> accessible;
            RequireLive(SUCCEEDED(AccessibleObjectFromWindow(app.appSearch, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(&accessible))),
                        "native search edit exposes accessibility");
            VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
            BSTR label = nullptr; HRESULT named = accessible->get_accName(self, &label);
            std::wstring result = label ? std::wstring(label, SysStringLen(label)) : std::wstring{};
            SysFreeString(label); RequireLive(SUCCEEDED(named), "native search edit has a name"); return result;
        };
        RequireLive(editName() == modules::String(IDS_PALETTE_NAME), "native edit is named for commands while the palette is open");
        SendMessageW(app.appSearch, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(appUri.c_str())); Settle(app);
        RequireLive(app.paletteQuery == appUri && app.paletteItems.size() == 1 &&
            SendMessageW(app.appSearch, EM_GETLIMITTEXT, 0, 0) == 4096,
            "native palette accepts a complete context link beyond the old app-search limit");
        app.ExecutePalette(0); Settle(app);
        RequireLive(app.commandCatalog.Save() == ranksBeforeSettings, "parameter targets remain absent from learned ranking");
        SetWindowTextW(app.appSearch, L"");
        std::wstring excessive = L"ms-settings:camera?cameraId=" + std::wstring(1100, L'x');
        SendMessageW(app.appSearch, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(excessive.c_str())); Settle(app);
        RequireLive(app.paletteQuery == excessive && app.paletteItems.empty(), "oversized pasted targets are rejected without truncating into a valid URI");
        SetWindowTextW(app.appSearch, L"ms-settings:defaultapps?registeredAUMID=Example!App"); Settle(app);
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().label.find(L"Example%21App") != std::wstring::npos,
                    "canonical escaping remains discoverable from the typed spelling");
        app.SetTab(Tab::Apps); Settle(app);
        RequireLive(SendMessageW(app.appSearch, EM_GETLIMITTEXT, 0, 0) == 120, "ordinary app search retains its original bound");
        RequireLive(editName() == L"Search installed apps", "native edit restores the app-search accessible name");
        app.OpenPalette(); Settle(app); app.OpenFeatures(); Settle(app); app.SetTab(Tab::Apps); Settle(app);
        RequireLive(editName() == L"Search installed apps", "indirect navigation through Features restores the native edit name");
        report << "s1_settings_parameters_native_input_canonical_inert=PASS\n";
        app.InvokeModuleAction(L"windows.open"); Settle(app);
        app.moduleQuery = L"ms-settings:privacy-webcam";
        app.modulePanel = *app.moduleRegistry.View(L"windows", app.moduleQuery);
        app.RebuildLayout(); Settle(app);
        RequireLive(app.modulePanel.rows.size() == 1 && app.modulePanel.rows.front().label.find(L"ms-settings:privacy-webcam") != std::wstring::npos,
                    "typed Settings link also appears in the shared module view");
        for (const auto &[command, label] : {std::pair{static_cast<int>(SleepNow), IDS_COMMAND_SLEEP},
                                             std::pair{static_cast<int>(RestartNow), IDS_COMMAND_RESTART}}) {
            app.OpenPalette(); app.paletteQuery = modules::String(label); app.BuildPalette();
            RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().action == command, "system command has one palette route");
            app.ExecutePalette(0); Settle(app);
            RequireLive(app.note == modules::String(IDS_POWER_INERT), "power commands refuse fixtures before any dialog or system action");
        }
        bool beforeDark = app.controlState.dark;
        app.OpenPalette(); app.paletteQuery = modules::String(IDS_COMMAND_DARK); app.BuildPalette();
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().action == Dark, "dark-mode command shares the existing control action");
        app.ExecutePalette(0); Settle(app);
        RequireLive(app.controlState.dark != beforeDark, "inert dark-mode routing changes only the fixture presentation");
        app.controlState.dark = beforeDark;
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
        report << "s1_settings_links_and_inert_system_commands=PASS\n";
        for (const auto *id : {L"utilities", L"clipboard", L"snippets", L"keep-awake", L"files"}) {
            app.moduleRegistry.Grant(id);
            RequireLive(app.moduleRegistry.Enable(id), "S1 fixture module starts");
        }
        app.OpenPalette();
        app.paletteQuery = L"2*(3+4)";
        app.BuildPalette();
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().label.find(L"14") != std::wstring::npos,
                    "palette calculator result is searchable");
        app.ExecutePalette(0); Settle(app);
        RequireLive(app.moduleView == L"utilities" && app.flyout == Flyout::Module,
                    "calculator reuses the island");
        app.InvokeModuleAction(L"clipboard.open"); Settle(app);
        RequireLive(!app.clipboardReadTask.Busy() && app.moduleRegistry.Capabilities().Active().empty(),
                    "S1 render fixtures never read clipboard or create execution requests");
        for (const auto &action : app.moduleRegistry.Actions())
            if (action.id.starts_with(L"clipboard.preview-") && action.text.find(modules::String(IDS_CLIP_IMAGE)) != std::wstring::npos) {
                app.InvokeModuleAction(action.id); break;
            }
        Settle(app);
        RequireLive(app.modulePanel.image && app.IndexForKey(app.StableKey(7221)) >= 0,
                    "clipboard image preview has a named accessible readout");
        app.InvokeModuleAction(L"clipboard.clear"); Settle(app);
        RequireLive(!app.modulePanel.image, "clearing history releases the image preview");
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
        app.OpenFeatures();
        report << "s1_calculator_clipboard_image_inert_lifecycle=PASS\n";
        app.moduleRegistry.Grant(L"keep-awake");
        RequireLive(app.moduleRegistry.Enable(L"keep-awake"), "keep-awake fixture starts");
        app.OpenModule(L"keep-awake"); Settle(app);
        RequireLive(app.moduleRegistry.Invoke(L"keep-awake.rule-apps", window, L"editor.exe; player.exe"),
                    "rule names save in fixture memory");
        app.OpenModule(L"keep-awake");
        app.modulePage = 10 / app.ModuleSlots(); app.RebuildLayout();
        RequireLive(app.IndexForKey(app.StableKey(5100 + static_cast<int>(10 % app.ModuleSlots()))) >= 0,
                    "selected app rule has a keyboard/accessibility action");
        RequireLive(!app.moduleRegistry.Invoke(L"keep-awake.rules-start", window) &&
                    app.moduleRegistry.Capabilities().Active().empty(), "preview rule start remains inert");
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
        report << "s1_keep_awake_rules_inert_accessible=PASS\n";
        app.moduleRegistry.Grant(L"files");
        RequireLive(app.moduleRegistry.Enable(L"files"), "indexed file fixture starts");
        app.OpenPalette();
        app.paletteQuery = modules::String(IDS_FILES_KEYWORD) + L" report";
        app.BuildPalette();
        RequireLive(!app.paletteItems.empty() && app.paletteItems.front().moduleAction.starts_with(L"files.result-"),
                    "indexed file results share the command palette");
        app.OpenFeatures(); Settle(app);
        RequireLive(app.paletteItems.empty() && app.paletteQuery.empty(), "leaving the palette clears private search results");
        app.InvokeModuleAction(L"files.open"); Settle(app);
        app.moduleQuery = L"report";
        app.modulePanel = *app.moduleRegistry.View(L"files", app.moduleQuery);
        app.RebuildLayout(); Settle(app);
        RequireLive(app.modulePanel.rows.front().label.find(L"report") != std::wstring::npos &&
                        app.moduleRegistry.Capabilities().Active().empty(), "file island uses inert local fixtures");
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
        report << "s1_indexed_files_inert_lifecycle=PASS\n";
        app.moduleRegistry.Grant(L"shelf");
        RequireLive(app.moduleRegistry.Enable(L"shelf"), "shelf fixture starts");
        app.InvokeModuleAction(L"shelf.open"); Settle(app);
        RequireLive(app.flyout == Flyout::Module && app.moduleView == L"shelf", "shelf reuses the island surface");
        SendMessageW(window, WM_ACTIVATE, WA_INACTIVE, 0);
        RequireLive(app.expanded, "shelf stays available when another app receives focus for dragging");
        app.InteractionTick();
        RequireLive(app.expanded && app.moduleRegistry.Capabilities().Active().empty(), "inert shelf has no polling or live drop registration");
        for (const auto &action : app.moduleRegistry.Actions()) if (action.id.starts_with(L"shelf.preview-")) {
            app.InvokeModuleAction(action.id); break;
        }
        Settle(app);
        RequireLive(app.modulePanel.rows.front().action.starts_with(L"shelf.copy-"), "shelf detail offers copy/drag");
        // Only the fixture hit is enabled here; the real module remains inert and rejects clipboard/drag IO.
        app.modulePanel.rows.front().enabled = true;
        app.RebuildLayout();
        int copyHit = app.IndexForKey(app.StableKey(5100));
        RequireLive(copyHit >= 0, "shelf copy hit is present");
        auto copyRect = app.hits[copyHit].rect;
        int copyX = static_cast<int>((app.PanelX() + (copyRect.left + copyRect.right) / 2 * app.textScale) * app.dpi / 96);
        int copyY = static_cast<int>((app.PanelY() + (copyRect.top + copyRect.bottom) / 2 * app.textScale) * app.dpi / 96);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(copyX, copyY));
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(copyX + 1, copyY));
        RequireLive(app.pressedId == 5100 && !app.pressedKey.empty(), "small pointer motion preserves a normal click");
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(copyX + 64, copyY));
        RequireLive(app.pressedId == 0 && app.pressedKey.empty() && GetCapture() != window &&
            app.moduleRegistry.Capabilities().Active().empty(), "shelf drag threshold releases capture without invoking inert copy IO");
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(copyX + 64, copyY));
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
        report << "s1_shelf_inert_surface_lifecycle=PASS\n";
        app.moduleRegistry.Grant(L"recycle-bin"); app.moduleRegistry.Grant(L"drives");
        RequireLive(app.moduleRegistry.Enable(L"recycle-bin") && app.moduleRegistry.Enable(L"drives"), "system fixtures enable");
        app.InvokeModuleAction(L"recycle-bin.open"); Settle(app);
        RequireLive(app.moduleView == L"recycle-bin" && app.modulePanel.rows.size() >= 6 &&
            app.moduleRegistry.Capabilities().Active().empty(), "Recycle Bin uses synthetic totals in the shared island");
        app.OpenPalette(); app.paletteQuery = modules::String(IDS_DRIVE_EJECT); app.BuildPalette();
        RequireLive(app.paletteItems.size() == 1 && app.paletteItems.front().moduleAction.starts_with(L"drives.eject-"),
                    "removable target uses the common palette");
        auto beforeDeviceRanks = app.commandCatalog.Save();
        app.ExecutePalette(0); Settle(app);
        RequireLive(app.commandCatalog.Save() == beforeDeviceRanks && app.moduleRegistry.Capabilities().Active().empty(),
                    "synthetic device action cannot prompt/eject or enter learned ranking");
        app.InvokeModuleAction(L"drives.open"); Settle(app);
        RequireLive(app.moduleView == L"drives" && app.modulePanel.rows.size() >= 4,
                    "removable metadata shares island layout and accessibility");
        app.moduleRegistry.ApplyPreset(modules::Preset::Minimal); app.OpenFeatures();
        report << "s1_system_metadata_inert_surfaces=PASS\n";
        std::vector<double> paletteLatency;
        for (int attempt = 0; attempt < 11; ++attempt) {
            app.SetExpanded(false);
            auto begin = steady_clock::now();
            SendMessageW(window, WM_HOTKEY, 3, 0);
            UpdateWindow(window);
            double elapsed = duration<double, std::milli>(steady_clock::now() - begin).count();
            if (attempt) paletteLatency.push_back(elapsed);
        }
        std::sort(paletteLatency.begin(), paletteLatency.end());
        report << "warm_palette_hotkey_to_paint_p95_ms=" << paletteLatency[8]
               << "\nwarm_palette_hotkey_to_paint_max_ms=" << paletteLatency.back() << '\n';
        RequireLive(paletteLatency[8] < 100, "warm palette hotkey-to-paint p95 stays below 100ms in the native fixture");
        app.SetExpanded(false);
        Settle(app);
        sample("warm_rest");
        auto before = ResourcesNow();
        auto started = GetTickCount64();
        PumpFor(quick || recordOnly ? 100 : 60000);
        auto after = sample(quick || recordOnly ? "warm_rest_short" : "warm_rest_60s");
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        double seconds = (GetTickCount64() - started) / 1000.0,
               cpu = (after.cpu - before.cpu) / 10000000.0 / seconds / system.dwNumberOfProcessors * 100;
        report << "warm_rest_seconds=" << seconds << "\nnormalized_cpu_percent=" << cpu << "\n";
        auto quantile = [](std::vector<double> values, double q) {
            if (values.empty())
                return -1.0;
            std::sort(values.begin(), values.end());
            return values[static_cast<size_t>(q * (values.size() - 1))];
        };
        report << "tick_p50_ms=" << quantile(app.frameIntervals, .5)
               << "\ntick_p95_ms=" << quantile(app.frameIntervals, .95)
               << "\ntick_p99_ms=" << quantile(app.frameIntervals, .99)
               << "\npaint_p95_ms=" << quantile(app.paintTimes, .95) << "\npresentation_samples=" << presented.size()
               << "\npresented_vblank_interval_p50_ms=" << quantile(presented, .5)
               << "\npresented_vblank_interval_p95_ms=" << quantile(presented, .95)
               << "\npresented_vblank_interval_p99_ms=" << quantile(presented, .99)
               << "\nmultiple_present_groups=" << averaged.size() << "\ninvalid_or_disjoint_statistics=" << disjoint
               << "\n";
        ULONGLONG shutdownStart = GetTickCount64();
        for (int i = 0; i < 10; ++i) {
            auto service = std::make_unique<MediaService>(window);
            PumpFor(10);
            service.reset();
            auto wireless = std::make_unique<Connectivity>(window);
            wireless->RefreshWifi(false);
            wireless->RefreshBluetooth(false);
            PumpFor(10);
            wireless.reset();
        }
        report << "read_only_service_shutdown_10_cycles_ms=" << GetTickCount64() - shutdownStart << "\n";
        RequireLive(GetTickCount64() - shutdownStart < 15000,
                    "real read-only native service shutdown stays bounded in this run");
        RequireLive(quick || recordOnly || cpu < .1, "60-second warm rest stays below 0.1 percent normalized CPU");
        report << "all_seven_tabs_wireless_compact=" << !quick
               << "\nsearch_focus_query_caret=PASS\nlarge_text_native_fields=PASS\n";
        report << "cycles="
               << (recordOnly ? 0
                   : quick    ? 1
                              : 50)
               << "\n"
               << (recordOnly ? "RECORDING "
                   : quick    ? "QUICK "
                              : "")
               << "PASS: native composition cycles, interruption, GPU release, resource bounds, text-scale EDIT/caret "
                  "and reported rest interval. Fixture-only: no hardware actions or screen-reader sign-off.\n";
        // Exercise production bar-to-island routing without reserving desktop space or saving preferences.
        auto workAreas = [] {
            std::vector<RECT> areas;
            EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                    MONITORINFO info{sizeof(info)};
                    if (GetMonitorInfoW(monitor, &info))
                        reinterpret_cast<std::vector<RECT> *>(context)->push_back(info.rcWork);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&areas));
            return areas;
        };
        auto desktopAreasBefore = workAreas();
        app.controlState.brightnessSupported = false;
        app.fixtureGeometry = false;
        app.dockMode = true;
        app.previewMode = true;
        app.desktopRequested = false;
        app.dpi = static_cast<float>(GetDpiForWindow(window));
        app.textScale = 1;
        if (app.editFont) {
            DeleteObject(app.editFont);
            app.editFont = nullptr;
        }
        app.EnsureEditResources();
        for (HWND input : {app.taskEdit, app.appSearch, app.wifiPassword, app.clipboardEdit})
            if (input)
                SendMessageW(input, WM_SETFONT, reinterpret_cast<WPARAM>(app.editFont), TRUE);
        app.StartDesktop();
        RequireLive(app.docks && app.docks->Count() == desktopAreasBefore.size(),
                    "production preview creates one bar per monitor");
        HMONITOR panelMonitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        for (int action : {desktop::DockApps, desktop::DockSearch, desktop::DockClock, desktop::DockWifi,
                           desktop::DockVolume, desktop::DockBattery, desktop::DockWeather}) {
            app.SetExpanded(false);
            Settle(app);
            app.OpenFromDock(action, panelMonitor);
            Settle(app);
            RequireLive(app.expanded, "bar widgets open the production island");
            RECT ownerBounds{};
            GetWindowRect(window, &ownerBounds);
            auto bar = app.docks->PillBounds(panelMonitor);
            float scale = app.dpi / 96;
            float bottom = ownerBounds.top + (app.PanelY() + app.height) * scale;
            RequireLive(bottom <= bar.top + 1, "production island opens above its monitor bar");
            RequireLive(app.SurfaceWidth() <= app.envelopeWidth + .1f,
                        "production bar island stays inside its monitor envelope");
            if (action == desktop::DockSearch) {
                RequireLive(app.flyout == Flyout::Palette && app.appSearch && IsWindowVisible(app.appSearch) &&
                                GetFocus() == app.appSearch,
                            "dock search opens the native focused palette field");
                SetWindowTextW(app.appSearch, L"Open Today");
                PumpFor(40);
                app.RebuildLayout();
                RequireLive(app.paletteQuery == L"Open Today" && app.paletteItems.size() == 1,
                            "native palette query filters commands independently of app search");
                app.Invoke(2600);
                Settle(app);
                RequireLive(app.tab == Tab::Today && app.flyout == Flyout::None,
                            "palette executes the matching Dako command");
            }
        }
        app.SetTab(Tab::Stats);
        Settle(app);
        RequireLive(app.statsTicking, "visible Stats starts sampling");
        app.OpenPalette();
        Settle(app);
        RequireLive(!app.statsTicking && !app.connectionsTicking && !app.mediaTicking,
                    "palette stops sampling from the previous view");
        app.SetTab(Tab::Stats);
        Settle(app);
        app.OpenFromDock(desktop::DockStatusMore, panelMonitor);
        Settle(app);
        RequireLive(app.flyout == Flyout::DockStatus, "status overflow opens its own island view");
        RequireLive(app.IndexForKey(app.StableKey(2800)) >= 0,
                    "status popover replaces the already-open view's hit layout");
        RequireLive(!app.statsTicking, "status popover stops hidden Stats sampling");
        unsigned mask = app.dockOptions.statusMask;
        app.Invoke(2800);
        RequireLive(app.dockOptions.statusMask == (mask ^ 1U),
                    "status choices update the preview dock without persistence");
        app.dockOptions.statusMask = mask;
        app.SetExpanded(false);
        Settle(app);
        app.StopDesktop();
        app.dockMode = false;
        app.StartDesktop();
        RequireLive(!app.docks, "switching to notch removes all preview docks");
        app.StopDesktop();
        app.dockMode = true;
        app.StartDesktop();
        RequireLive(app.docks && app.docks->Count() == desktopAreasBefore.size(),
                    "switching back recreates monitor docks");
        app.StopDesktop();
        auto desktopAreasAfter = workAreas();
        RequireLive(desktopAreasBefore.size() == desktopAreasAfter.size(), "preview preserves monitor count");
        for (size_t i = 0; i < desktopAreasBefore.size(); ++i)
            RequireLive(EqualRect(&desktopAreasBefore[i], &desktopAreasAfter[i]),
                        "bar/notch preview switches never change a work area");
        report << "production_bar_widgets_anchor_search=PASS\npreview_bar_notch_switch_preserves_work_areas=PASS\n";
        app.nativeControls.reset();
        app.media.reset();
        app.hardware.reset();
        app.catalog.reset();
        app.connectivity.reset();
        DestroyWindow(window);
        PumpFor(50);
        report.close();
        std::puts("PASS: live native renderer/lifecycle verification. See the requested output directory.");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    } catch (const winrt::hresult_error &e) {
        std::fprintf(stderr, "FAIL HRESULT %08X\n", static_cast<unsigned>(e.code().value));
        return 2;
    }
    winrt::uninit_apartment();
    return 0;
}
