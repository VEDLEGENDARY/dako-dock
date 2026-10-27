#pragma once
// Included by Island.cpp after the Island declaration.
bool Island::DockEnabled() const {
    return dockMode && (previewMode || desktopRequested) && !renderMode && !fixtureGeometry;
}

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
            SetExpanded(true);
            keyboardOpen = true;
            SetForegroundWindow(window);
        };
        callbacks.recent = [this](const std::wstring &appId, HMONITOR monitor) { OpenRecent(appId, monitor); };
        callbacks.context = [this] { ContextMenu(); };
        docks = std::make_unique<desktop::DockShell>();
        if (!docks->Start(window, &shell, previewMode, textScale, std::move(callbacks))) {
            docks.reset();
            shell.Stop();
            dockMode = false;
            desktopRequested = false;
            note = L"The dock could not reserve space safely. Dako is using the top notch for this session.";
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
        panelMonitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
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
    status.wifi = system.wifi;
    status.volume = system.muted ? L"Muted" : Number(system.volume * 100) + L" percent";
    status.weather = weather.available     ? Number(weather.temperature) + L"°C"
                     : weatherCity.empty() ? L"Weather off"
                                           : L"Weather";
    docks->Options(dockOptions);
    docks->Appearance(reducedMotion, highContrast, textScale);
    docks->SetData(std::move(pins), runningWindows, std::move(status));
    if (catalog) {
        std::vector<std::wstring> keys;
        for (const auto &key : appPreferences.favorites)
            keys.push_back(key);
        catalog->RequestIcons(std::move(keys));
    }
}
void Island::OpenFromDock(int action, HMONITOR monitor) {
    if (!docks)
        return;
    bool sameMonitor = panelMonitor == monitor;
    Tab next = action == desktop::DockApps || action == desktop::DockSearch || action == desktop::DockRecent ? Tab::Apps
               : action == desktop::DockWifi || action == desktop::DockVolume ? Tab::Controls
                                                                              : Tab::Today;
    if (expanded && sameMonitor && tab == next && action != desktop::DockSearch && action != desktop::DockStatusMore &&
        action != desktop::DockRecent && flyout == Flyout::None) {
        SetExpanded(false);
        return;
    }
    panelMonitor = monitor;
    dockAnchor = docks->Anchor(monitor, action);
    RECT bounds = docks->Bounds(monitor);
    UINT x = 96, y = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y);
    dpi = static_cast<float>(x);
    ReleaseGraphics();
    SetWindowPos(window, HWND_TOPMOST, bounds.left, bounds.top - 1, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    RefreshGeometry();
    if (action == desktop::DockSearch) {
        OpenPalette();
        return;
    }
    if (action == desktop::DockStatusMore) {
        flyout = Flyout::DockStatus;
        SetExpanded(true);
        UpdateTimers();
        RebuildLayout();
        PositionInputs();
        InvalidateRect(window, nullptr, FALSE);
        keyboardOpen = true;
        SetForegroundWindow(window);
        SetFocus(window);
        return;
    }
    if (action == desktop::DockWifi)
        OpenFlyout(Flyout::Wifi);
    else
        SetTab(next);
    keyboardOpen = true;
    launchPreview = false;
    SetForegroundWindow(window);
    SetFocus(window);
    if (action == desktop::DockSearch) {
        EnsureAppSearch();
        RebuildLayout();
        FocusAt(IndexForKey(StableKey(401)));
    }
    if (action == desktop::DockVolume) {
        RebuildLayout();
        FocusAt(IndexForKey(StableKey(Volume)));
    }
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
    return b.top + (CompactApps() ? 40.0f : 56.0f) + (ShowAppRail() ? 76.0f : 0.0f) +
           (b.bottom - b.top >= 224 && !runningIndices.empty() ? 100.0f : 0.0f);
}

void Island::ConfirmDesktopChoice() {
    if (renderMode || previewMode || fixtureGeometry || desktopPromptDone)
        return;
    desktopPromptDone = true;
    if (!desktopRequested) {
        ModalGuard guard(modalOpen);
        desktopRequested = MessageBoxW(window,
                                       L"Use Dako as my taskbar?\n\nDako will hide Explorer's taskbar and reserve "
                                       L"space for the floating dock. You can restore Explorer from Dako's menu.",
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
