#include "../src/Island.h"
#include "OwnedWindowCapture.h"
#include <cstdio>
#include <cstdlib>

void RequireUnified(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::fflush(stderr);
        std::exit(1);
    }
}
void PumpUnified() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
void PumpUnifiedFor(ULONGLONG milliseconds) {
    auto until = GetTickCount64() + milliseconds;
    while (GetTickCount64() < until) {
        PumpUnified();
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RequireUnified(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM apartment");
    try {
        Island app;
        std::puts("stage island constructor");
        std::fflush(stdout);
        app.renderMode = true;
        app.fixtureGeometry = true;
        app.previewMode = true;
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = WindowProcedure;
        type.lpszClassName = WindowClass;
        RegisterClassW(&type);
        HWND root = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, WindowClass,
                                    L"Dako unified inert fixture", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1000, 760, nullptr,
                                    nullptr, type.hInstance, &app);
        std::puts("stage production HWND");
        std::fflush(stdout);
        RequireUnified(root != nullptr, "production island fixture initialized");
        app.renderMode = false;
        app.reducedMotion = true;
        app.dockMode = true;
        app.docks = std::make_unique<desktop::DockShell>();
        unsigned volumeActions = 0;
        desktop::DockShell::Callbacks callbacks;
        callbacks.open = [&](int action, HMONITOR monitor) { app.OpenFromDock(action, monitor); };
        callbacks.popover = [&](surface::Request request, desktop::DockGroup group, HMONITOR monitor) {
            app.RequestSurface(std::move(request), std::move(group), monitor);
        };
        callbacks.volume = [&](int delta, HMONITOR) {
            ++volumeActions;
            if (delta)
                app.SetSystemVolume(std::clamp(app.system.volume + delta / 120.0f * .02f, 0.0f, 1.0f));
            else
                app.ToggleSystemMute();
            app.UpdateDocks();
        };
        RequireUnified(app.docks->Start(root, nullptr, true, 1, callbacks, false), "unreserved inert dock fixture");
        std::puts("stage fixture dock");
        std::fflush(stdout);
        HMONITOR monitor = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
        app.panelMonitor = monitor;
        app.Place();
        ShowWindow(root, SW_SHOWNOACTIVATE);
        app.connections.networks.push_back({});
        app.connections.networks.back().name = L"Inert network";
        const int controls[]{desktop::DockApps,    desktop::DockSearch, desktop::DockWifi,    desktop::DockVolume,
                             desktop::DockBattery, desktop::DockClock,  desktop::DockWeather, desktop::DockStatusMore};
        unsigned routes = 0;
        for (auto edge : {anchor::Edge::Top, anchor::Edge::Bottom}) {
            app.SetExpanded(false);
            app.edge = edge;
            app.dockOptions.edge = edge;
            app.docks->Options(app.dockOptions);
            app.Place();
            for (int control : controls) {
                std::printf("route=%d edge=%d\n", control, static_cast<int>(edge));
                std::fflush(stdout);
                app.OpenFromDock(control, monitor);
                RequireUnified(app.expanded, "one click opens each dock panel");
                RECT before{};
                GetWindowRect(root, &before);
                app.Draw();
                auto *target = app.target.Get();
                PumpUnified();
                if (control >= desktop::DockWifi && control <= desktop::DockWeather) {
                    RequireUnified(app.surfaceRequest.kind == surface::Kind::QuickPanel,
                                   "all five status buttons share the Quick Panel");
                    RequireUnified(app.IndexForKey(app.StableKey(Volume)) >= 0, "common volume control remains");
                }
                if (control == desktop::DockWifi) {
                    int network = app.IndexForKey(app.StableKey(1000));
                    RequireUnified(network >= 0, "network selection is available as the second click");
                    app.Invoke(1000);
                    RequireUnified(app.expanded && app.flyout == Flyout::None,
                                   "inert selection opens no native dialog");
                }
                int other = control == desktop::DockVolume ? desktop::DockWifi : desktop::DockVolume;
                app.OpenFromDock(other, monitor);
                RECT after{};
                GetWindowRect(root, &after);
                RequireUnified(EqualRect(&before, &after), "same-monitor retarget preserves the envelope HWND bounds");
                RequireUnified(app.target.Get() == target, "same-monitor retarget preserves the graphics target");
                app.OpenFromDock(other, monitor);
                RequireUnified(!app.expanded, "clicking the active button closes");
                app.OpenFromDock(control, monitor);
                app.Key(VK_ESCAPE);
                RequireUnified(!app.expanded, "Escape closes one surface level");
                RequireUnified(!app.scheduler.active && !app.frameClock.Active(), "no motion timers remain at rest");
                ++routes;
            }
            app.OpenFromDock(desktop::DockWifi, monitor);
            app.Draw();
            DwmFlush();
            auto p = app.DockPill();
            auto e = app.DockExtension(app.width, app.height);
            float x = (std::max(p.left, e.left) + std::min(p.right, e.right)) / 2;
            float y = edge == anchor::Edge::Bottom ? p.top : p.bottom;
            float scale = app.dpi / 96;
            auto pixels = fixture::CaptureOwnedWindow(
                root, 4000, {static_cast<LONG>(x * scale), static_cast<LONG>((y - 2) * scale)},
                {static_cast<LONG>(x * scale), static_cast<LONG>((y + 2) * scale)});
            RequireUnified(SUCCEEDED(pixels.status) && pixels.center == pixels.corner &&
                               (pixels.center & 0xFFFFFF) == theme::Surface,
                           "production union has identical pixels above and below its join");
            std::printf("edge=%d join_above=%08x join_below=%08x\n", static_cast<int>(edge), pixels.center,
                        pixels.corner);
            SendMessageW(root, WM_SYSCOMMAND, SC_CLOSE, 0);
            RequireUnified(IsWindow(root) && app.docks->Count() > 0 && !app.expanded,
                           "Alt+F4 collapses the surface and keeps the dock alive");
        }
        unsigned forbidden = 0;
        app.ContextMenu();
        RequireUnified(app.IndexForKey(app.StableKey(2990)) >= 0,
                       "dock context menu exposes Settings in the shared compact surface");
        app.Invoke(9100);
        RequireUnified(app.surfaceRequest.kind == surface::Kind::FullIsland && app.IndexForKey(app.StableKey(100)) >= 0,
                       "the expand control exposes full Dako navigation");
        app.SetExpanded(false);
        WNDCLASSW sourceType{};
        sourceType.hInstance = GetModuleHandleW(nullptr);
        sourceType.lpfnWndProc = DefWindowProcW;
        sourceType.lpszClassName = L"Dako.Unified.OwnedPreviewSource";
        sourceType.hbrBackground = CreateSolidBrush(RGB(240, 60, 180));
        RegisterClassW(&sourceType);
        desktop::DockGroup group;
        group.key = L"fixture:owned-previews";
        group.name = L"Owned preview fixtures";
        for (int i = 0; i < 3; ++i) {
            HWND source =
                CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, sourceType.lpszClassName, L"Owned inert preview",
                                WS_OVERLAPPEDWINDOW, 24, 24, 320, 200, nullptr, nullptr, sourceType.hInstance, nullptr);
            ShowWindow(source, SW_SHOWNOACTIVATE);
            UpdateWindow(source);
            desktop::WindowEntry entry;
            entry.window = source;
            entry.process = GetCurrentProcessId();
            entry.title = L"Owned document " + std::to_wstring(i + 1);
            group.windows.push_back(entry);
        }
        auto preview = surface::Describe(surface::Control::App, surface::Trigger::Hover, group.windows.size());
        preview.behavior = surface::Behavior::Panel;
        preview.control = desktop::DockGroupFirst;
        preview.group = group.key;
        app.RequestSurface(preview, group, monitor);
        app.Draw();
        DwmFlush();
        RequireUnified(app.surfaceThumbnails[0] && app.surfaceThumbnails[1] && app.surfaceThumbnails[2],
                       "three native thumbnails register against the production root HWND");
        auto slot = app.thumbnailSlots[0];
        float scale = app.dpi / 96;
        POINT sample{static_cast<LONG>((app.PanelX() + (slot.left + slot.right) / 2) * scale),
                     static_cast<LONG>((app.PanelY() + (slot.top + slot.bottom) / 2 - app.scroll) * scale)};
        auto pixels = fixture::CaptureOwnedWindow(root, 4000, sample, sample);
        RequireUnified(SUCCEEDED(pixels.status) && (pixels.center & 0xFFFFFF) == 0xF03CB4,
                       "owned source pixels composite over the production preview card");
        std::printf("production_preview_pixel=%08x\n", pixels.center);
        app.SetExpanded(false);
        for (const auto &entry : group.windows)
            DestroyWindow(entry.window);
        DeleteObject(sourceType.hbrBackground);
        HWND bar = nullptr;
        std::pair<HWND, HWND *> dockSearch{root, &bar};
        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                auto pair = reinterpret_cast<std::pair<HWND, HWND *> *>(context);
                wchar_t name[64]{};
                GetClassNameW(window, name, 64);
                if (GetWindow(window, GW_OWNER) == pair->first && !wcscmp(name, L"Dako.Native.Dock")) {
                    *pair->second = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&dockSearch));
        RequireUnified(bar != nullptr, "owned input dock found for direct volume action");
        RECT barBounds{};
        GetWindowRect(bar, &barBounds);
        float barScale = GetDpiForWindow(bar) / 96.0f;
        auto dockLayout = desktop::SolveDock((barBounds.right - barBounds.left) / barScale, {}, {}, app.dockOptions);
        auto volume = std::find_if(dockLayout.hits.begin(), dockLayout.hits.end(),
                                   [](const auto &hit) { return hit.action == desktop::DockVolume; });
        RequireUnified(volume != dockLayout.hits.end(), "volume action is present");
        POINT local{static_cast<LONG>(volume->rect.CenterX() * barScale),
                    static_cast<LONG>((volume->rect.top + volume->rect.bottom) / 2 * barScale)};
        POINT screen = local;
        ClientToScreen(bar, &screen);
        SendMessageW(bar, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
        RequireUnified(volumeActions == 1 && !app.expanded, "volume wheel is an Action with no surface");
        SendMessageW(root, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
        RequireUnified(volumeActions == 2 && !app.expanded,
                       "focused-parent wheel routing still reaches the pill action");
        UpdateWindow(bar);
        SendMessageW(bar, WM_MBUTTONUP, 0, MAKELPARAM(local.x, local.y));
        RequireUnified(volumeActions == 3 && app.system.muted && !app.expanded,
                       "middle-click mutes through the inert gate without opening anything");
        app.OpenFromDock(desktop::DockVolume, monitor);
        app.Draw();
        SendMessageW(bar, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
        app.Draw();
        RequireUnified(volumeActions == 4 && app.surfaceRequest.kind == surface::Kind::QuickPanel,
                       "in-pill HUD also composes correctly while the Quick Panel is open");
        app.SetExpanded(false);
        auto pendingPeek = surface::Describe(surface::Control::Wifi, surface::Trigger::Hover);
        pendingPeek.control = desktop::DockWifi;
        app.RequestSurface(pendingPeek, {}, monitor);
        auto staleHover = app.surfaceHover.generation;
        app.OpenFromDock(desktop::DockVolume, monitor);
        app.SurfaceDeadline(staleHover);
        RequireUnified(app.expanded && app.surfaceRequest.behavior == surface::Behavior::Panel &&
                           app.surfaceRequest.section == surface::Section::Volume,
                       "a queued hover deadline cannot replace a clicked Panel");
        app.SetExpanded(false);
        app.reducedMotion = false;
        app.OpenFromDock(desktop::DockApps, monitor);
        PumpUnifiedFor(350);
        app.Draw();
        auto *animatedTarget = app.target.Get();
        std::printf("animated open: expanded=%d reduced=%d high_contrast=%d phase=%d height=%g target=%p\n",
                    app.expanded, app.reducedMotion, app.highContrast, static_cast<int>(app.surfaceMotion.phase),
                    app.height, static_cast<void *>(animatedTarget));
        app.OpenFromDock(desktop::DockVolume, monitor);
        std::printf("retarget start: snapshot=%d blend=%g phase=%d\n", !!app.previousPane, app.paneBlend.value,
                    static_cast<int>(app.surfaceMotion.phase));
        // Sample the production transition at a known interval. A native paint/message drain
        // can exceed the entire fade on a busy GPU, so wall time is not an intermediate sample.
        app.lastMotion = steady_clock::now() - std::chrono::milliseconds(45);
        app.Transition();
        std::printf("retarget sample: snapshot=%d blend=%g phase=%d\n", !!app.previousPane, app.paneBlend.value,
                    static_cast<int>(app.surfaceMotion.phase));
        std::fflush(stdout);
        RequireUnified(app.previousPane && app.paneBlend.value > 0 && app.paneBlend.value < 1,
                       "production content has an active shared crossfade");
        app.Draw();
        app.OpenFromDock(desktop::DockSearch, monitor);
        PumpUnifiedFor(50);
        app.Draw();
        app.OpenFromDock(desktop::DockWifi, monitor);
        PumpUnifiedFor(350);
        app.Draw();
        RequireUnified(app.target.Get() == animatedTarget && !app.surfaceMotion.Moving() && !app.previousPane,
                       "rapid animated retargets preserve graphics and release completed snapshots");
        app.SetExpanded(false);
        PumpUnifiedFor(450);
        RequireUnified(!app.target && !app.scheduler.active && !app.frameClock.Active(),
                       "animated close releases graphics and leaves no active frame clock");
        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                if (process != GetCurrentProcessId())
                    return TRUE;
                wchar_t name[96]{};
                GetClassNameW(window, name, 96);
                if (!wcscmp(name, L"#32768") || !wcscmp(name, L"tooltips_class32") ||
                    !wcscmp(name, L"Dako.Native.WindowPreview") || !wcscmp(name, L"#32770"))
                    ++*reinterpret_cast<unsigned *>(context);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&forbidden));
        RequireUnified(forbidden == 0, "no native menu, tooltip, preview or dialog HWND in dock flows");
        app.SetExpanded(false);
        DestroyWindow(root);
        std::printf("PASS: %u production routes, two-click Wi-Fi, active toggle, Escape, Alt+F4, stable HWND/graphics, "
                    "zero rest motion.\n",
                    routes);
    } catch (const winrt::hresult_error &error) {
        std::fprintf(stderr, "FAIL HRESULT %08X\n", static_cast<unsigned>(error.code().value));
        return 2;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 3;
    }
    CoUninitialize();
    return 0;
}
