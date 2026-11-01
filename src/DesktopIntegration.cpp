#include "Island.h"

namespace {
bool CurrentSurfaceWindow(const desktop::WindowEntry &entry) {
    DWORD process = 0;
    return IsWindow(entry.window) && GetWindowThreadProcessId(entry.window, &process) && process == entry.process;
}
} // namespace
bool Island::DockEnabled() const { return dockMode && !renderMode && (!fixtureGeometry || docks != nullptr); }

void Island::StopDesktop() {
    for (size_t i = 0; i < numberHotkeys.size(); ++i) {
        if (numberHotkeys[i])
            UnregisterHotKey(window, 1000 + static_cast<int>(i));
        numberHotkeys[i] = false;
    }
    if (shellHookRegistered && DeregisterShellHookWindow(window))
        privacy::Policy().Unwatch(L"shell-hook", reinterpret_cast<uintptr_t>(window));
    shellHookRegistered = false;
    if (windowList) {
        windowList->Stop();
        windowList.reset();
    }
    if (docks) {
        docks->Stop();
        docks.reset();
    }
    hotZone.Stop();
    shell.Stop();
}

void Island::StartDesktop() {
    if (renderMode || fixtureGeometry)
        return;
    StopDesktop();
    if (desktopRequested && !previewMode && !shell.Start(dockMode)) {
        note = L"Taskbar recovery could not start. Explorer is still available.";
        desktopRequested = false;
    }
    if (DockEnabled()) {
        desktop::DockShell::Callbacks callbacks;
        callbacks.open = [this](int action, HMONITOR monitor) { OpenFromDock(action, monitor); };
        callbacks.launch = [this](const std::wstring &key) { LaunchPin(key); };
        callbacks.reorder = [this](size_t from, size_t to) { ReorderPin(from, to); };
        callbacks.pin = [this](const std::wstring &key, bool pinned) { PinToDock(key, pinned); };
        callbacks.error = [this](const std::wstring &message) {
            note = message;
            auto request = surface::Describe(surface::Control::More);
            request.control = desktop::DockStatusMore;
            surfaceHover.pinned = true;
            PresentSurface(request, {},
                           panelMonitor ? panelMonitor : MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST));
            keyboardOpen = true;
            SetForegroundWindow(window);
        };
        callbacks.recent = [this](const std::wstring &appId, HMONITOR monitor) { OpenRecent(appId, monitor); };
        callbacks.pointer = [this](bool inside, HMONITOR) { SurfacePointer(inside); };
        callbacks.popover = [this](surface::Request request, desktop::DockGroup group, HMONITOR monitor) {
            RequestSurface(std::move(request), std::move(group), monitor);
        };
        callbacks.volume = [this](int delta, HMONITOR) {
            if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode))
                return;
            if (delta)
                SetSystemVolume(std::clamp(system.volume + delta / 120.0f * .02f, 0.0f, 1.0f));
            else
                ToggleSystemMute();
            UpdateDocks();
        };
        callbacks.expand = [this](HMONITOR monitor) {
            panelMonitor = monitor;
            dockAnchor = docks->Anchor(monitor, desktop::DockApps);
            miniMode = false;
            interaction.OnExpand();
            ApplyInteraction();
        };
        callbacks.context = [this] { ContextMenu(); };
        docks = std::make_unique<desktop::DockShell>();
        docks->Options(dockOptions);
        if (!docks->Start(window, &shell, previewMode, textScale, std::move(callbacks), false)) {
            docks.reset();
            shell.Stop();
            dockMode = false;
            desktopRequested = false;
            note = L"The floating dock could not start. Dako is using the notch for this session.";
        }
    }
    if (!DockEnabled() && !previewMode) {
        hotZone.Initialize(window);
        hotZone.Place(window, !expanded, static_cast<UINT>(dpi));
    }
    windowList = std::make_unique<desktop::WindowList>();
    if (!windowList->Start(window)) {
        windowList.reset();
        note = L"Windows did not provide app events. Pinned apps are still available.";
    }
    shellHookRegistered = RegisterShellHookWindow(window) != FALSE;
    if (shellHookRegistered)
        privacy::Policy().Watch(L"shell-hook", reinterpret_cast<uintptr_t>(window), L"dock", L"window-flash-events");
    if (shell.Active() && docks) {
        for (size_t i = 0; i < numberHotkeys.size(); ++i)
            numberHotkeys[i] = RegisterHotKey(window, 1000 + static_cast<int>(i), MOD_WIN | MOD_NOREPEAT,
                                              '1' + static_cast<UINT>(i)) != FALSE;
    }
    if (docks) {
        docks->PanelVisible(expanded);
        if (!connectivity)
            connectivity = std::make_unique<Connectivity>(window);
        connectivity->RefreshWifi(false);
        if (!catalog)
            catalog = std::make_unique<DesktopCatalog>(window);
        if (!appsRequested) {
            appsRequested = true;
            appsLoading = true;
            catalog->DiscoverApps();
        }
        panelMonitor = docks->ActiveMonitor();
        UpdateDocks();
    }
    RefreshGeometry();
}

void Island::ToggleDesktopMode() {
    if (previewMode || renderMode || fixtureGeometry) {
        note = L"Desktop policy changes are disabled in preview.";
        return;
    }
    desktopRequested = !shell.Active();
    desktopPromptDone = true;
    if (!SaveSettings())
        note = L"Desktop mode applies for this session; settings could not be saved.";
    StartDesktop();
}

void Island::UpdateDocks() {
    if (!docks)
        return;
    std::vector<desktop::PinEntry> pins;
    for (const auto &key : appPreferences.favorites) {
        desktop::PinEntry pin;
        pin.key = key;
        auto found = std::find_if(apps.begin(), apps.end(), [&](const AppEntry &app) { return AppKey(app) == key; });
        if (found != apps.end()) {
            pin.name = found->name;
            pin.appId = found->appId;
            pin.executable = found->executable;
            pin.pixels = found->iconPixels;
            pin.width = found->iconWidth;
            pin.height = found->iconHeight;
        } else {
            pin.name = std::filesystem::path(key).stem().wstring();
            if (pin.name.empty())
                pin.name = L"Pinned application";
        }
        pins.push_back(std::move(pin));
    }
    desktop::DockStatus status;
    dockOptions.opaque = opaqueMaterial;
    status.wifi = system.wifi;
    status.volume = system.muted ? L"Muted" : Number(system.volume * 100) + L" percent";
    status.weather = weather.available     ? Number(weather.temperature) + L"°C"
                     : weatherCity.empty() ? L"Weather off"
                                           : L"Weather";
    docks->Options(dockOptions);
    docks->Appearance(reducedMotion, highContrast, textScale);
    auto groups =
        expanded && (surfaceRequest.kind == surface::Kind::Preview || surfaceRequest.kind == surface::Kind::AppContext)
            ? desktop::BuildDockGroups(pins, runningWindows, panelMonitor)
            : std::vector<desktop::DockGroup>{};
    docks->SetData(std::move(pins), runningWindows, std::move(status));
    if (expanded &&
        (surfaceRequest.kind == surface::Kind::Preview || surfaceRequest.kind == surface::Kind::AppContext) &&
        !surfaceGroup.key.empty() && surfaceGroup.key != L"dako:menu") {
        auto found = std::find_if(groups.begin(), groups.end(),
                                  [&](const auto &group) { return group.key == surfaceGroup.key; });
        if (found != groups.end()) {
            bool changed = found->windows.size() != surfaceGroup.windows.size();
            for (size_t i = 0; !changed && i < found->windows.size(); ++i)
                changed = found->windows[i].window != surfaceGroup.windows[i].window ||
                          found->windows[i].process != surfaceGroup.windows[i].process;
            if (changed) {
                ClearSurfaceThumbnails();
                surfaceGroup = *found;
                if (surfaceRequest.kind == surface::Kind::Preview && surfaceGroup.windows.empty())
                    SetExpanded(false);
                else {
                    RetargetLayout();
                    RebuildLayout();
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
        } else if (surfaceRequest.kind == surface::Kind::Preview)
            SetExpanded(false);
        else if (!surfaceGroup.windows.empty()) {
            ClearSurfaceThumbnails();
            surfaceGroup.windows.clear();
            RetargetLayout();
            RebuildLayout();
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    if (catalog) {
        std::vector<std::wstring> keys;
        for (const auto &key : appPreferences.favorites)
            keys.push_back(key);
        UINT extent = static_cast<UINT>(std::ceil(56 * std::max(2.0f, dpi / 96) * textScale * 1.5f));
        catalog->RequestIcons(std::move(keys), extent);
    }
}

void Island::OpenFromDock(int action, HMONITOR monitor) {
    auto control = action == desktop::DockApps      ? surface::Control::Apps
                   : action == desktop::DockSearch  ? surface::Control::Search
                   : action == desktop::DockWifi    ? surface::Control::Wifi
                   : action == desktop::DockVolume  ? surface::Control::Volume
                   : action == desktop::DockBattery ? surface::Control::Battery
                   : action == desktop::DockClock   ? surface::Control::Clock
                   : action == desktop::DockWeather ? surface::Control::Weather
                                                    : surface::Control::More;
    auto request = surface::Describe(control);
    request.control = action;
    RequestSurface(request, {}, monitor);
}

void Island::LaunchPin(const std::wstring &key) {
    if (renderMode || fixtureGeometry)
        return;
    auto found = std::find_if(apps.begin(), apps.end(), [&](const AppEntry &app) { return AppKey(app) == key; });
    AppEntry app = found == apps.end() ? AppEntry{std::filesystem::path(key).stem().wstring(), key} : *found;
    OpenApp(app);
    UpdateDocks();
}

void Island::ReorderPin(size_t from, size_t to) {
    auto &pins = appPreferences.favorites;
    if (from >= pins.size() || to >= pins.size() || from == to)
        return;
    auto saved = pins;
    auto value = pins[from];
    pins.erase(pins.begin() + static_cast<ptrdiff_t>(from));
    pins.insert(pins.begin() + static_cast<ptrdiff_t>(to), std::move(value));
    if (!previewMode && !renderMode && !appPreferences.Save()) {
        pins = std::move(saved);
        note = L"Could not save pinned-app order.";
    }
    FilterApps(false);
    UpdateDocks();
}

void Island::PinToDock(const std::wstring &key, bool pinned) {
    if (key.empty())
        return;
    auto &pins = appPreferences.favorites;
    auto saved = pins;
    auto found = std::find(pins.begin(), pins.end(), key);
    if (pinned && found == pins.end() && pins.size() < 64)
        pins.push_back(key);
    if (!pinned && found != pins.end())
        pins.erase(found);
    if (!previewMode && !renderMode && !appPreferences.Save()) {
        pins = std::move(saved);
        note = L"Could not save pinned apps.";
    }
    FilterApps(false);
    UpdateDocks();
}

void Island::FilterRunning() {
    runningIndices.clear();
    auto query = desktop::AppIdentity(appQuery);
    for (size_t i = 0; i < runningWindows.size(); ++i) {
        const auto &entry = runningWindows[i];
        if (!fixtureGeometry && panelMonitor && entry.monitor != panelMonitor)
            continue;
        if (!query.empty() && desktop::AppIdentity(entry.title).find(query) == std::wstring::npos)
            continue;
        runningIndices.push_back(i);
    }
    std::stable_sort(runningIndices.begin(), runningIndices.end(), [&](size_t a, size_t b) {
        auto first = runningWindows[a].identity + runningWindows[a].title;
        auto second = runningWindows[b].identity + runningWindows[b].title;
        return first < second;
    });
    if (runningPage * 2 >= runningIndices.size())
        runningPage = 0;
}

float Island::AppsListTop() const {
    auto b = Body();
    return b.top + 48 + (ShowAppRail() ? 76.0f : 0.0f) +
           (b.bottom - b.top >= 248 && !runningIndices.empty() ? 128.0f : 0.0f);
}

void Island::ConfirmDesktopChoice() {
    if (renderMode || previewMode || fixtureGeometry || desktopPromptDone)
        return;
    desktopPromptDone = true;
    if (!desktopRequested) {
        ModalGuard guard(modalOpen);
        desktopRequested = MessageBoxW(window,
                                       L"Use Dako as my taskbar?\n\nDako will hide Explorer's taskbar and float "
                                       L"over your apps without reserving space. Restore Explorer from Dako's menu.",
                                       L"Dako", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    if (!SaveSettings())
        note = L"Your choice applies for this session; settings could not be saved.";
}

bool Island::PrepareSystemChange() {
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode))
        return false;
    if (shell.EnsureRecovery())
        return true;
    note = L"Recovery could not start. Windows settings were left unchanged.";
    return false;
}

void Island::SetSystemVolume(float value) {
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        system.volume = std::clamp(value, 0.0f, 1.0f);
        return;
    }
    if (PrepareSystemChange())
        system.SetVolume(value);
}

void Island::ToggleSystemMute() {
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        system.muted = !system.muted;
        return;
    }
    if (PrepareSystemChange())
        system.ToggleMute();
}

void Island::RequestSurface(surface::Request request, desktop::DockGroup group, HMONITOR monitor) {
    if (request.behavior == surface::Behavior::Peek) {
        if (surfaceHover.pinned || (request.kind == surface::Kind::Tooltip && !dockOptions.labels))
            return;
        hoverMonitor = monitor;
        hoverGroup = std::move(group);
        auto token = surfaceHover.Enter(request);
        if (!surfaceDeadline)
            surfaceDeadline = std::make_unique<OneShotDeadline>(window, SurfaceDue);
        if (surfaceHover.delay)
            surfaceDeadline->Arm(surfaceHover.delay, token);
        else {
            surfaceDeadline->Cancel();
            SurfaceDeadline(token);
        }
        return;
    }
    if (expanded && surfaceRequest.SameTarget(request) && panelMonitor == monitor &&
        surfaceRequest.behavior == surface::Behavior::Panel) {
        SetExpanded(false);
        return;
    }
    surfaceHover.Close();
    surfaceHover.pinned = true;
    surfaceHover.peeking = false;
    if (surfaceDeadline)
        surfaceDeadline->Cancel();
    PresentSurface(std::move(request), std::move(group), monitor);
}

void Island::SurfacePointer(bool inside) {
    if (!DockEnabled())
        return;
    auto token = surfaceHover.Pointer(inside);
    if (!surfaceDeadline)
        return;
    if (surfaceHover.delay)
        surfaceDeadline->Arm(surfaceHover.delay, token);
    else if (surfaceHover.due != surface::HoverPolicy::Due::Show)
        surfaceDeadline->Cancel();
}

void Island::SurfaceDeadline(WPARAM token) {
    auto due = surfaceHover.Consume(static_cast<unsigned>(token));
    if (due == surface::HoverPolicy::Due::Show) {
        surfaceHover.peeking = true;
        PresentSurface(surfaceHover.pending, hoverGroup, hoverMonitor);
    } else if (due == surface::HoverPolicy::Due::Hide)
        SetExpanded(false, false);
}

void Island::CapturePane() {
    if (!expanded || !target || reducedMotion || highContrast || renderMode)
        return;
    ComPtr<ID2D1BitmapRenderTarget> capture;
    float visibleHeight = DockEnabled() ? height / textScale : LayoutHeight();
    if (visibleHeight < 1)
        return;
    auto desired = D2D1::SizeF(LayoutWidth(), visibleHeight);
    float density = dpi / 96 * textScale;
    auto pixels = D2D1::SizeU(static_cast<UINT>(std::ceil(desired.width * density)),
                              static_cast<UINT>(std::ceil(desired.height * density)));
    if (FAILED(target->CreateCompatibleRenderTarget(&desired, &pixels, nullptr,
                                                    D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &capture)))
        return;
    auto savedTarget = target;
    auto savedHits = hits;
    target = capture;
    capturingPane = true;
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0.0f));
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    float fade = std::clamp(paneBlend.value, 0.0f, 1.0f);
    if (previousPane && fade < 1) {
        auto size = previousPane->GetSize();
        float left = (LayoutWidth() - size.width) / 2;
        float top = edge == anchor::Edge::Bottom ? visibleHeight - size.height : 0;
        target->DrawBitmap(previousPane.Get(), {left, top, left + size.width, top + size.height}, 1 - fade);
    }
    auto layer = D2D1::LayerParameters();
    layer.opacity = fade;
    if (fade < 1)
        target->PushLayer(layer, nullptr);
    target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection * 8 * (1 - fade), 0));
    PanelContent();
    if (fade < 1)
        target->PopLayer();
    HRESULT result = target->EndDraw();
    capturingPane = false;
    target = savedTarget;
    hits = std::move(savedHits);
    if (SUCCEEDED(result))
        capture->GetBitmap(&previousPane);
}

void Island::PresentSurface(surface::Request request, desktop::DockGroup group, HMONITOR monitor) {
    if (!docks && !fixtureGeometry && !renderMode)
        return;
    if (ChromeActive()) {
        chrome.result = chrome.cancel;
        CloseChrome();
    }
    bool moved = panelMonitor != monitor;
    if (!moved)
        CapturePane();
    ClearSurfaceThumbnails();
    surfaceRequest = std::move(request);
    surfaceMotion.label = surfaceRequest.kind == surface::Kind::Tooltip;
    if (surfaceMotion.label)
        SetPropW(window, L"Dako.SurfaceLabel", reinterpret_cast<HANDLE>(1));
    else
        RemovePropW(window, L"Dako.SurfaceLabel");
    if (surfaceMotion.label && docks)
        docks->Joined(monitor, false);
    surfaceGroup = std::move(group);
    if (moved && docks) {
        ReleaseGraphics();
        panelMonitor = monitor;
        UINT x = 96, y = 96;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y);
        dpi = static_cast<float>(x);
        Place();
    }
    if (docks) {
        dockAnchor = docks->Anchor(monitor, surfaceRequest.control);
        bodyDirection = dockAnchor.x >= surfaceMotion.anchor.value ? 1.0f : -1.0f;
    }
    if (!expanded || moved)
        surfaceMotion.anchor.Snap(static_cast<float>(dockAnchor.x));
    flyout = surfaceRequest.kind == surface::Kind::Palette ? Flyout::Palette : Flyout::None;
    tab = surfaceRequest.kind == surface::Kind::Apps || surfaceRequest.kind == surface::Kind::Palette ? Tab::Apps
                                                                                                      : Tab::Controls;
    miniMode = false;
    scroll = 0;
    focused = hovered = -1;
    focusedKey.clear();
    if (surfaceRequest.kind == surface::Kind::Apps) {
        FilterApps(false);
        FilterRunning();
    }
    SetExpanded(true);
    if (surfaceRequest.behavior == surface::Behavior::Panel) {
        keyboardOpen = true;
        if (!fixtureGeometry && !renderMode) {
            SetForegroundWindow(window);
            SetFocus(window);
        }
        RebuildLayout();
        if (surfaceRequest.kind == surface::Kind::Palette)
            FocusAt(IndexForKey(StableKey(401)));
        else if (surfaceRequest.section == surface::Section::Wifi)
            FocusAt(IndexForKey(StableKey(Wifi)));
        else if (surfaceRequest.section == surface::Section::Volume)
            FocusAt(IndexForKey(StableKey(Volume)));
        else if (surfaceRequest.section == surface::Section::Battery)
            FocusAt(IndexForKey(StableKey(9112)));
        else if (surfaceRequest.section == surface::Section::Clock)
            FocusAt(IndexForKey(StableKey(FocusStart)));
        else if (surfaceRequest.section == surface::Section::Weather)
            FocusAt(IndexForKey(StableKey(9113)));
        else if (!hits.empty())
            FocusAt(0);
    }
    InvalidateRect(window, nullptr, FALSE);
}

float Island::UnifiedHeight() const {
    if (surfaceRequest.kind == surface::Kind::Media)
        return 120;
    if (surfaceRequest.kind == surface::Kind::Tooltip)
        return 28;
    if (surfaceRequest.kind == surface::Kind::Status)
        return 80;
    if (surfaceRequest.kind == surface::Kind::Preview)
        return 196 + static_cast<float>(surfaceGroup.windows.size() > 3 ? surfaceGroup.windows.size() - 3 : 0) * 48;
    if (surfaceRequest.kind == surface::Kind::AppContext)
        return 4 * 48 + static_cast<float>(surfaceGroup.windows.size()) * 48;
    if (surfaceRequest.kind == surface::Kind::Overflow)
        return std::max(48.0f, static_cast<float>(runningWindows.size()) * 48);
    float extra = 0;
    if (surfaceRequest.section == surface::Section::Wifi)
        extra = 48 * static_cast<float>(std::max(size_t{1}, connections.networks.size()));
    else if (surfaceRequest.section == surface::Section::Audio)
        extra = static_cast<float>(controlState.outputs.size()) * 48;
    else if (surfaceRequest.section == surface::Section::Bluetooth)
        extra = static_cast<float>(connections.devices.size()) * 48;
    else if (surfaceRequest.section == surface::Section::Clock) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        if (renderMode || fixtureGeometry)
            now = {2026, 10, 2, 27, 9, 41, 0, 0};
        auto first = calendar::Days(std::chrono::year(now.wYear) / now.wMonth / 1);
        auto last = std::chrono::year_month_day_last(std::chrono::year(now.wYear),
                                                     std::chrono::month_day_last(std::chrono::month(now.wMonth)));
        unsigned cells = std::chrono::weekday(first).c_encoding() + static_cast<unsigned>(last.day());
        extra = 168 + static_cast<float>((cells + 6) / 7) * 24;
    } else if (surfaceRequest.section != surface::Section::None && surfaceRequest.section != surface::Section::Volume)
        extra = 48;
    return 408 + extra;
}

void Island::UnifiedContent() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (renderMode || fixtureGeometry)
        now = {2026, 10, 2, 27, 9, 41, 0, 0};
    wchar_t dateText[96]{}, timeText[64]{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &now, nullptr, dateText, 96, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, timeText, 64);
    SYSTEM_POWER_STATUS power{};
    GetSystemPowerStatus(&power);
    if (renderMode || fixtureGeometry) {
        power.BatteryFlag = 1;
        power.ACLineStatus = 0;
        power.BatteryLifePercent = 76;
        power.BatteryLifeTime = 7200;
    }
    auto l = Layout();
    auto b = Body(), h = TabRect(-1);
    std::wstring battery = power.BatteryFlag == 128          ? L"AC power"
                           : power.BatteryLifePercent <= 100 ? L"Battery " + Number(power.BatteryLifePercent) + L"%"
                                                             : L"Battery unavailable";
    bool quick = surfaceRequest.kind == surface::Kind::QuickPanel;
    auto header = [&] {
        if (surfaceRequest.kind == surface::Kind::Tooltip)
            return;
        Fill(h, theme::Surface, shape::Flat());
        Text(quick                                            ? L"Quick Panel"
             : surfaceRequest.kind == surface::Kind::Overflow ? L"Windows"
             : surfaceRequest.kind == surface::Kind::Media    ? L"Now playing"
             : surfaceRequest.kind == surface::Kind::Status
                 ? surfaceRequest.section == surface::Section::Wifi      ? L"Wi-Fi"
                   : surfaceRequest.section == surface::Section::Volume  ? L"Volume"
                   : surfaceRequest.section == surface::Section::Battery ? L"Power"
                   : surfaceRequest.section == surface::Section::Clock   ? L"Clock"
                                                                         : L"Weather"
                 : surfaceGroup.name,
             {h.left + 8, h.top, h.right - 44, h.bottom}, type::Control);
        SmallButton(9100, {h.right - 36, h.top + 4, h.right - 4, h.bottom - 4}, L"\xE740", L"Open full Dako");
    };
    D2D1::Matrix3x2F saved;
    if (!layoutOnly) {
        target->GetTransform(&saved);
        auto viewport = b;
        if (!renderMode && surfaceRequest.kind != surface::Kind::Tooltip) {
            float inset = (LayoutWidth() * textScale - DockExtension(width, height).Width()) / (2 * textScale);
            viewport.left = std::clamp(inset, b.left, b.right);
            viewport.right = std::max(viewport.left, std::min(b.right, inset + width / textScale));
            float gap = l.emergency ? layout::Metrics::Space4 : layout::Metrics::Gutter;
            if (edge == anchor::Edge::Bottom)
                viewport.bottom = std::max(viewport.top, std::min(b.bottom, h.top - gap));
            else
                viewport.top = std::min(viewport.bottom, std::max(b.top, h.bottom + gap));
        }
        target->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll) * saved);
    }
    buildingBody = true;
    float x = b.left, y = b.top, right = b.right;
    auto line = [&](int id, const std::wstring &title, const std::wstring &detail, const wchar_t *glyph) {
        if (!layoutOnly && (y + 44 < b.top + scroll || y > b.bottom + scroll))
            AddHit(id, {x, y, right, y + 44}, title + L". " + detail);
        else
            Button(id, {x, y, right, y + 44}, title, detail, glyph, false);
        y += 48;
    };
    if (surfaceRequest.kind == surface::Kind::Tooltip) {
        std::wstring text = surfaceGroup.name.empty() ? surfaceRequest.control == desktop::DockApps     ? L"Apps"
                                                        : surfaceRequest.control == desktop::DockSearch ? L"Search"
                                                                                                        : L"Dako"
                                                      : surfaceGroup.name;
        Fill({x, y, right, y + 28}, theme::Card, shape::Control);
        Text(text, {x + 12, y, right - 12, y + 28}, type::Label, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (surfaceRequest.kind == surface::Kind::Status) {
        std::wstring title =
            surfaceRequest.section == surface::Section::Wifi      ? system.wifi
            : surfaceRequest.section == surface::Section::Volume  ? L"Volume " + Number(system.volume * 100) + L"%"
            : surfaceRequest.section == surface::Section::Battery ? battery
            : surfaceRequest.section == surface::Section::Clock   ? timeText
            : surfaceRequest.section == surface::Section::Weather
                ? weather.available ? Number(weather.temperature) + L"\x00B0 " + WeatherService::Condition(weather.code)
                                    : weather.message
                : L"Today";
        line(9110, title, L"Open Quick Panel", L"\xE713");
        Text(dateText, {x + 12, y, right - 12, y + 32}, type::Body, theme::Muted);
    } else if (surfaceRequest.kind == surface::Kind::AppContext && surfaceGroup.key == L"dako:menu") {
        line(2990, L"Dako settings", L"", L"\xE713");
        line(9100, L"Open full Dako", L"", L"\xE740");
        line(1903, L"Full details", L"", L"\xE946");
        line(Quit, L"Exit and restore Windows", L"", L"\xE8BB");
    } else if (surfaceRequest.kind == surface::Kind::AppContext) {
        for (size_t i = 0; i < surfaceGroup.windows.size(); ++i)
            line(20000 + static_cast<int>(i), surfaceGroup.windows[i].title, L"Activate window", L"\xE737");
        line(9120, L"Open another instance", surfaceGroup.name, L"\xE710");
        line(9121, surfaceGroup.pinned ? L"Unpin" : L"Pin to dock", L"", L"\xE718");
        line(9122, L"Close window", L"", L"\xE8BB");
        line(9123, L"Recent documents", L"", L"\xE81C");
    } else if (surfaceRequest.kind == surface::Kind::Preview) {
        thumbnailSlots = {};
        size_t count = std::min(size_t{3}, surfaceGroup.windows.size());
        float gap = 8, w = count ? (right - x - gap * static_cast<float>(count - 1)) / static_cast<float>(count) : 0;
        for (size_t i = 0; i < count; ++i) {
            float left = x + static_cast<float>(i) * (w + gap);
            D2D1_RECT_F card{left, y, left + w, y + 184};
            Card(card);
            thumbnailSlots[i] = {left + 12, y + 12, left + w - 12, y + 128};
            Fill(thumbnailSlots[i], theme::Inset, shape::Chip);
            if (!layoutOnly) {
                brush->SetColor(Color(highContrast ? theme::Border : theme::Edge));
                graphics::Rounded(target.Get(), factory.Get(), geometryCache, thumbnailSlots[i], shape::Chip,
                                  brush.Get(), (highContrast ? 2.0f : 1.0f) * 96 / dpi / textScale);
            }
            Text(surfaceGroup.windows[i].title, {left + 8, y + 132, left + w - 8, y + 156}, type::Caption);
            AddHit(20000 + static_cast<int>(i), {left, y, left + w, y + 156},
                   L"Activate " + surfaceGroup.windows[i].title);
            int activateId = 20000 + static_cast<int>(i), closeId = 30000 + static_cast<int>(i);
            auto activateKey = StableKey(activateId), closeKey = StableKey(closeId);
            D2D1_RECT_F closeRect{left + w / 2 - 16, y + 152, left + w / 2 + 16, y + 184};
            auto closeName = L"Close " + surfaceGroup.windows[i].title;
            bool showClose = renderMode || hoveredKey == activateKey || hoveredKey == closeKey ||
                             focusedKey == activateKey || focusedKey == closeKey;
            if (showClose)
                SmallButton(closeId, closeRect, L"\xE8BB", closeName);
            else
                AddHit(closeId, closeRect, closeName, false);
        }
        y += 196;
        for (size_t i = 3; i < surfaceGroup.windows.size(); ++i)
            line(20000 + static_cast<int>(i), surfaceGroup.windows[i].title, L"Activate window", L"\xE737");
    } else if (surfaceRequest.kind == surface::Kind::Overflow) {
        for (size_t i = 0; i < runningWindows.size(); ++i)
            line(10000 + static_cast<int>(i), runningWindows[i].title, L"Activate window", L"\xE737");
    } else if (surfaceRequest.kind == surface::Kind::Media) {
        MediaCard(true);
    } else if (quick) {
        Card({x, y, right, y + 56});
        Text(dateText, {x + 12, y, right - 12, y + 28}, type::Label);
        Text(weather.available
                 ? Number(weather.temperature) + L"\x00B0 \x00B7 " + WeatherService::Condition(weather.code)
                 : L"Today \x00B7 Weather off",
             {x + 12, y + 28, right - 12, y + 52}, type::Caption, theme::Muted);
        y += 68;
        Button(Wifi, {x, y, right, y + 64}, L"Wi-Fi", system.wifi, L"\xE701", system.wifiConnected);
        y += 76;
        if (surfaceRequest.section == surface::Section::Wifi) {
            for (size_t i = 0; i < connections.networks.size(); ++i)
                line(1000 + static_cast<int>(i), connections.networks[i].name,
                     connections.networks[i].connected ? L"Connected"
                     : connections.networks[i].secure  ? L"Secured"
                                                       : L"Open network",
                     L"\xE701");
            if (connections.networks.empty()) {
                Text(connections.wifiBusy               ? L"Searching…"
                     : !connections.wifiMessage.empty() ? connections.wifiMessage
                     : connections.wifiOn               ? L"No networks found"
                                                        : L"Wi-Fi is off",
                     {x + 12, y, right - 12, y + 44}, type::Body);
                y += 48;
            }
        }
        float half = (right - x - 12) / 2;
        const int ids[]{Bluetooth, Audio, Night, Dark};
        const wchar_t *names[]{L"Bluetooth", L"Audio", L"Night Light", L"Dark mode"};
        const wchar_t *icons[]{L"\xE702", L"\xE767", L"\xE708", L"\xE7A1"};
        for (int i = 0; i < 4; ++i) {
            float left = x + (i % 2) * (half + 12), top = y + (i / 2) * 76;
            Button(ids[i], {left, top, left + half, top + 64}, names[i], L"", icons[i], i == 3 && controlState.dark);
        }
        y += 152;
        auto volume = VolumeRect();
        Fill(volume, theme::Card, shape::Pill(volume.bottom - volume.top));
        float position = volume.left + (volume.right - volume.left) * std::clamp(system.volume, 0.0f, 1.0f);
        Fill({volume.left, volume.top, position, volume.bottom}, theme::Accent,
             shape::Pill(volume.bottom - volume.top));
        Text(L"Volume " + Number(system.volume * 100) + L"%",
             {volume.left + 12, volume.top, volume.right - 12, volume.bottom}, type::Label);
        AddHit(Volume, volume, L"Volume. " + Number(system.volume * 100) + L" percent", HasAudio());
        FocusRing(Volume, volume);
        y = volume.bottom + 12;
        Card({x, y, right, y + 64});
        Text(currentMedia.available ? currentMedia.title : L"Nothing playing", {x + 12, y, right - 124, y + 32},
             type::Label);
        Text(currentMedia.artist, {x + 12, y + 32, right - 124, y + 60}, type::Caption, theme::Muted);
        for (int i = 0; i < 3; ++i) {
            float left = right - 116 + i * 36;
            SmallButton(Previous + i, {left, y + 16, left + 32, y + 48},
                        i == 0                 ? L"\xE892"
                        : i == 2               ? L"\xE893"
                        : currentMedia.playing ? L"\xE769"
                                               : L"\xE768",
                        i == 0   ? L"Previous track"
                        : i == 2 ? L"Next track"
                                 : L"Play or pause",
                        currentMedia.available && (i == 0   ? currentMedia.canPrevious
                                                   : i == 2 ? currentMedia.canNext
                                                            : currentMedia.canPlay));
        }
        y += 68;
        if (surfaceRequest.section == surface::Section::Audio) {
            for (size_t i = 0; i < controlState.outputs.size(); ++i)
                line(9270 + static_cast<int>(i), controlState.outputs[i].name,
                     controlState.outputs[i].isDefault ? L"Default output" : L"Choose in Sound settings", L"\xE767");
        }
        if (surfaceRequest.section == surface::Section::Bluetooth) {
            for (size_t i = 0; i < connections.devices.size(); ++i)
                line(1200 + static_cast<int>(i), connections.devices[i].name,
                     connections.devices[i].connected ? L"Connected" : L"Available device", L"\xE702");
        }
        if (surfaceRequest.section == surface::Section::Battery)
            line(9112, battery,
                 power.ACLineStatus == 1 ? L"Connected to power"
                 : power.BatteryLifeTime != static_cast<DWORD>(-1)
                     ? Number(power.BatteryLifeTime / 60) + L" minutes remaining"
                     : L"Power and battery",
                 L"\xE83F");
        if (surfaceRequest.section == surface::Section::Clock) {
            Text(timeText, {x + 12, y, right - 12, y + 44}, type::Title);
            y += 48;
            auto first = calendar::Days(std::chrono::year(now.wYear) / now.wMonth / 1);
            int offset = static_cast<int>(std::chrono::weekday(first).c_encoding());
            auto last = std::chrono::year_month_day_last(std::chrono::year(now.wYear),
                                                         std::chrono::month_day_last(std::chrono::month(now.wMonth)));
            int days = static_cast<int>(static_cast<unsigned>(last.day()));
            float cell = (right - x) / 7;
            const wchar_t *labels[]{L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
            for (int day = 0; day < 7; ++day)
                Text(labels[day], {x + day * cell, y, x + (day + 1) * cell, y + 24}, type::Caption, theme::Muted,
                     DWRITE_TEXT_ALIGNMENT_CENTER);
            y += 24;
            for (int day = 1; day <= days; ++day) {
                int slot = offset + day - 1;
                float left = x + (slot % 7) * cell, top = y + (slot / 7) * 24;
                D2D1_RECT_F date{left, top, left + cell, top + 24};
                if (day == now.wDay)
                    Fill(date, theme::AccentSoft, shape::Chip);
                Text(std::to_wstring(day), date, type::Caption, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            y += static_cast<float>((offset + days + 6) / 7) * 24 + 8;
            Text(L"Focus · " + Clock(focusSeconds), {x + 12, y, right - 12, y + 36}, type::Body);
            y += 40;
            line(FocusStart, focusRunning ? L"Pause focus" : L"Start focus", L"", L"\xE768");
        }
        if (surfaceRequest.section == surface::Section::Weather)
            line(9113, weatherCity.empty() ? L"Choose a city" : weatherCity,
                 weather.available
                     ? Number(weather.temperature) + L"\x00B0 \x00B7 " + WeatherService::Condition(weather.code) +
                           L" \x00B7 " + Number(weather.low) + L"\x2013" + Number(weather.high) + L"\x00B0"
                     : weather.message,
                 L"\xE706");
    }
    buildingBody = false;
    if (!layoutOnly) {
        target->SetTransform(saved);
        target->PopAxisAlignedClip();
    }
    header();
}

bool Island::UnifiedAction(int id) {
    if (id >= 9270 && id < 9300) {
        Open(L"ms-settings:sound");
        return true;
    }
    if (id >= 1200 && id < 1328 && surfaceRequest.kind == surface::Kind::QuickPanel) {
        size_t index = static_cast<size_t>(id - 1200);
        if (index < connections.devices.size() && controls::MayWrite(renderMode, fixtureGeometry, previewMode) &&
            connectivity && PrepareSystemChange())
            connectivity->BluetoothAction(connections.devices[index], connections.devices[index].paired ? 0 : 2);
        return true;
    }
    if (id == 9100) {
        ClearSurfaceThumbnails();
        surfaceRequest = {};
        surfaceHover.pinned = true;
        SetTab(Tab::Today);
        return true;
    }
    if (id == 9110) {
        auto request = surfaceRequest;
        request.kind = surface::Kind::QuickPanel;
        request.width = surface::Width::Standard;
        request.behavior = surface::Behavior::Panel;
        RequestSurface(request, {}, panelMonitor);
        return true;
    }
    if (id == 9111) {
        Invoke(Play);
        return true;
    }
    if (id == 9112) {
        Open(L"ms-settings:batterysaver");
        return true;
    }
    if (id == 9113) {
        TodayAction(WeatherCity);
        return true;
    }
    if (id >= 1000 && id < 1128 && surfaceRequest.kind == surface::Kind::QuickPanel) {
        auto index = static_cast<size_t>(id - 1000);
        if (index >= connections.networks.size() || !controls::MayWrite(renderMode, fixtureGeometry, previewMode))
            return true;
        auto network = connections.networks[index];
        if (network.connected)
            return true;
        std::wstring key;
        if (network.secure && network.profile.empty()) {
            auto entered = PromptText(window, L"Network security key", L"Connect to " + network.name, L"", 64);
            if (!entered)
                return true;
            key = std::move(*entered);
        }
        if (!PrepareSystemChange())
            return true;
        if (!connectivity)
            connectivity = std::make_unique<Connectivity>(window);
        connectivity->ConnectWifi(network, std::move(key));
        return true;
    }
    if (id >= 9120 && id <= 9123) {
        auto group = surfaceGroup;
        if (id == 9120)
            LaunchPin(group.launchKey);
        else if (id == 9121) {
            if (group.pinned)
                for (const auto &key : group.pinKeys)
                    PinToDock(key, false);
            else
                PinToDock(group.launchKey, true);
        } else if (id == 9122 && !group.windows.empty() && CurrentSurfaceWindow(group.windows.front()) &&
                   controls::MayWrite(renderMode, fixtureGeometry, previewMode))
            desktop::WindowList::Close(group.windows.front().window);
        else if (id == 9123)
            OpenRecent(group.appId, panelMonitor);
        if (id != 9123)
            SetExpanded(false);
        return true;
    }
    if (id >= 20000 && id < 30000) {
        size_t index = static_cast<size_t>(id - 20000);
        if (index < surfaceGroup.windows.size() && CurrentSurfaceWindow(surfaceGroup.windows[index]) && !renderMode &&
            !fixtureGeometry && desktop::WindowList::Activate(surfaceGroup.windows[index].window))
            SetExpanded(false);
        return true;
    }
    if (id >= 30000 && id < 30003) {
        size_t index = static_cast<size_t>(id - 30000);
        if (index < surfaceGroup.windows.size() && CurrentSurfaceWindow(surfaceGroup.windows[index]) &&
            controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
            desktop::WindowList::Close(surfaceGroup.windows[index].window);
            ClearSurfaceThumbnails();
            surfaceGroup.windows.erase(surfaceGroup.windows.begin() + index);
            if (surfaceGroup.windows.empty())
                SetExpanded(false);
            else {
                RetargetLayout();
                RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return true;
    }
    if (id >= 10000 && id < 20000) {
        size_t index = static_cast<size_t>(id - 10000);
        if (index < runningWindows.size() && CurrentSurfaceWindow(runningWindows[index]) && !renderMode &&
            !fixtureGeometry && desktop::WindowList::Activate(runningWindows[index].window))
            SetExpanded(false);
        return true;
    }
    return false;
}

void Island::ClearSurfaceThumbnails() {
    for (auto &thumbnail : surfaceThumbnails) {
        if (thumbnail)
            DwmUnregisterThumbnail(thumbnail);
        thumbnail = nullptr;
    }
    thumbnailSlots = {};
}

void Island::UpdateSurfaceThumbnails() {
    if (renderMode || capturingPane || !DockEnabled() || surfaceRequest.kind != surface::Kind::Preview || !expanded)
        return;
    float scale = dpi / 96, alpha = std::clamp(contentBlend.value * paneBlend.value, 0.0f, 1.0f);
    auto body = Body(), nav = TabRect(-1);
    if (edge == anchor::Edge::Bottom)
        body.bottom = std::min(body.bottom, nav.top - layout::Metrics::Gutter);
    else
        body.top = std::max(body.top, nav.bottom + layout::Metrics::Gutter);
    float blend = std::clamp(contentBlend.value, 0.0f, 1.0f), zoom = .98f + .02f * blend;
    auto origin = D2D1::Point2F(LayoutWidth() / 2, edge == anchor::Edge::Bottom ? height / textScale : 0);
    auto presented = D2D1::Matrix3x2F::Translation(bodyDirection * 8 * (1 - paneBlend.value), 0) *
                     D2D1::Matrix3x2F::Scale(zoom, zoom, origin) *
                     D2D1::Matrix3x2F::Translation(0, -anchor::Grow(edge) * 8 * (1 - blend)) *
                     D2D1::Matrix3x2F::Scale(textScale, textScale) * D2D1::Matrix3x2F::Translation(PanelX(), PanelY());
    auto *outline = SurfacePath();
    for (size_t i = 0; i < surfaceThumbnails.size(); ++i) {
        auto r = thumbnailSlots[i];
        float inset = (highContrast ? 2.0f : 1.0f) * 96 / dpi / textScale;
        auto first = presented.TransformPoint({r.left + inset, r.top - scroll + inset});
        auto last = presented.TransformPoint({r.right - inset, r.bottom - scroll - inset});
        bool visible = i < surfaceGroup.windows.size() && r.right > r.left && r.top - scroll >= body.top &&
                       r.bottom - scroll <= body.bottom && alpha > .001f && shape::Contains(outline, first) &&
                       shape::Contains(outline, last) && shape::Contains(outline, {first.x, last.y}) &&
                       shape::Contains(outline, {last.x, first.y});
        if (!surfaceThumbnails[i] && visible) {
            auto source = surfaceGroup.windows[i].window;
            DWORD process = 0;
            GetWindowThreadProcessId(source, &process);
            if (IsWindow(source) && process == surfaceGroup.windows[i].process &&
                (!fixtureGeometry || process == GetCurrentProcessId()))
                DwmRegisterThumbnail(window, source, &surfaceThumbnails[i]);
        }
        if (!surfaceThumbnails[i])
            continue;
        DWM_THUMBNAIL_PROPERTIES props{};
        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        props.rcDestination = {
            static_cast<LONG>(std::lround(first.x * scale)), static_cast<LONG>(std::lround(first.y * scale)),
            static_cast<LONG>(std::lround(last.x * scale)), static_cast<LONG>(std::lround(last.y * scale))};
        props.fVisible = visible;
        props.fSourceClientAreaOnly = TRUE;
        props.opacity = static_cast<BYTE>(alpha * 255);
        DwmUpdateThumbnailProperties(surfaceThumbnails[i], &props);
    }
}
