#include "Island.h"
#include "SettingsPageModel.h"

layout::Result Island::Layout() const {
    layout::View v = miniMode                 ? layout::View::Media
                     : flyout != Flyout::None ? layout::View::Wireless
                                              : static_cast<layout::View>(tab);
    auto result = layout::Solve({std::max(100.0f, (envelopeWidth - 40) / textScale),
                                 (DockEnabled() ? envelopeHeight - 20 : envelopeHeight - PanelY() - 12) / textScale, v,
                                 filteredApps.size()});
    if (v == layout::View::Controls)
        result.contentHeight = result.narrow ? 576.0f : 448.0f;
    if (flyout == Flyout::DockStatus)
        result.contentHeight = 304;
    if (flyout == Flyout::Features)
        result.contentHeight = 520 + static_cast<float>(moduleRegistry.Descriptors().size()) * 160;
    if (flyout == Flyout::Features && featuresPage == 2) {
        result.contentHeight += 520 + static_cast<float>(privacySnapshot.active.size()) * 88 +
                                static_cast<float>(std::min(size_t{8}, privacySnapshot.events.size())) * 122;
    }
    if (flyout == Flyout::Features && featuresPage == 3)
        result.contentHeight = 512 + static_cast<float>(moduleRegistry.Descriptors().size()) * 256;
    if (flyout == Flyout::Palette || flyout == Flyout::Recent || flyout == Flyout::Module)
        result.contentHeight = result.body.Height();
    if (flyout == Flyout::Module && modulePanel.image)
        result.contentHeight = std::max(result.body.Height(),
            std::max(24.0f, std::min(96.0f, result.body.Height() - 56)) + 56);
    if (v == layout::View::Wallpaper)
        result.contentHeight = std::max(360.0f, result.body.Height());
    if (v == layout::View::Stats) {
        int columns = result.panel.Width() >= 720 ? 2 : 1;
        result.contentHeight =
            std::max(360.0f, (columns == 2 ? 272.0f : 508.0f) +
                                 static_cast<float>((hardwareState.volumes.size() + columns - 1) / columns) * 52 + 32);
    }
    return result;
}

D2D1_RECT_F Island::R(layout::Rect r) {
    return {r.left, r.top, r.right, r.bottom};
}

D2D1_RECT_F Island::Body() const {
    return R(Layout().body);
}

D2D1_RECT_F Island::Footer() const {
    return R(Layout().footer);
}

bool Island::CanInvoke(const Hit &hit) const {
    return hit.enabled && VisibleHit(hit) && ((hit.id == Expand && !expanded && !YieldResting()) || Actionable());
}

bool Island::Actionable() const {
    return expanded && !capturingPane;
}

int Island::IndexForKey(const std::wstring &key) const {
    for (size_t i = 0; i < hits.size(); ++i)
        if (hits[i].key == key)
            return static_cast<int>(i);
    return -1;
}

int Island::IndexForChild(LONG child) const {
    for (size_t i = 0; i < hits.size(); ++i)
        if (hits[i].child == child)
            return static_cast<int>(i);
    return -1;
}

std::wstring Island::StableKey(int id) const {
    if ((id >= 5100 && id < 5120) || (id >= 7200 && id < 7220)) {
        size_t slot = static_cast<size_t>(id - (id >= 7200 ? 7200 : 5100));
        size_t index = modulePage * ModuleSlots() + slot;
        if (index < modulePanel.rows.size())
            return L"module:" + moduleView + L":" + (modulePanel.rows[index].action.empty() ?
                std::to_wstring(index) : modulePanel.rows[index].action);
    }
    if (id >= 200 && id < 220 && id - 200 < static_cast<int>(tasks.size()))
        return L"task:" + std::to_wstring(tasks[id - 200].identity);
    if (id >= 220 && id < 240 && id - 220 < static_cast<int>(tasks.size()))
        return L"delete:" + std::to_wstring(tasks[id - 220].identity);
    if (id >= 300 && id < 400 && AppIndex(id - 300) < apps.size())
        return L"app:" + AppKey(apps[AppIndex(id - 300)]);
    if (id >= 900 && id < 1000 && AppIndex(id - 900) < apps.size())
        return L"star:" + AppKey(apps[AppIndex(id - 900)]);
    if (id >= 1400 && id < 1410 && id - 1400 < static_cast<int>(railApps.size()))
        return L"rail:" + AppKey(apps[railApps[id - 1400]]);
    if (id >= 500 && id < 564 && id - 500 < static_cast<int>(wallpapers.size()))
        return L"wallpaper:" + wallpapers[id - 500].path;
    if (id >= 1000 && id < 1128 && id - 1000 < static_cast<int>(connections.networks.size()))
        return WifiKey(connections.networks[id - 1000]);
    if (id >= 1200 && id < 1328 && id - 1200 < static_cast<int>(connections.devices.size()))
        return BluetoothKey(connections.devices[id - 1200]);
    if (id >= 2400 && id < 2402) {
        size_t slot = runningPage * 2 + static_cast<size_t>(id - 2400);
        if (slot < runningIndices.size())
            return L"running:" +
                   std::to_wstring(reinterpret_cast<UINT_PTR>(runningWindows[runningIndices[slot]].window));
    }
    if (id >= 2600 && id < 2620) {
        size_t index = palettePage * PaletteSlots() + static_cast<size_t>(id - 2600);
        if (index < paletteItems.size())
            return L"palette:" + paletteItems[index].key;
    }
    if (ReadingControl(id) && id != 1800)
        return L"reading:" + std::to_wstring(static_cast<int>(tab)) + L":" + std::to_wstring(miniMode) + L":" +
               std::to_wstring(id);
    return L"action:" + std::to_wstring(id);
}

std::wstring Island::WifiKey(const WifiNetwork &n) {
    wchar_t guid[40]{};
    StringFromGUID2(n.adapter, guid, 40);
    return L"wifi:" + std::wstring(guid) + L":" + n.name + L":" + std::to_wstring(n.authentication) + L":" +
           std::to_wstring(n.cipher);
}

std::wstring Island::BluetoothKey(const BluetoothEntry &d) {
    return L"bt:" + (d.identifier.empty() ? std::to_wstring(d.native.Address.ullLong) : d.identifier);
}

size_t Island::AppIndex(size_t slot) const {
    return appPage < appPages.size() && appPages[appPage].begin + slot < appPages[appPage].end
               ? filteredApps[appPages[appPage].begin + slot]
               : apps.size();
}

bool Island::VisibleHit(const Hit &h) const {
    auto r = h.rect, c = h.clip;
    return r.bottom > c.top && r.top < c.bottom && r.right > c.left && r.left < c.right;
}

void Island::ReplaceWallpapers(std::vector<WallpaperEntry> next) {
    std::wstring selected = selectedWallpaper < wallpapers.size() ? wallpapers[selectedWallpaper].path : L"";
    wallpapers = std::move(next);
    selectedWallpaper = 0;
    for (size_t i = 0; i < wallpapers.size(); ++i)
        if (wallpapers[i].path == selected)
            selectedWallpaper = i;
    wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));
    thumbnails.clear();
}

void Island::RequestVisibleIcons() {
    if (!catalog || renderMode || tab != Tab::Apps)
        return;
    std::vector<std::wstring> keys;
    for (size_t i : railApps)
        keys.push_back(AppKey(apps[i]));
    for (size_t slot = 0; slot < AppSlots(); ++slot) {
        size_t i = AppIndex(slot);
        if (i < apps.size())
            keys.push_back(AppKey(apps[i]));
    }
    catalog->RequestIcons(std::move(keys));
}

void Island::RebuildLayout() {
    moduleRegistry.Visible(expanded && flyout == Flyout::Module ? moduleView : L"");
    int previous = focused;
    bool hadFocus = !focusedKey.empty();
    bool saved = layoutOnly;
    layoutOnly = true;
    PanelContent();
    layoutOnly = saved;
    focused = IndexForKey(focusedKey);
    hovered = IndexForKey(hoveredKey);
    if (focused < 0 && hadFocus && !hits.empty()) {
        int start = std::clamp(previous, 0, static_cast<int>(hits.size()) - 1);
        for (int distance = 0; distance < static_cast<int>(hits.size()); ++distance) {
            int i = (start + distance) % static_cast<int>(hits.size());
            if (hits[i].enabled) {
                focused = i;
                focusedKey = hits[i].key;
                break;
            }
        }
    }
    if (focused < 0)
        focusedKey.clear();
    if (!pressedKey.empty() && IndexForKey(pressedKey) < 0) {
        pressedKey.clear();
        pressedId = 0;
    }
    if (rowPositions.size() > 256) {
        for (auto it = rowPositions.begin(); it != rowPositions.end();)
            if (IndexForKey(it->first) < 0)
                it = rowPositions.erase(it);
            else
                ++it;
    }
    if (childIds.size() > 8192) {
        for (auto it = childIds.begin(); it != childIds.end();)
            if (IndexForKey(it->first) < 0)
                it = childIds.erase(it);
            else
                ++it;
    }
    std::wstring status = note;
    if (flyout == Flyout::Wifi || flyout == Flyout::Bluetooth)
        status += flyout == Flyout::Wifi ? connections.wifiMessage : connections.bluetoothMessage;
    if (status != lastStatus) {
        lastStatus = status;
        LONG child = CHILDID_SELF;
        for (const auto &hit : hits)
            if (hit.id == 1800 || hit.id == 2040)
                child = hit.child;
        NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, window, OBJID_CLIENT, child);
    }
}

void Island::RefreshGeometry() {
    wallpaperReveal.Cancel();
    Place();
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, Layout().contentHeight - Layout().body.Height()));
    dockWidth.Snap(expanded ? SurfaceWidth() : theme::DockWidth);
    dockHeight.Snap(expanded ? SurfaceHeight() : theme::DockHeight);
    width = dockWidth.value;
    height = dockHeight.value;
    paneBlend.Snap(1);
    previousPane.Reset();
    transitioning = false;
    RebuildLayout();
    Place();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::SnapMotion() {
    for (auto *v : {&dockReveal, &dockWidth, &dockHeight, &paneBlend, &wallpaperFocus, &tabLeft, &tabRight,
                    &railPointer, &railStrength, &paletteBlend, &contentBlend, &togglePosition, &wallpaperParallax,
                    &volumeMotion, &composerFocus})
        v->Snap(v->goal);
    for (auto &v : rings)
        v.Snap(v.goal);
    for (auto &[key, v] : hoverMotion) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : tileState) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : feedback) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : rowPositions) {
        (void)key;
        v.Snap(v.goal);
    }
    previousPane.Reset();
    wallpaperReveal.Cancel();
    frameClock.Stop();
    frameTickReady = false;
    scheduler.Stop();
    KillTimer(window, TransitionTimer);
    transitioning = false;
    width = dockWidth.value;
    height = dockHeight.value;
    Place();
    if (!expanded && !renderMode)
        ReleaseGraphics();
}

bool Island::YieldResting() const {
    return DockEnabled() || browserContext || fullscreenContext;
}

std::wstring Island::AppKey(const AppEntry &app) const {
    return app.parsingName.empty() ? app.name : app.parsingName;
}

bool Island::Favorite(const AppEntry &app) const {
    return std::find(appPreferences.favorites.begin(), appPreferences.favorites.end(), AppKey(app)) !=
           appPreferences.favorites.end();
}

int Island::AppGroup(const AppEntry &app) const {
    return Favorite(app) ? 0
           : std::find(appPreferences.recent.begin(), appPreferences.recent.end(), AppKey(app)) !=
                   appPreferences.recent.end()
               ? 1
               : 2;
}

void Island::SampleStats() {
    if (renderMode || fixtureGeometry || !expanded || tab != Tab::Stats || flyout != Flyout::None)
        return;
    system.Sample();
    if (!hardware)
        hardware = std::make_unique<HardwareStats>(window);
    hardware->Request();
}

unsigned Island::SystemColor(int index) {
    auto c = GetSysColor(index);
    return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
}

double Island::Luminance(unsigned color) {
    auto channel = [](unsigned v) {
        double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((color >> 16) & 255) + 0.7152 * channel((color >> 8) & 255) + 0.0722 * channel(color & 255);
}

double Island::Contrast(unsigned a, unsigned b) {
    double x = Luminance(a), y = Luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

void Island::LoadSettings() {
    SettingsStore store;
    preferencesPath = store.Path();
    if (!previewMode && !store.Migrate())
        note = L"Could not migrate settings; original files are preserved.";
    auto values = store.Get(L"Dako");
    auto number = [&](const wchar_t *key, int fallback) {
        auto found = values.find(key);
        if (found == values.end())
            return fallback;
        try {
            return std::stoi(found->second);
        } catch (...) {
            return fallback;
        }
    };
    auto today = store.Get(L"Today");
    weatherCity = today[L"WeatherCity"];
    calendarUrl = today[L"CalendarUrl"];
    localEvents = store.Get(L"CalendarEvents");
    RebuildCalendar();
    dockMode = desktop::NormalizeLayout(values[L"layoutMode"]) == L"dock";
    desktopPromptDone = number(L"DesktopPromptDone", 0) != 0;
    desktopRequested = desktopRequested || number(L"DesktopMode", 0) != 0;
    dockOptions.statusMask = static_cast<unsigned>(number(L"DockStatusMask", 31)) & 31;
    dockOptions.showDesktop = number(L"DockShowDesktop", 0) != 0;
    dockOptions.showTaskView = number(L"DockShowTaskView", 0) != 0;
    if (values[L"layoutMode"] == L"bar" && !previewMode) {
        values[L"layoutMode"] = L"dock";
        store.Update(L"Dako", values);
    }
    if (!layoutOverride.empty())
        dockMode = layoutOverride == L"dock";
    motionOverride = number(L"ReducedMotion", 0) != 0;
    opaqueMaterial = number(L"Opaque", 1) != 0;
    int appearance = number(L"Appearance", 1);
    systemAppearance = appearance == 0;
    lightAppearance = appearance == 1;
    browserClearance = static_cast<float>(std::clamp(number(L"BrowserClearance", 112), 64, 240));
    fixedAccent = number(L"FixedAccent", 1) != 0;
    shell.SetMaximizeNew(number(L"MaximizeNew", 0) != 0);
}
bool Island::SaveSettings() {
    if (renderMode || previewMode)
        return true;
    SettingsStore store(preferencesPath);
    auto values = store.Get(L"Dako");
    SettingsStore::Section changed{{L"layoutMode", dockMode ? L"dock" : L"notch"},
                                   {L"DesktopPromptDone", desktopPromptDone ? L"1" : L"0"},
                                   {L"DesktopMode", desktopRequested ? L"1" : L"0"},
                                   {L"DockStatusMask", std::to_wstring(dockOptions.statusMask)},
                                   {L"DockShowDesktop", dockOptions.showDesktop ? L"1" : L"0"},
                                   {L"DockShowTaskView", dockOptions.showTaskView ? L"1" : L"0"},
                                   {L"ReducedMotion", motionOverride ? L"1" : L"0"},
                                   {L"Opaque", opaqueMaterial ? L"1" : L"0"},
                                   {L"Appearance", systemAppearance  ? L"0"
                                                   : lightAppearance ? L"1"
                                                                     : L"2"},
                                   {L"BrowserClearance", std::to_wstring(static_cast<int>(browserClearance))},
                                   {L"MaximizeNew", shell.MaximizeNew() ? L"1" : L"0"},
                                   {L"FixedAccent", fixedAccent ? L"1" : L"0"}};
    for (auto &entry : changed)
        values[entry.first] = entry.second;
    return store.Update(L"Dako", std::move(values));
}

void Island::ReadTextScale() {
    if (renderMode)
        return;
    DWORD percent = 100, bytes = sizeof(percent);
    RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Accessibility)", L"TextScaleFactor", RRF_RT_REG_DWORD,
                 nullptr, &percent, &bytes);
    float next = std::clamp(static_cast<float>(percent) / 100, 1.0f, 2.25f);
    if (next == textScale)
        return;
    textScale = next;
    ReleaseGraphics();
    if (editFont) {
        DeleteObject(editFont);
        editFont = nullptr;
    }
    EnsureEditResources();
    for (HWND input : {taskEdit, appSearch, wifiPassword, clipboardEdit})
        if (input)
            SendMessageW(input, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
}

void Island::ConfigureAppearance(bool forceHighContrast) {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    highContrast = forceHighContrast || (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (systemAppearance && !renderMode) {
        DWORD light = 0, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        lightAppearance = light != 0;
    }
    theme::Surface = lightAppearance ? theme::Paper : theme::Ink;
    theme::Card = lightAppearance ? 0xF7F5EF : 0x1B1B18;
    theme::CardHover = lightAppearance ? 0xDEDBD4 : 0x35352F;
    theme::Inset = lightAppearance ? 0xE6E3DB : 0x242420;
    theme::Border = lightAppearance ? 0xCFCCC5 : 0x3B3B35;
    theme::Text = lightAppearance ? theme::Ink : theme::Paper;
    theme::Muted = lightAppearance ? 0x65635E : 0xB5B2A9;
    if (highContrast) {
        theme::Surface = SystemColor(COLOR_WINDOW);
        theme::Card = theme::Inset = SystemColor(COLOR_BTNFACE);
        theme::CardHover = SystemColor(COLOR_BTNFACE);
        theme::Text = SystemColor(COLOR_WINDOWTEXT);
        theme::Muted = SystemColor(COLOR_BTNTEXT);
        theme::Border = theme::Text;
    }
    ApplyAccent(fixedAccent ? theme::Signal : targetAccent);
    ConfigureMaterial();
    if (editBackground) {
        DeleteObject(editBackground);
        editBackground = nullptr;
    }
    EnsureEditResources();
    UpdateDocks();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::ApplyAccent(unsigned rgb) {
    if (fixedAccent)
        rgb = theme::Signal;
    if (highContrast) {
        rgb = SystemColor(COLOR_HIGHLIGHT);
        theme::Accent = theme::AccentSoft = theme::AccentHover = rgb;
        theme::SelectedText = SystemColor(COLOR_HIGHLIGHTTEXT);
        if (Contrast(theme::SelectedText, rgb) < 4.5)
            theme::SelectedText = Contrast(0, rgb) > Contrast(0xFFFFFF, rgb) ? 0 : 0xFFFFFF;
        theme::AccentInk = theme::SelectedText;
        theme::Focus = SystemColor(COLOR_WINDOWTEXT);
        theme::Secondary = theme::Tertiary = theme::Focus;
        return;
    }
    theme::Accent = rgb;
    theme::AccentHover = dock::Mix(rgb, lightAppearance ? theme::Ink : theme::Paper, 0.10f);
    theme::AccentInk = Contrast(rgb, theme::Ink) > Contrast(rgb, theme::Paper) ? theme::Ink : theme::Paper;
    theme::AccentSoft = dock::Mix(theme::Surface, rgb, 0.12f);
    unsigned readable = rgb;
    for (int i = 0; i < 32 && Contrast(readable, theme::AccentSoft) < 4.5; ++i)
        readable = dock::Mix(readable, lightAppearance ? 0x000000 : 0xFFFFFF, 0.08f);
    theme::SelectedText = theme::Focus = readable;
    theme::Secondary = lightAppearance ? 0x526E62 : 0xA5BCAE;
    theme::Tertiary = lightAppearance ? 0x546C82 : 0xA3B8CC;
}

void Island::SetAccent(unsigned rgb) {
    if (fixedAccent)
        rgb = theme::Signal;
    if (rgb == targetAccent)
        return;
    previousAccent = theme::Accent;
    targetAccent = rgb;
    paletteBlend.Snap(0);
    paletteBlend.goal = 1;
    if (reducedMotion || renderMode) {
        paletteBlend.Snap(1);
        ApplyAccent(rgb);
    } else
        ArmMotion();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::UpdateContext() {
    if (renderMode)
        return;
    if (DockEnabled()) {
        browserContext = false;
        fullscreenContext = false;
        if (docks)
            docks->RefreshFullscreen();
        return;
    }
    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!foreground || processId == GetCurrentProcessId() || GetWindow(foreground, GW_OWNER) == window ||
        desktop::IsTaskbar(foreground))
        return;
    bool wasFullscreen = fullscreenContext;
    RECT activeBounds{};
    MONITORINFO activeMonitor{sizeof(activeMonitor)};
    GetWindowRect(foreground, &activeBounds);
    GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &activeMonitor);
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    fullscreenContext = SUCCEEDED(SHQueryUserNotificationState(&state)) && desktop::HideForNotificationState(state);
    if (foreground == contextWindow) {
        if (wasFullscreen != fullscreenContext) {
            if (expanded)
                RefreshGeometry();
            else
                Place();
        }
        return;
    }
    bool wasBrowser = browserContext;
    contextWindow = foreground;
    browserContext = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process) {
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (QueryFullProcessImageNameW(process, 0, path, &length))
            browserContext = dock::BrowserExecutable(std::wstring_view(path, length));
        CloseHandle(process);
    }
    wchar_t name[64]{};
    GetClassNameW(foreground, name, 64);
    if (!wcscmp(name, L"MozillaWindowClass"))
        browserContext = true;
    if (wasBrowser != browserContext || wasFullscreen != fullscreenContext) {
        if (expanded)
            RefreshGeometry();
        else
            Place();
    }
}

Island::~Island() {
    clipboardReadTask.Stop();
    if (clipboardReadToken)
        privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken));
    moduleRegistry.Stop();
    StopDesktop();
    frameClock.Stop();
    if (backdropWindow)
        DestroyWindow(backdropWindow);
    if (editFont)
        DeleteObject(editFont);
    if (editBackground)
        DeleteObject(editBackground);
}

void Island::PreviewTrace(const char *stage) {
    if (!previewMode)
        return;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    auto path = std::filesystem::path(executable).parent_path() / L"preview-startup.txt";
    privacy::Write audit(L"diagnostics", L"file-write", path.wstring(), L"preview-startup-trace");
    std::ofstream trace(path, std::ios::app);
    trace << GetTickCount64() << " " << stage << "\n";
    trace.flush();
    audit.Complete(static_cast<bool>(trace));
}

void Island::Initialize() {
    PreviewTrace("initialize");
    if (previewMode)
        SetPropW(window, L"Dako.IsolatedPreview", reinterpret_cast<HANDLE>(1));
    dpi = static_cast<float>(GetDpiForWindow(window));
    BOOL animation = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
    systemReduced = !animation;
    reducedMotion = systemReduced;
    if (!renderMode)
        LoadSettings();
    if (!renderMode && !fixtureGeometry) {
        privacy::Policy().SetOffline(SettingsStore(preferencesPath).Get(L"Privacy")[L"Offline"] == L"1");
        privacy::Policy().Observe(window);
    }
    modules::RegisterBuiltins(moduleRegistry);
    moduleRegistry.Owner(window);
    moduleRegistry.Mode(renderMode || fixtureGeometry, previewMode);
    if (!renderMode && !fixtureGeometry)
        commandCatalog.Restore(SettingsStore(preferencesPath).Get(L"CommandRanking"));
    if (!renderMode && !fixtureGeometry && !moduleRegistry.Load(SettingsStore(preferencesPath)))
        note = modules::String(IDS_START_FAILED);
    reducedMotion = systemReduced || motionOverride;
    ReadTextScale();
    ConfigureAppearance();
    CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibleProperties));
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(textFactory.GetAddressOf())));
    const float sizes[]{12, 12, 14, 16, 28, 38, 18, 13, 32, 11};
    ComPtr<IDWriteFontCollection> fontCollection;
    UINT32 iconFamily = 0;
    BOOL hasFluent = FALSE;
    if (SUCCEEDED(textFactory->GetSystemFontCollection(&fontCollection)))
        fontCollection->FindFamilyName(L"Segoe Fluent Icons", &iconFamily, &hasFluent);
    const wchar_t *iconFont = hasFluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    for (size_t i = 0; i < formats.size(); ++i) {
        const wchar_t *family = i == type::Icon                                                   ? iconFont
                                : i == type::Editorial || i == type::Display || i == type::Metric ? L"Georgia"
                                : i == type::Mono                                                 ? L"Consolas"
                                                                                                  : L"Segoe UI";
        Check(textFactory->CreateTextFormat(
            family, nullptr, i == 3 || i == 7 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizes[i], locale, &formats[i]));
        formats[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        textFactory->CreateEllipsisTrimmingSign(formats[i].Get(), &ellipsis);
        formats[i]->SetTrimming(&trim, ellipsis.Get());
    }
    int corner = 1;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    PreviewTrace("native-services");
    if (!renderMode && !fixtureGeometry)
        system.Initialize(window);
    PreviewTrace("native-services-ready");
    if (!renderMode) {
        catalog = std::make_unique<DesktopCatalog>(window);
        appPreferences.Load();
        for (const auto &task : taskStore.Load())
            tasks.push_back({task.first, task.second, nextTask++});
        if (!previewMode) {
            toggleHotkey = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE) != FALSE;
            quitHotkey = RegisterHotKey(window, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != FALSE;
            paletteHotkey = RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K') != FALSE;
            taskbarHotkey = RegisterHotKey(window, 4, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T') != FALSE;
        }
    }
    Place();
    if (!renderMode && !previewMode && !toggleHotkey)
        note = L"Global shortcut unavailable. Use the tray menu.";
    RebuildLayout();
    PreviewTrace("initialized");
    if (!renderMode && !previewMode && !DockEnabled()) {
        hotZone.Initialize(window);
        hotZone.Place(window, true, static_cast<UINT>(dpi));
    }
}

void Island::ReleaseGraphics() {
    frameClock.Stop();
    frameTickReady = false;
    if (backdropWindow)
        ShowWindow(backdropWindow, SW_HIDE);
    previousPane.Reset();
    wallpaperMask.Reset();
    appIcons.clear();
    thumbnails.clear();
    brush.Reset();
    target.Reset();
    hostTarget.Reset();
    composition.Reset();
    if (scheduler.active && !renderMode && !reducedMotion)
        SetTimer(window, TransitionTimer, motionInterval, nullptr);
    if (!renderMode)
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_NOREDIRECTIONBITMAP);
}

float Island::LayoutWidth() const {
    return Layout().panel.Width();
}

float Island::LayoutHeight() const {
    return Layout().panel.Height();
}

float Island::SurfaceWidth() const {
    return LayoutWidth() * textScale;
}

float Island::SurfaceHeight() const {
    return LayoutHeight() * textScale;
}

float Island::ContentWidth() const {
    return LayoutWidth();
}

float Island::PanelX() const {
    return renderMode ? 0 : (envelopeWidth - SurfaceWidth()) / 2;
}

float Island::HandleY() const {
    return 0;
}

float Island::PanelY() const {
    return renderMode       ? 0
           : DockEnabled()  ? std::max(0.0f, envelopeHeight - height - 8)
           : browserContext ? std::min(browserClearance, std::max(12.0f, envelopeHeight - 272.0f))
                            : 12.0f;
}

std::array<POINT, 35> Island::NotchPixels(float scale) const {
    auto curve = dock::Notch(renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2, HandleY(), theme::DockWidth,
                             theme::DockHeight);
    std::array<POINT, 35> points{};
    for (size_t i = 0; i < points.size(); ++i)
        points[i] = {static_cast<LONG>(std::lround(curve[i].x * scale)),
                     static_cast<LONG>(std::lround(curve[i].y * scale))};
    return points;
}

void Island::PaintNotch(HDC dc) {
    if (YieldResting())
        return;
    auto points = NotchPixels(dpi / 96);
    HBRUSH fill = CreateSolidBrush(RGB(13, 13, 15));
    auto oldBrush = SelectObject(dc, fill);
    auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(fill);
}

void Island::DrawNotch() {
    auto points = dock::Notch(renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2, HandleY(), theme::DockWidth,
                              theme::DockHeight);
    ComPtr<ID2D1PathGeometry> geometry;
    Check(factory->CreatePathGeometry(&geometry));
    ComPtr<ID2D1GeometrySink> sink;
    Check(geometry->Open(&sink));
    sink->BeginFigure({points[0].x, points[0].y}, D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); ++i)
        sink->AddLine({points[i].x, points[i].y});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    Check(sink->Close());
    brush->SetColor(Color(theme::Surface));
    target->FillGeometry(geometry.Get(), brush.Get());
}

std::array<POINT, 69> Island::AttachedPixels(float scale) const {
    auto outline = dock::Attached((envelopeWidth - width) / 2, 0, width, height);
    std::array<POINT, 69> points{};
    for (size_t i = 0; i < points.size(); ++i)
        points[i] = {static_cast<LONG>(std::lround(outline[i].x * scale)),
                     static_cast<LONG>(std::lround(outline[i].y * scale))};
    return points;
}

void Island::IslandSurface() {
    D2D1_RECT_F rect{(envelopeWidth - width) / 2, PanelY(), (envelopeWidth + width) / 2, PanelY() + height};
    brush->SetColor(Color(theme::Surface, backdropAvailable && !opaqueMaterial && !highContrast ? 0.94f : 1.0f));
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, theme::Radius, theme::Radius), brush.Get());
    brush->SetColor(Color(theme::Border));
    target->DrawRoundedRectangle(
        D2D1::RoundedRect({rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f}, theme::Radius,
                          theme::Radius),
        brush.Get(), 1);
}

LRESULT CALLBACK Island::BackdropProcedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(handle, message, wparam, lparam);
}

void Island::PositionBackdrop() {
    if (!backdropWindow)
        return;
    if (!expanded || dockReveal.value < 0.01f || opaqueMaterial || highContrast || !backdropAvailable) {
        ShowWindow(backdropWindow, SW_HIDE);
        return;
    }
    RECT host{};
    GetWindowRect(window, &host);
    float scale = dpi / 96;
    int x = host.left + static_cast<int>(std::lround((envelopeWidth - width) * scale / 2)),
        y = host.top + static_cast<int>(std::lround(PanelY() * scale)),
        w = static_cast<int>(std::lround(width * scale)), h = static_cast<int>(std::lround(height * scale));
    RECT next{x, y, x + w, y + h};
    if (EqualRect(&next, &backdropPlacement) && IsWindowVisible(backdropWindow))
        return;
    backdropPlacement = next;
    int radius = static_cast<int>(theme::Radius * scale);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
    if (!SetWindowRgn(backdropWindow, region, FALSE))
        DeleteObject(region);
    SetWindowPos(backdropWindow, window, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Island::ConfigureMaterial() {
    DWORD transparency = 1, size = sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                 L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);
    systemTransparency = transparency;
    // The DWM backdrop must own a tight panel HWND. Applying it to our large
    // transparent input envelope can blur unrelated browser content.
    int none = DWMSBT_NONE;
    DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
    MARGINS clear{};
    DwmExtendFrameIntoClientArea(window, &clear);
    if (!renderMode && !opaqueMaterial && !highContrast && transparency && !backdropWindow) {
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = BackdropProcedure;
        type.lpszClassName = L"Dako.Panel.Material";
        RegisterClassW(&type);
        backdropWindow =
            CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, type.lpszClassName,
                            L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, nullptr);
    }
    int backdrop = opaqueMaterial || highContrast || !transparency ? DWMSBT_NONE : DWMSBT_TRANSIENTWINDOW;
    backdropResult = backdropWindow
                         ? DwmSetWindowAttribute(backdropWindow, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop))
                         : HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    backdropAvailable = backdropWindow && SUCCEEDED(backdropResult) && backdrop == DWMSBT_TRANSIENTWINDOW;
    if (backdropWindow) {
        MARGINS margins = backdropAvailable ? MARGINS{-1, -1, -1, -1} : MARGINS{};
        DwmExtendFrameIntoClientArea(backdropWindow, &margins);
        BOOL dark = !lightAppearance;
        DwmSetWindowAttribute(backdropWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }
    PositionBackdrop();
}

void Island::Graphics() {
    if (target)
        return;
    PreviewTrace("graphics-create");
    RECT client{};
    GetClientRect(window, &client);
    if (!renderMode) {
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_NOREDIRECTIONBITMAP);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (!renderMode && SUCCEEDED(composition.Create(window, factory.Get(), static_cast<UINT>(client.right),
                                                    static_cast<UINT>(client.bottom), dpi, &target))) {
        ConfigureMaterial();
    } else {
        composition.Reset();
        if (backdropWindow)
            ShowWindow(backdropWindow, SW_HIDE);
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_NOREDIRECTIONBITMAP);
        backdropAvailable = false;
        int backdrop = DWMSBT_NONE;
        DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        Check(factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi),
            D2D1::HwndRenderTargetProperties(
                window, D2D1::SizeU(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom))),
            &hostTarget));
        Check(hostTarget.As(&target));
    }
    Check(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush));
    PreviewTrace("graphics-ready");
    if (scheduler.active && composition.FrameReady() && frameClock.Start(window, MotionFrame, composition.FrameReady()))
        KillTimer(window, TransitionTimer);
}

void Island::Place() {
    MONITORINFO info{sizeof(info)};
    auto selected = DockEnabled() && panelMonitor ? panelMonitor : MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(selected, &info)) {
        selected = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(selected, &info);
        panelMonitor = selected;
    }
    const float scale = dpi / 96.0f;
    auto monitor = selected;
    if (monitor != refreshMonitor) {
        refreshMonitor = monitor;
        MONITORINFOEXW display{};
        display.cbSize = sizeof(display);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (GetMonitorInfoW(monitor, &display) &&
            EnumDisplaySettingsW(display.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency >= 30)
            motionInterval = std::max(4U, 1000U / std::min(240U, static_cast<UINT>(mode.dmDisplayFrequency)));
    }
    if (!fixtureGeometry) {
        envelopeWidth = std::min(theme::EnvelopeWidth, (info.rcMonitor.right - info.rcMonitor.left) / scale);
        envelopeHeight = std::min(theme::EnvelopeHeight, (info.rcMonitor.bottom - info.rcMonitor.top) / scale);
    }
    RECT barBounds = DockEnabled() && docks ? docks->PillBounds(selected) : RECT{};
    bool anchored = DockEnabled() && barBounds.bottom > barBounds.top;
    if (anchored && !fixtureGeometry)
        envelopeHeight =
            std::min(theme::EnvelopeHeight, std::max(80.0f, (barBounds.top - info.rcMonitor.top) / scale - 8));
    const int w = static_cast<int>(std::lround((renderMode ? width : envelopeWidth) * scale));
    const int h = static_cast<int>(std::lround((renderMode ? height : envelopeHeight) * scale));
    RECT bounds{};
    GetWindowRect(window, &bounds);
    int left = info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - w) / 2;
    if (anchored && dockAnchor.x)
        left = std::clamp(dockAnchor.x - w / 2, info.rcMonitor.left,
                          std::max(info.rcMonitor.left, info.rcMonitor.right - w));
    int top = anchored ? barBounds.top - h + static_cast<int>(4 * scale) : info.rcMonitor.top;
    if (bounds.left != left || bounds.top != top || bounds.right - bounds.left != w || bounds.bottom - bounds.top != h)
        SetWindowPos(window, HWND_TOPMOST, left, top, w, h, SWP_NOACTIVATE);
    const int radius = static_cast<int>(theme::Radius * scale);
    std::array<int, 9> signature{w,
                                 h,
                                 static_cast<int>(std::lround(width * scale)),
                                 static_cast<int>(std::lround(height * scale)),
                                 static_cast<int>(PanelY() * scale),
                                 static_cast<int>(dpi),
                                 dockReveal.value > 0.003f,
                                 YieldResting(),
                                 renderMode};
    if (signature != regionSignature) {
        regionSignature = signature;
        auto notch = NotchPixels(scale);
        HRGN shape = renderMode       ? CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2)
                     : YieldResting() ? CreateRectRgn(0, 0, 0, 0)
                                      : CreatePolygonRgn(notch.data(), static_cast<int>(notch.size()), WINDING);
        if (!renderMode && dockReveal.value > 0.003f) {
            int x = static_cast<int>((envelopeWidth - width) * 0.5f * scale), y = static_cast<int>(PanelY() * scale);
            HRGN body = CreateRoundRectRgn(x, y, x + static_cast<int>(std::lround(width * scale)),
                                           y + static_cast<int>(std::lround(height * scale)), radius * 2, radius * 2);
            CombineRgn(shape, shape, body, RGN_OR);
            DeleteObject(body);
        }
        if (!SetWindowRgn(window, shape, TRUE))
            DeleteObject(shape);
    }
    hotZone.Place(window, !expanded && !fullscreenContext, static_cast<UINT>(dpi));
    PositionBackdrop();
    PositionInputs();
}

void Island::PositionInputs() {
    if (layoutOnly)
        return;
    bool visible = expanded && !capturingPane;
    float scale = dpi / 96;
    auto position = [&](HWND input, int id) {
        if (!input)
            return;
        auto found = std::find_if(hits.begin(), hits.end(), [&](const Hit &h) { return h.id == id; });
        bool show = visible && found != hits.end();
        if (show) {
            EnableWindow(input, found->enabled);
            auto r = found->rect;
            auto c = found->clip;
            show = r.top >= c.top - 0.01f && r.bottom <= c.bottom + 0.01f;
            if (show) {
                r.top = std::max(r.top, c.top);
                r.bottom = std::min(r.bottom, c.bottom);
            }
            if (show)
                SetWindowPos(input, nullptr, static_cast<int>(std::lround((PanelX() + r.left * textScale) * scale)),
                             static_cast<int>(std::lround((PanelY() + r.top * textScale) * scale)),
                             static_cast<int>(std::lround((r.right - r.left) * textScale * scale)),
                             static_cast<int>(std::lround((r.bottom - r.top) * textScale * scale)),
                             SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ShowWindow(input, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    };
    if (appSearch) {
        LRESULT limit = flyout == Flyout::Palette ? 4096 : 120;
        if (SendMessageW(appSearch, EM_GETLIMITTEXT, 0, 0) != limit) {
            SendMessageW(appSearch, EM_SETLIMITTEXT, static_cast<WPARAM>(limit), 0);
            LabelInput(appSearch, flyout == Flyout::Palette ? modules::String(IDS_PALETTE_NAME).c_str() : L"Search installed apps");
        }
    }
    position(taskEdit, 400);
    position(appSearch, 401);
    position(wifiPassword, 402);
    position(clipboardEdit, 403);
}

void Island::ArmMotion() {
    if (renderMode || reducedMotion) {
        SnapMotion();
        return;
    }
    bool started = scheduler.Start();
    if (started)
        lastMotion = steady_clock::now();
    if (composition.FrameReady() && frameClock.Start(window, MotionFrame, composition.FrameReady()))
        KillTimer(window, TransitionTimer);
    else if (started)
        SetTimer(window, TransitionTimer, motionInterval, nullptr);
}

void Island::SetExpanded(bool value, bool deliberate) {
    if (value && !expanded)
        UpdateContext();
    if (!renderMode && !fixtureGeometry) {
        if (value && !DockEnabled() && !(flyout == Flyout::Module && moduleView == L"shelf")) {
            lastProximity = steady_clock::now();
            SetCoalescableTimer(window, ProximityTimer, 100, nullptr, 50);
        } else
            KillTimer(window, ProximityTimer);
        if (value && tab == Tab::Today && flyout == Flyout::None)
            RefreshToday();
    }
    expanded = value;
    if (value && flyout == Flyout::Palette && paletteItems.empty()) BuildPalette();
    if (value && flyout == Flyout::Module) {
        moduleRegistry.Poll();
        if (auto panel = moduleRegistry.View(moduleView, moduleQuery)) modulePanel = std::move(*panel);
    }
    hovered = -1;
    if (!value) {
        focusedKey.clear();
        pressedKey.clear();
        focused = -1;
    }
    if (value) {
        if (tab == Tab::Controls)
            RefreshControls();
        system.RefreshVolume();
        if (!renderMode && !fixtureGeometry && tab == Tab::Controls && flyout == Flyout::None) {
            if (!connectivity)
                connectivity = std::make_unique<Connectivity>(window);
            connectivity->RefreshWifi(false);
            connectivity->RefreshBluetooth(false);
        }
        if (!renderMode && !fixtureGeometry && !media && tab == Tab::Controls && flyout == Flyout::None)
            media = std::make_unique<MediaService>(window);
    }
    if (media)
        media->SetVisible(value && tab == Tab::Controls && flyout == Flyout::None);
    if (!value) {
        clipboardReadTask.Cancel();
        paletteItems.clear();
        commandCatalog.Clear();
        paletteQuery.clear();
        if (appSearch && flyout == Flyout::Palette) SetWindowTextW(appSearch, L"");
        moduleRegistry.Query(L"");
        modulePanel = {};
        moduleQuery.clear();
        clipboard.clear();
        if (clipboardEdit)
            SetWindowTextW(clipboardEdit, L"");
        clipboardState = L"Preview cleared";
        keyboardOpen = false;
        launchPreview = false;
        if (deliberate)
            proximity.Dismiss();
        else {
            proximity.outside = 0;
            proximity.dwell = 0;
        }
        if (wifiPassword)
            SetWindowTextW(wifiPassword, L"");
        if (connectivity)
            connectivity->CancelDiscovery();
    }
    dockReveal.goal = value ? 1.0f : 0.0f;
    dockWidth.goal = value ? SurfaceWidth() : theme::DockWidth;
    dockHeight.goal = value ? SurfaceHeight() : theme::DockHeight;
    transitioning = !reducedMotion && !renderMode;
    if (transitioning)
        ArmMotion();
    else {
        dockReveal.Snap(dockReveal.goal);
        dockWidth.Snap(dockWidth.goal);
        dockHeight.Snap(dockHeight.goal);
        width = dockWidth.value;
        height = dockHeight.value;
        Place();
        if (!value && !renderMode)
            ReleaseGraphics();
    }
    RebuildLayout();
    UpdateTimers();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::Transition() {
    if (!scheduler.active)
        return;
    auto now = steady_clock::now();
    float dt = duration<float>(now - lastMotion).count();
    lastMotion = now;
    if (!diagnosticsPath.empty() && frameIntervals.size() < 8192)
        frameIntervals.push_back(dt * 1000);
    if (dt > 0.5f) {
        SnapMotion();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    // Analytic springs consume elapsed time in bounded steps without stretching a stall.
    bool moving = false;
    auto advance = [&](dock::Spring &v, float frequency = 23.0f) {
        float remaining = dt;
        while (remaining > 0) {
            float step = std::min(remaining, 0.05f);
            v.Step(step, frequency);
            remaining -= step;
        }
        moving |= v.Moving();
    };
    for (auto *v : {&dockReveal, &dockWidth, &dockHeight})
        advance(*v, 38);
    for (auto *v : {&paneBlend, &wallpaperFocus, &tabLeft, &tabRight, &railPointer, &railStrength, &paletteBlend,
                    &contentBlend, &togglePosition, &wallpaperParallax, &volumeMotion, &composerFocus})
        advance(*v, 34);
    for (auto &v : rings)
        advance(v, 20);
    for (auto &[id, v] : hoverMotion) {
        (void)id;
        advance(v, 30);
    }
    for (auto &[id, v] : tileState) {
        (void)id;
        advance(v, 24);
    }
    for (auto &[key, v] : feedback) {
        (void)key;
        v.Step(dt);
        moving |= v.Moving();
    }
    for (auto &[key, v] : rowPositions) {
        (void)key;
        advance(v, 24);
    }
    for (auto it = feedback.begin(); it != feedback.end();)
        if (!it->second.Moving())
            it = feedback.erase(it);
        else
            ++it;
    width = dockWidth.value;
    height = dockHeight.value;
    transitioning = dockReveal.Moving() || dockWidth.Moving() || dockHeight.Moving();
    ApplyAccent(dock::Mix(previousAccent, targetAccent, paletteBlend.value));
    if (paneBlend.value == 1)
        previousPane.Reset();
    if (!moving) {
        frameClock.Stop();
        frameTickReady = false;
        scheduler.Stop();
        KillTimer(window, TransitionTimer);
        for (auto it = hoverMotion.begin(); it != hoverMotion.end();)
            if (it->second.value == 0)
                it = hoverMotion.erase(it);
            else
                ++it;
    }
    if (dockReveal.value == 0 && !expanded)
        SnapMotion();
    Place();
    RebuildLayout();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::ProximityTick() {
    if (expanded && flyout == Flyout::Module && moduleView == L"shelf" && moduleRegistry.Enabled(L"shelf")) {
        KillTimer(window, ProximityTimer);
        return;
    }
    UpdateContext();
    shell.UpdateBrowserCutout(nullptr); // Restore legacy cutouts; never alter foreign chrome.
    auto now = steady_clock::now();
    float dt = duration<float>(now - lastProximity).count();
    lastProximity = now;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    float x = cursor.x * 96.0f / dpi, y = cursor.y * 96.0f / dpi, center = envelopeWidth / 2;
    bool buttonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    bool approaching =
        std::abs(x - center) < theme::DockWidth / 2 + 18 && y >= 0 && y < theme::DockHeight + 6 && !buttonDown;
    bool withinBody = std::abs(x - center) < width / 2 && y >= PanelY() && y < PanelY() + height;
    bool inside =
        std::abs(x - center) < std::max(width, theme::DockWidth) / 2 + 18 && y >= 0 && y < PanelY() + height + 18;
    if (!expanded && !approaching) {
        KillTimer(window, ProximityTimer);
        proximity.dwell = 0;
        return;
    }
    HWND foreground = GetForegroundWindow();
    bool ownedModal = foreground && GetWindow(foreground, GW_OWNER) == window;
    bool editing = foreground == window && (GetFocus() == taskEdit || GetFocus() == appSearch ||
                                            GetFocus() == wifiPassword || GetFocus() == clipboardEdit);
    bool protectedInput = previewMode || modalOpen || ownedModal || editing || draggingVolume || draggingBrightness ||
                          launchPreview || (keyboardOpen && foreground == window);
    if (expanded && buttonDown && !withinBody && !protectedInput) {
        SetExpanded(false);
        return;
    }
    if (!expanded && dockReveal.value > 0.05f && proximity.cooldown == 0 && withinBody && !buttonDown) {
        SetExpanded(true);
        return;
    }
    int action = proximity.Step(dt, expanded, approaching, inside, protectedInput);
    if (action > 0)
        SetExpanded(true);
    else if (action < 0)
        SetExpanded(false, false);
}

void Island::UpdateTimers() {
    if (renderMode || fixtureGeometry)
        return;
    auto timer = [&](UINT_PTR id, bool enabled, UINT period, bool &active) {
        if (enabled == active)
            return;
        active = enabled;
        if (enabled)
            SetTimer(window, id, period, nullptr);
        else
            KillTimer(window, id);
    };
    bool stats = expanded && tab == Tab::Stats && flyout == Flyout::None;
    if (stats && !statsTicking)
        SampleStats();
    timer(StatsTimer, stats, 1000, statsTicking);
    timer(MediaTimer, currentMedia.playing && expanded && tab == Tab::Controls && flyout == Flyout::None, 1000,
          mediaTicking);
    timer(FocusTimer, focusRunning || (expanded && !miniMode && flyout == Flyout::None), 1000, focusTicking);
    timer(ConnectivityTimer, expanded && (flyout == Flyout::Wifi || flyout == Flyout::Bluetooth), 2500,
          connectionsTicking);
}

void Island::SetTab(Tab value) {
    bool fromPalette = flyout == Flyout::Palette;
    if (expanded && !miniMode && flyout == Flyout::None && tab == value)
        return;
    modulePanel = {};
    moduleView.clear();
    moduleQuery.clear();
    miniMode = false;
    previousPane.Reset();
    paneBlend.Snap(1);
    if (connectivity)
        connectivity->CancelDiscovery();
    flyout = Flyout::None;
    bodyDirection = static_cast<int>(value) >= static_cast<int>(tab) ? 1.0f : -1.0f;
    tab = value;
    note.clear();
    scroll = 0;
    focusedKey.clear();
    focused = hovered = -1;
    if (tab == Tab::Apps)
        FilterRunning();
    if (tab == Tab::Clipboard && !fixtureGeometry)
        ReadClipboard();
    if (!renderMode && !fixtureGeometry && (tab == Tab::Apps || tab == Tab::Wallpaper)) {
        if (!catalog)
            catalog = std::make_unique<DesktopCatalog>(window);
        if (tab == Tab::Apps && (!appsRequested || (!appsLoading && apps.empty()))) {
            appsRequested = true;
            appsLoading = true;
            catalog->DiscoverApps();
        }
        if (tab == Tab::Wallpaper) {
            catalog->DiscoverDisplays();
            if (!wallpapersRequested)
                WallpaperSource(wallpaperSource);
        }
    }
    SetExpanded(true);
    if (fromPalette && appSearch) {
        SetWindowTextW(appSearch, appQuery.c_str());
        SendMessageW(appSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search installed apps…"));
    }
    PositionInputs();
}

D2D1_COLOR_F Island::Color(unsigned rgb, float alpha) {
    return D2D1::ColorF(rgb, alpha);
}

void Island::Fill(D2D1_RECT_F rect, unsigned color, float radius) {
    if (layoutOnly)
        return;
    if (rect.bottom - rect.top >= 48 && rect.right - rect.left >= 60)
        radius = std::min(radius, 10.0f);
    brush->SetColor(Color(color));
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
    if (radius >= 10 && rect.bottom - rect.top >= 32 && rect.right - rect.left >= 60) {
        brush->SetColor(Color(theme::Border, highContrast ? 1.0f : 0.65f));
        D2D1_RECT_F edge{rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f};
        target->DrawRoundedRectangle(D2D1::RoundedRect(edge, radius, radius), brush.Get(), 1);
    }
}

void Island::Text(const std::wstring &text, D2D1_RECT_F rect, int format, unsigned color, DWRITE_TEXT_ALIGNMENT align) {
    if (layoutOnly || rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    brush->SetColor(Color(color));
    formats[format]->SetTextAlignment(align);
    target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), formats[format].Get(), rect, brush.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Island::Line(float x1, float y1, float x2, float y2, unsigned color, float thickness) {
    if (layoutOnly)
        return;
    brush->SetColor(Color(color));
    target->DrawLine({x1, y1}, {x2, y2}, brush.Get(), thickness);
}

void Island::AddHit(int id, D2D1_RECT_F rect, std::wstring name, bool enabled) {
    auto original = rect;
    if (buildingBody) {
        rect.top -= scroll;
        rect.bottom -= scroll;
    }
    auto clip = !expanded      ? D2D1_RECT_F{0, 0, theme::DockWidth, theme::DockHeight}
                : buildingBody ? Body()
                               : R(Layout().panel);
    auto visible = layout::Intersection({rect.left, rect.top, rect.right, rect.bottom},
                                        {clip.left, clip.top, clip.right, clip.bottom});
    (void)visible;
    auto key = StableKey(id);
    auto [it, inserted] = childIds.try_emplace(key, nextChild);
    if (inserted)
        ++nextChild;
    Hit hit{id, rect, std::move(name), enabled, key, it->second, 0, 10, clip, nullptr};
    if (id == 400 || id == 401 || id == 402 || id == 403) {
        hit.input = id == 400 ? taskEdit : id == 401 ? appSearch : id == 402 ? wifiPassword : clipboardEdit;
    }
    hits.push_back(std::move(hit));
    (void)original;
}

void Island::FocusRing(int id, D2D1_RECT_F rect, float radius) {
    if (layoutOnly)
        return;
    if (focusedKey == StableKey(id)) {
        brush->SetColor(Color(theme::Focus));
        target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get(), highContrast ? 3.0f : 1.5f);
        if (highContrast) {
            brush->SetColor(Color(theme::Surface));
            target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get(), 1);
        }
    }
}

bool Island::Hover(int id) const {
    return hovered >= 0 && hovered < static_cast<int>(hits.size()) && hits[hovered].id == id;
}

float Island::HoverAmount(int id) const {
    if (highContrast)
        return 0;
    auto found = hoverMotion.find(id);
    return found == hoverMotion.end() ? 0 : found->second.value;
}

void Island::SetHover(int next) {
    if (next == hovered)
        return;
    if (hovered >= 0 && hovered < static_cast<int>(hits.size()))
        hoverMotion[hits[hovered].id].goal = 0;
    hovered = next;
    hoveredKey = next >= 0 && next < static_cast<int>(hits.size()) ? hits[next].key : L"";
    if (next >= 0 && next < static_cast<int>(hits.size()))
        hoverMotion[hits[next].id].goal = 1;
    if (reducedMotion || renderMode) {
        for (auto &[id, v] : hoverMotion) {
            (void)id;
            v.Snap(v.goal);
        }
    } else
        ArmMotion();
    InvalidateRect(window, nullptr, FALSE);
}

D2D1_RECT_F Island::Tactile(int id, D2D1_RECT_F rect) {
    float amount = pressedKey == StableKey(id) ? 1.0f : 0.0f;
    auto found = feedback.find(StableKey(id));
    if (found != feedback.end())
        amount += found->second.value;
    float inset = std::clamp(amount, -0.3f, 1.0f) * 1.5f;
    return {rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset};
}

void Island::Button(int id, D2D1_RECT_F rect, const std::wstring &title, const std::wstring &subtitle,
                    const wchar_t *icon, bool active, bool enabled) {
    AddHit(id, rect, title + L". " + subtitle, enabled);
    auto visual = Tactile(id, rect);
    auto [state, inserted] = tileState.try_emplace(id, active ? 1.0f : 0.0f);
    (void)inserted;
    state->second.goal = active ? 1.0f : 0.0f;
    if (reducedMotion || renderMode || highContrast)
        state->second.Snap(state->second.goal);
    else if (!layoutOnly && state->second.Moving())
        ArmMotion();
    Fill(visual,
         dock::Mix(dock::Mix(theme::Card, theme::AccentSoft, state->second.value), theme::CardHover, HoverAmount(id)),
         layout::CardRadius);
    Text(icon, {rect.left + 12, rect.top + 12, rect.left + 40, rect.bottom - 12}, type::Icon,
         active ? (highContrast ? theme::SelectedText : theme::Accent) : theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(title, {rect.left + 50, rect.top + 7, rect.right - 26, rect.top + 30}, type::Control,
         highContrast && active ? theme::SelectedText
         : enabled              ? theme::Text
                                : theme::Muted);
    Text(subtitle, {rect.left + 50, rect.top + 30, rect.right - 26, rect.bottom - 6}, type::Secondary,
         highContrast && active ? theme::SelectedText : theme::Muted);
    Text(L"\xE76C", {rect.right - 24, rect.top + 15, rect.right - 8, rect.bottom - 15}, type::Icon,
         highContrast && active ? theme::SelectedText : theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    FocusRing(id, rect);
}

void Island::Paragraph(const std::wstring &text, D2D1_RECT_F rect, unsigned color, int format) {
    if (layoutOnly || rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    ComPtr<IDWriteTextLayout> textLayout;
    if (SUCCEEDED(textFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), formats[format].Get(),
                                                rect.right - rect.left, rect.bottom - rect.top, &textLayout))) {
        textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        textLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        brush->SetColor(Color(color));
        target->DrawTextLayout({rect.left, rect.top}, textLayout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

float Island::RowY(const std::wstring &key, float goal) {
    // Reordering rows independently can cross headings and neighboring controls.
    // Stable geometry makes rapid filtering, pagination and device updates safe.
    auto [it, inserted] = rowPositions.try_emplace(key, goal);
    (void)inserted;
    it->second.Snap(goal);
    return it->second.value;
}

std::wstring Island::Clock(double seconds) {
    auto s = static_cast<int>(std::max(0.0, seconds));
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d:%02d", s / 60, s % 60);
    return buffer;
}

D2D1_RECT_F Island::TabRect(int index) const {
    auto l = Layout();
    auto found = std::find(PrimaryTabs.begin(), PrimaryTabs.end(), static_cast<Tab>(index));
    size_t slot = found == PrimaryTabs.end() ? 0 : static_cast<size_t>(found - PrimaryTabs.begin());
    constexpr float weights[]{84, 64, 60, 60, 98};
    float prior = 0, total = 0;
    for (size_t i = 0; i < 5; ++i) {
        total += weights[i];
        if (i < slot)
            prior += weights[i];
    }
    float left = l.header.left + l.header.Width() * prior / total;
    return {left, 28, left + l.header.Width() * weights[slot] / total - 4, 60};
}
void Island::CompactHeader(bool wireless) {
    auto l = Layout();
    float mid = (l.header.left + l.header.right) / 2;
    TextButton(wireless ? FlyoutBack : 1900, {l.header.left, 0, mid - 3, 32}, wireless ? L"Back" : TabTitle());
    TextButton(1901, {mid + 3, 0, l.header.right, 32}, L"Menu");
}
std::wstring Island::TabTitle() const {
    constexpr const wchar_t *names[]{L"Controls", L"Today", L"Stats", L"Clipboard", L"Apps", L"Wallpaper", L"Power"};
    return names[static_cast<int>(tab)];
}
void Island::Tabs() {
    if (Layout().emergency) {
        CompactHeader();
        return;
    }
    auto l = Layout();
    Text(L"dako", {l.header.left, 0, l.header.left + 66, 26}, type::Section);
    SYSTEM_POWER_STATUS power{};
    bool valid = GetSystemPowerStatus(&power) != FALSE;
    if (renderMode || fixtureGeometry) {
        power.BatteryLifePercent = 76;
        power.BatteryFlag = 0;
        power.ACLineStatus = 1;
        valid = true;
    }
    std::wstring battery = !valid                            ? L"Power ?"
                           : power.BatteryFlag == 128        ? L"AC"
                           : power.BatteryLifePercent <= 100 ? Number(power.BatteryLifePercent) + L"%"
                                                             : L"Power ?";
    D2D1_RECT_F chip{l.header.right - 70, 2, l.header.right, 24};
    Fill(chip, theme::Inset, 10);
    Text(battery, chip, type::Caption, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    AddHit(2002, chip, L"Battery: " + battery + (power.ACLineStatus == 1 ? L", plugged in" : L", on battery"), false);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (renderMode || fixtureGeometry)
        now = {2026, 10, 5, 23, 14, 42, 0, 0};
    wchar_t time[64]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, 64);
    if (l.header.Width() > 300)
        Text(time, {l.header.right - 190, 0, l.header.right - 82, 26}, type::Mono, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    auto selected = TabRect(static_cast<int>(tab));
    tabLeft.goal = selected.left;
    tabRight.goal = selected.right;
    if (reducedMotion || renderMode || l.narrow) {
        tabLeft.Snap(selected.left);
        tabRight.Snap(selected.right);
    }
    for (Tab primary : PrimaryTabs) {
        int index = static_cast<int>(primary);
        auto rect = TabRect(index);
        constexpr const wchar_t *labels[]{L"Controls", L"Today",     L"Stats", L"Clipboard",
                                          L"Apps",     L"Wallpaper", L"Power"};
        AddHit(100 + index, rect, labels[index]);
        Text(labels[index], rect, l.narrow ? type::Caption : type::Control, primary == tab ? theme::Text : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(100 + index, rect, 6);
    }
    Line(l.header.left, 62, l.header.right, 62, theme::Border);
    if (std::find(PrimaryTabs.begin(), PrimaryTabs.end(), tab) != PrimaryTabs.end())
        Line(tabLeft.value + 8, 62, tabRight.value - 8, 62, theme::Accent, 2);
}

void Island::DrawRing(float x, float y, float radius, float value, unsigned color, float thickness) {
    if (layoutOnly)
        return;
    brush->SetColor(Color(theme::CardHover));
    target->DrawEllipse(D2D1::Ellipse({x, y}, radius, radius), brush.Get(), thickness);
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0)
        return;
    brush->SetColor(Color(color));
    if (value >= 0.999f) {
        target->DrawEllipse(D2D1::Ellipse({x, y}, radius, radius), brush.Get(), thickness);
        return;
    }
    // A small bounded polyline avoids allocating an immutable path every tick.
    constexpr int segments = 120;
    float end = value * 6.28318530718f, start = -1.57079632679f;
    D2D1_POINT_2F previous{x, y - radius};
    int count = std::max(1, static_cast<int>(std::ceil(value * segments)));
    for (int i = 1; i <= count; ++i) {
        float angle = start + end * i / count;
        D2D1_POINT_2F point{x + radius * std::cos(angle), y + radius * std::sin(angle)};
        if (!ringStroke)
            Check(factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                         D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                             nullptr, 0, &ringStroke));
        target->DrawLine(previous, point, brush.Get(), thickness, ringStroke.Get());
        previous = point;
    }
}

void Island::TextButton(int id, D2D1_RECT_F rect, const std::wstring &text, bool primary, bool enabled) {
    AddHit(id, rect, text, enabled);
    Fill(Tactile(id, rect),
         dock::Mix(primary && enabled ? theme::Accent : theme::Inset,
                   primary && enabled ? theme::AccentHover : theme::CardHover, HoverAmount(id)),
         8);
    Text(text, rect, type::Control,
         !enabled  ? theme::Muted
         : primary ? theme::AccentInk
                   : theme::Text,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    FocusRing(id, rect, 8);
}

void Island::ReadClipboard() {
    clipboard.clear();
    clipboardOffset = 0;
    if (renderMode || fixtureGeometry || previewMode) {
        clipboardState = modules::String(IDS_CLIP_INERT);
    } else if (clipboardReadTask.Busy()) {
        clipboardState = modules::String(IDS_CLIP_READING);
    } else {
        clipboardReadToken = privacy::Policy().Reserve();
        privacy::Policy().Watch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken),
                                 L"core-clipboard", L"clipboard-read");
        if (!clipboardReadTask.Start(::clipboard::TransferOp::Read, {}, window, L"core-clipboard")) {
            privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken));
            clipboardReadToken = 0;
            clipboardState = modules::String(IDS_CLIP_UNSUPPORTED);
        } else clipboardState = modules::String(IDS_CLIP_READING);
    }
    if (clipboardEdit) SetWindowTextW(clipboardEdit, L"");
}
D2D1_RECT_F Island::ClipboardRect() const {
    auto b = Body();
    return {b.left + 12, b.top + (Layout().body.Height() >= 100 ? 44.0f : 0.0f), b.right - 12,
            b.bottom - (Layout().body.Height() >= 100 ? 12.0f : 0.0f)};
}

void Island::EnsureClipboardEdit() {
    if (clipboardEdit || renderMode || capturingPane || layoutOnly)
        return;
    clipboardEdit = CreateWindowExW(0, L"EDIT", clipboard.c_str(),
                                    WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0,
                                    0, 1, 1, window, reinterpret_cast<HMENU>(403), nullptr, nullptr);
    LabelInput(clipboardEdit, L"Current clipboard text, read only");
    EnsureEditResources();
    SendMessageW(clipboardEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    PositionInputs();
}

void Island::ClipboardView() {
    auto b = Body();
    Fill(b, theme::Inset, 14);
    if (Layout().body.Height() >= 100)
        Text(clipboardState, {b.left + 12, b.top + 4, b.right - 12, b.top + 36}, type::Control);
    auto rect = ClipboardRect();
    AddHit(403, rect, L"Current clipboard text, read only");
    if (renderMode)
        Paragraph(clipboard.empty() ? L"Nothing to preview." : clipboard, rect, theme::Text);
    EnsureClipboardEdit();
}

void Island::RetargetLayout() {
    if (!expanded)
        return;
    dockWidth.goal = SurfaceWidth();
    dockHeight.goal = SurfaceHeight();
    if (reducedMotion || renderMode) {
        dockWidth.Snap(dockWidth.goal);
        dockHeight.Snap(dockHeight.goal);
        width = dockWidth.value;
        height = dockHeight.value;
    } else if (dockWidth.Moving() || dockHeight.Moving()) {
        transitioning = true;
        ArmMotion();
    }
}

void Island::FilterApps(bool reset) {
    FilterRunning();
    filteredApps.clear();
    if (reset)
        appPage = 0;
    for (size_t i = 0; i < apps.size(); ++i)
        if (appQuery.empty() ||
            FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE, apps[i].name.c_str(), -1,
                            appQuery.c_str(), -1, nullptr, nullptr, nullptr, 0) >= 0)
            filteredApps.push_back(i);
    std::stable_sort(filteredApps.begin(), filteredApps.end(), [this](size_t a, size_t b) {
        int ga = AppGroup(apps[a]), gb = AppGroup(apps[b]);
        if (ga != gb)
            return ga < gb;
        if (ga == 1) {
            auto ai = std::find(appPreferences.recent.begin(), appPreferences.recent.end(), AppKey(apps[a])),
                 bi = std::find(appPreferences.recent.begin(), appPreferences.recent.end(), AppKey(apps[b]));
            return ai < bi;
        }
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, apps[a].name.c_str(), -1,
                               apps[b].name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    if (std::none_of(filteredApps.begin(), filteredApps.end(),
                     [this](size_t i) { return AppKey(apps[i]) == selectedAppKey; }))
        selectedAppKey = filteredApps.empty() ? L"" : AppKey(apps[filteredApps.front()]);
    railApps.clear();
    for (size_t i : filteredApps)
        if (Favorite(apps[i]) && railApps.size() < 7)
            railApps.push_back(i);
    RepageApps();
    if (tab == Tab::Apps)
        RetargetLayout();
}

void Island::SmallButton(int id, D2D1_RECT_F rect, const wchar_t *icon, const std::wstring &label, bool enabled) {
    AddHit(id, rect, label, enabled);
    Fill(Tactile(id, rect), id == AddTask && enabled ? theme::Accent : Hover(id) ? theme::CardHover : theme::Card, 10);
    Text(icon, rect, type::Icon,
         id == AddTask && enabled ? theme::AccentInk
         : enabled                ? theme::SelectedText
                                  : theme::Muted,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    FocusRing(id, rect, 10);
}

void Island::Power() {
    const int actions[]{Lock, SleepNow, RestartNow, ShutdownNow};
    const wchar_t *labels[]{L"Lock", L"Sleep", L"Restart", L"Shut down"};
    const wchar_t *icons[]{L"\xE72E", L"\xE708", L"\xE777", L"\xE7E8"};
    auto b = Layout().body;
    int cols = Layout().narrow ? 2 : 4;
    for (int i = 0; i < 4; ++i) {
        auto r = layout::Column(b, i % cols, cols);
        r.top = b.top + (i / cols) * 100.0f;
        r.bottom = r.top + 88;
        auto rect = R(r);
        AddHit(actions[i], rect, labels[i]);
        Fill(Tactile(actions[i], rect), dock::Mix(theme::Card, theme::CardHover, HoverAmount(actions[i])), 14);
        Text(icons[i], {r.left, r.top + 8, r.right, r.top + 44}, type::Icon, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(labels[i], {r.left, r.top + 48, r.right, r.bottom - 8}, type::Control, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(actions[i], rect);
    }
}

void Island::OpenFlyout(Flyout next) {
    previousPane.Reset();
    paneBlend.Snap(1);
    flyout = next;
    connectionPage = 0;
    selectedWifi = selectedBluetooth = -1;
    focused = hovered = -1;
    if (wifiPassword)
        SetWindowTextW(wifiPassword, L"");
    if (!renderMode && !fixtureGeometry) {
        if (!connectivity)
            connectivity = std::make_unique<Connectivity>(window);
        if (next == Flyout::Wifi)
            connectivity->RefreshWifi(true);
        else if (next == Flyout::Bluetooth)
            connectivity->RefreshBluetooth();
    }
    SetExpanded(true);
}

void Island::EnsurePassword() {
    if (wifiPassword || renderMode || capturingPane || layoutOnly)
        return;
    wifiPassword = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 0, 0, 1, 1,
                                   window, reinterpret_cast<HMENU>(402), nullptr, nullptr);
    LabelInput(wifiPassword, L"Network security key");
    EnsureEditResources();
    SendMessageW(wifiPassword, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    SendMessageW(wifiPassword, EM_SETLIMITTEXT, 64, 0);
    SendMessageW(wifiPassword, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Network security key"));
    PositionInputs();
}

size_t Island::ConnectionSlots() const {
    bool selected = flyout == Flyout::Wifi ? selectedWifi >= 0 : selectedBluetooth >= 0;
    float available = std::max(Layout().body.Height(), Layout().contentHeight);
    return static_cast<size_t>(std::clamp(std::floor((available - 76 - (selected ? 156.0f : 0.0f)) / 56), 1.0f, 5.0f));
}

void Island::ConnectionView() {
    auto l = Layout();
    float w = l.panel.Width();
    bool wifi = flyout == Flyout::Wifi, busy = wifi ? connections.wifiBusy : connections.bluetoothBusy,
         enabled = wifi ? connections.wifiOn : connections.bluetoothOn,
         available = wifi ? connections.wifiAvailable : connections.bluetoothAvailable;
    if (l.emergency)
        CompactHeader(true);
    else {
        SmallButton(FlyoutBack, {20, 8, 56, 44}, L"\xE72B", L"Back to controls");
        Text(wifi ? L"Wi-Fi" : L"Bluetooth", {68, 8, w - 132, 44}, type::Control);
        SmallButton(FlyoutRefresh, {w - 116, 8, w - 80, 44}, L"\xE72C", wifi ? L"Refresh networks" : L"Find devices",
                    !busy);
        D2D1_RECT_F toggle{w - 68, 14, w - 20, 38};
        AddHit(FlyoutToggle, toggle, enabled ? L"Turn radio off" : L"Turn radio on", available && !busy);
        Fill(toggle, enabled ? theme::Accent : theme::CardHover, 12);
        togglePosition.goal = enabled ? 1.0f : 0.0f;
        if (reducedMotion || renderMode)
            togglePosition.Snap(togglePosition.goal);
        if (!layoutOnly) {
            brush->SetColor(Color(enabled ? theme::AccentInk : theme::Muted));
            target->FillEllipse(D2D1::Ellipse({w - 56 + 24 * togglePosition.value, 26}, 8, 8), brush.Get());
        }
        FocusRing(FlyoutToggle, toggle, 12);
    }
    auto b = Body();
    D2D1::Matrix3x2F saved;
    if (!layoutOnly) {
        target->GetTransform(&saved);
        target->PushAxisAlignedClip(b, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        auto layer = D2D1::LayerParameters();
        layer.opacity = std::clamp(paneBlend.value, 0.0f, 1.0f);
        target->PushLayer(layer, nullptr);
        target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection * 12 * (1 - paneBlend.value), -scroll) *
                             saved);
    }
    buildingBody = true;
    const auto &message = wifi ? connections.wifiMessage : connections.bluetoothMessage;
    AddHit(2040, {b.left, b.top, b.right, b.top + 44},
           message.empty() ? (busy   ? L"Searching"
                              : wifi ? L"Available networks"
                                     : L"Your devices")
                           : message,
           false);
    Paragraph(message.empty() ? (busy   ? L"Searching…"
                                 : wifi ? L"Available networks"
                                        : L"Your devices")
                              : message,
              {b.left, b.top, b.right, b.top + 44});
    size_t count = wifi ? connections.networks.size() : connections.devices.size(), slots = ConnectionSlots();
    for (size_t slot = 0; slot < slots && connectionPage * slots + slot < count; ++slot) {
        size_t index = connectionPage * slots + slot;
        int id = (wifi ? 1000 : 1200) + static_cast<int>(index);
        float y = RowY(StableKey(id), b.top + 52 + static_cast<float>(slot) * 56);
        bool selected = wifi ? static_cast<int>(index) == selectedWifi : static_cast<int>(index) == selectedBluetooth;
        bool connected = wifi ? connections.networks[index].connected : connections.devices[index].connected;
        std::wstring name = wifi ? connections.networks[index].name : connections.devices[index].name,
                     status = connected ? L"Connected"
                              : wifi    ? (connections.networks[index].secure ? L"Secured" : L"Open network")
                              : connections.devices[index].paired ? L"Paired"
                                                                  : L"Ready to pair";
        D2D1_RECT_F row{b.left, y, b.right, y + 48};
        AddHit(id, row, name + L". " + status);
        Fill(Tactile(id, row), selected ? theme::AccentSoft : theme::Card, 12);
        Text(wifi ? L"\xE701" : L"\xE702", {b.left + 12, y + 8, b.left + 40, y + 40}, type::Icon,
             highContrast && selected ? theme::SelectedText
             : connected              ? theme::Accent
                                      : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(name, {b.left + 52, y + 2, b.right - 12, y + 26}, type::Body,
             highContrast && selected ? theme::SelectedText : theme::Text);
        Text(status, {b.left + 52, y + 26, b.right - 12, y + 46}, type::Secondary,
             highContrast && selected ? theme::SelectedText : theme::Muted);
        FocusRing(id, row);
    }
    float detail = b.top + 52 + static_cast<float>(slots) * 56 + 12;
    if (!count)
        Paragraph(busy         ? L"Searching…"
                  : !available ? L"No adapter available. Use More settings."
                  : !enabled   ? L"Radio is off."
                               : L"No results. Try Refresh.",
                  {b.left + 12, b.top + 76, b.right - 12, b.top + 160});
    if (wifi && selectedWifi >= 0 && selectedWifi < static_cast<int>(connections.networks.size())) {
        const auto &network = connections.networks[selectedWifi];
        Fill({b.left, detail, b.right, detail + 120}, theme::Inset, 12);
        bool needs = (renderMode || fixtureGeometry) && network.secure && network.profile.empty() && !network.connected;
        if (needs) {
            D2D1_RECT_F input{b.left + 12, detail + 12, b.right - 12,
                              detail + 12 + std::min(32.0f, Layout().body.Height())};
            Fill(input, theme::CardHover, 8);
            AddHit(402, input, L"Network security key, 8 to 63 characters or 64 hexadecimal digits");
            EnsurePassword();
            if (renderMode)
                Text(L"Network security key", input, type::Secondary, theme::Muted);
        } else
            Paragraph(network.connected ? L"Connected to " + network.name : L"Windows saved profile or open network.",
                      {b.left + 12, detail + 8, b.right - 12, detail + 52});
        bool can = network.connected || !needs || (wifiPassword && GetWindowTextLengthW(wifiPassword) > 0);
        TextButton(network.connected ? FlyoutDisconnect : FlyoutConnect,
                   {b.right - 164, detail + 64, b.right - 12, detail + 104}, L"Wi-Fi settings ↗", !network.connected,
                   can && !connections.wifiConnecting && !busy);
    }
    if (!wifi && selectedBluetooth >= 0 && selectedBluetooth < static_cast<int>(connections.devices.size())) {
        const auto &device = connections.devices[selectedBluetooth];
        Fill({b.left, detail, b.right, detail + 120}, theme::Inset, 12);
        Text(device.name, {b.left + 12, detail + 6, b.right - 12, detail + 42}, type::Body);
        TextButton(device.connected ? FlyoutDisconnect
                   : device.paired  ? FlyoutConnect
                                    : FlyoutPair,
                   {b.right - 160, detail + 60, b.right - 12, detail + 100}, L"Bluetooth settings ↗", !device.connected,
                   !busy);
    }
    buildingBody = false;
    if (!layoutOnly) {
        target->SetTransform(saved);
        target->PopLayer();
        target->PopAxisAlignedClip();
    }
    if (!l.emergency) {
        auto f = Footer();
        TextButton(FlyoutSettings, {f.left, f.top, f.right - 104, f.bottom}, L"More settings ↗");
        SmallButton(FlyoutPrevious, {f.right - 92, f.top, f.right - 52, f.bottom}, L"\xE76B", L"Previous devices",
                    connectionPage > 0);
        SmallButton(FlyoutNext, {f.right - 40, f.top, f.right, f.bottom}, L"\xE76C", L"Next devices",
                    (connectionPage + 1) * slots < count);
    }
    if (!l.emergency && !note.empty())
        Paragraph(note, R(l.status), theme::Text, type::Caption);
    else if (l.contentHeight > l.body.Height())
        Text(L"Scroll for device details", R(l.status), type::Caption, theme::Muted);
}

void Island::RefreshConnectionsView() {
    if (!connectivity)
        return;
    std::wstring networkKey = selectedWifi >= 0 && selectedWifi < static_cast<int>(connections.networks.size())
                                  ? WifiKey(connections.networks[selectedWifi])
                                  : L"";
    std::wstring deviceKey = selectedBluetooth >= 0 && selectedBluetooth < static_cast<int>(connections.devices.size())
                                 ? BluetoothKey(connections.devices[selectedBluetooth])
                                 : L"";
    connections = connectivity->Snapshot();
    selectedWifi = selectedBluetooth = -1;
    for (size_t i = 0; i < connections.networks.size(); ++i)
        if (WifiKey(connections.networks[i]) == networkKey)
            selectedWifi = static_cast<int>(i);
    for (size_t i = 0; i < connections.devices.size(); ++i)
        if (BluetoothKey(connections.devices[i]) == deviceKey)
            selectedBluetooth = static_cast<int>(i);
    if (selectedWifi < 0 && wifiPassword)
        SetWindowTextW(wifiPassword, L"");
    system.wifi = connections.wifiAvailable ? (connections.wifiOn ? L"Not connected" : L"Off") : L"Not available";
    system.wifiConnected = false;
    for (const auto &network : connections.networks)
        if (network.connected) {
            system.wifi = network.name;
            system.wifiConnected = true;
            break;
        }
    system.bluetooth = connections.bluetoothAvailable ? (connections.bluetoothOn ? L"On" : L"Off") : L"Not available";
    for (const auto &device : connections.devices)
        if (device.connected) {
            system.bluetooth = device.name;
            break;
        }
    int selected = flyout == Flyout::Wifi ? selectedWifi : selectedBluetooth;
    size_t count = flyout == Flyout::Wifi ? connections.networks.size() : connections.devices.size(),
           slots = ConnectionSlots();
    if (selected >= 0)
        connectionPage = static_cast<size_t>(selected) / slots;
    else if (connectionPage * slots >= count)
        connectionPage = 0;
    UpdateDocks();
    RebuildLayout();
    PositionInputs();
    if (expanded) {
        ArmMotion();
        InvalidateRect(window, nullptr, FALSE);
    }
}

void Island::PanelContent(bool header) {
    hits.clear();
    buildingBody = false;
    if (!expanded) {
        AddHit(Expand, {0, 0, theme::DockWidth, theme::DockHeight}, L"Reveal dock");
        return;
    }
    if (miniMode) {
        MediaCard(true);
        return;
    }
    if (flyout == Flyout::Palette) {
        Palette();
        return;
    }
    if (flyout == Flyout::Features) {
        Features();
        return;
    }
    if (flyout == Flyout::Module) {
        ModuleView();
        return;
    }
    if (flyout == Flyout::DockStatus) {
        DockPopover();
        return;
    }
    if (flyout == Flyout::Recent) {
        RecentView();
        return;
    }
    if (flyout == Flyout::Audio) {
        OutputDevices();
        return;
    }
    if (flyout != Flyout::None) {
        ConnectionView();
        return;
    }
    if (header)
        Tabs();
    auto b = Body();
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, Layout().contentHeight - (b.bottom - b.top)));
    D2D1::Matrix3x2F saved;
    if (!layoutOnly) {
        target->GetTransform(&saved);
        target->PushAxisAlignedClip(b, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        auto layer = D2D1::LayerParameters();
        layer.opacity = std::clamp(paneBlend.value, 0.0f, 1.0f);
        target->PushLayer(layer, nullptr);
        target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection * 12 * (1 - paneBlend.value), -scroll) *
                             saved);
    }
    buildingBody = true;
    switch (tab) {
    case Tab::Controls:
        Controls();
        break;
    case Tab::Today:
        Today();
        break;
    case Tab::Stats:
        Stats();
        break;
    case Tab::Clipboard:
        ClipboardView();
        break;
    case Tab::Apps:
        Apps();
        break;
    case Tab::Wallpaper:
        Wallpaper();
        break;
    case Tab::Power:
        Power();
        break;
    }
    buildingBody = false;
    if (!layoutOnly) {
        target->SetTransform(saved);
        target->PopLayer();
        target->PopAxisAlignedClip();
    }
    if (!Layout().emergency) {
        auto f = Footer();
        TextButton(1901, {f.left, f.top, f.left + 86, f.bottom}, L"Menu ▾");
    }
    if (!Layout().emergency && tab == Tab::Apps) {
        auto f = Footer();
        Text(Number(static_cast<double>(filteredApps.size())) + L" apps",
             {f.left + 100, f.top, f.right - 104, f.bottom}, type::Secondary, theme::Muted);
        SmallButton(AppsPrevious, {f.right - 92, f.top, f.right - 52, f.bottom}, L"\xE76B", L"Previous apps",
                    appPage > 0);
        SmallButton(AppsNext, {f.right - 40, f.top, f.right, f.bottom}, L"\xE76C", L"Next apps",
                    appPage + 1 < appPages.size());
    }
    if (!Layout().emergency && tab == Tab::Wallpaper) {
        auto f = Footer();
        size_t slots = WallpaperSlots();
        if (f.right - f.left >= 400) {
            SmallButton(WallpaperPrevious, {f.left + 100, f.top, f.left + 136, f.bottom}, L"\xE76B",
                        L"Previous wallpaper page", selectedWallpaper >= slots);
            SmallButton(WallpaperNext, {f.left + 144, f.top, f.left + 180, f.bottom}, L"\xE76C", L"Next wallpaper page",
                        (selectedWallpaper / slots + 1) * slots < wallpapers.size());
        }
        float applyWidth = std::min(128.0f, f.right - f.left - 98);
        TextButton(WallpaperApply, {f.right - applyWidth, f.top, f.right, f.bottom},
                   wallpaperApplying ? L"Changing…" : L"Apply", true, !wallpapers.empty() && !wallpaperApplying);
    }

    if (!Layout().emergency && tab == Tab::Clipboard) {
        auto f = Footer();
        float mid = (f.left + f.right + 100) / 2 - 6;
        TextButton(ClipboardRefresh, {f.left + 100, f.top, mid, f.bottom}, L"Refresh");
        TextButton(ClipboardClear, {mid + 12, f.top, f.right, f.bottom}, L"Clear preview");
    }
    auto status = R(Layout().status);
    if (!Layout().emergency && !note.empty()) {
        AddHit(1800, status, note, false);
        Fill(status, theme::AccentSoft, 8);
        Paragraph(note, {status.left + 8, status.top + 2, status.right - 8, status.bottom - 2},
                  highContrast ? theme::SelectedText : theme::Text, type::Caption);
    } else if (tab == Tab::Clipboard)
        Text(L"Read in memory · no history · cleared when closed", status, type::Secondary, theme::Muted);
    if (!Layout().emergency && note.empty()) {
        Line(status.left, status.top - 4, status.right, status.top - 4, theme::Border);
        if (tab != Tab::Clipboard)
            Text(L"DAKO / " + TabTitle(), status, type::Mono, theme::Muted);
        if (tab != Tab::Clipboard)
            Text(Layout().contentHeight > Layout().body.Height() ? L"SCROLL FOR MORE"
                 : previewMode                                   ? L"ISOLATED PREVIEW"
                                                                 : L"CTRL + ALT + SPACE",
                 {status.left + 160, status.top, status.right, status.bottom}, type::Mono, theme::Muted,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

void Island::Draw() {
    auto paintStart = steady_clock::now();
    Graphics();
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(renderMode && expanded ? Color(theme::Surface) : D2D1::ColorF(0, 0.0f));
    if (renderMode) {
        if (expanded) {
            target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
            PanelContent();
        } else {
            hits.clear();
            AddHit(Expand, {0, 0, theme::DockWidth, theme::DockHeight}, L"Reveal dock");
            DrawNotch();
        }
    } else {
        if (!YieldResting() && dockReveal.value <= 0.003f)
            DrawNotch();
        if (dockReveal.value > 0.003f) {
            IslandSurface();
            float left = (envelopeWidth - width) / 2;
            target->PushAxisAlignedClip({left, PanelY(), left + width, PanelY() + height},
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale) *
                                 D2D1::Matrix3x2F::Translation(PanelX(), PanelY()));
            PanelContent();
            target->PopAxisAlignedClip();
        } else if (expanded) {
            RebuildLayout();
        } else {
            hits.clear();
            AddHit(Expand, {0, 0, theme::DockWidth, theme::DockHeight}, L"Reveal dock");
        }
    }
    if (!diagnosticsPath.empty() && paintTimes.size() < 8192)
        paintTimes.push_back(duration<double, std::milli>(steady_clock::now() - paintStart).count());
    HRESULT result = target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        ReleaseGraphics();
        InvalidateRect(window, nullptr, FALSE);
    } else {
        Check(result);
        if (composition.Active()) {
            if (beforePresent)
                beforePresent();
            if (FAILED(composition.Present())) {
                ReleaseGraphics();
                InvalidateRect(window, nullptr, FALSE);
            } else if (frameTickReady) {
                frameTickReady = false;
                frameClock.Presented();
            }
        }
    }
    PositionInputs();
}

bool Island::PresentedContains(float x, float y) const {
    if (renderMode)
        return x >= 0 && x <= width && y >= 0 && y <= height;
    float left = (envelopeWidth - width) / 2;
    return dockReveal.value > 0.003f &&
           layout::RoundedContains({left, PanelY(), left + width, PanelY() + height}, theme::Radius, x, y);
}

int Island::HitAt(float x, float y, bool accessibleHit) {
    RebuildLayout();
    if (!expanded) {
        float left = renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2;
        return !YieldResting() && y >= HandleY() && y <= HandleY() + theme::DockHeight && x >= left &&
                       x <= left + theme::DockWidth
                   ? 0
                   : -1;
    }
    if (!Actionable() || !PresentedContains(x, y))
        return -1;
    x = (x - PanelX()) / textScale;
    y = (y - PanelY()) / textScale;
    if (x < 0 || x > LayoutWidth() || y < 0 || y > LayoutHeight())
        return -1;
    for (int i = static_cast<int>(hits.size()) - 1; i >= 0; --i) {
        const auto &h = hits[i];
        auto r = h.rect, c = h.clip;
        if ((h.enabled || accessibleHit) && x >= c.left && x <= c.right && y >= c.top && y <= c.bottom &&
            layout::SkewContains({r.left, r.top, r.right, r.bottom}, h.skew, std::min(10.0f, (r.bottom - r.top) / 2), x,
                                 y))
            return i;
    }
    return -1;
}

void Island::SetVolumeAt(int x) {
    auto rect = VolumeRect();
    SetSystemVolume(((x * 96.0f / dpi - PanelX()) / textScale - rect.left - 12) / (rect.right - rect.left - 24));
    volumeMotion.goal = system.volume;
    ArmMotion();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::Open(const wchar_t *path) {
    if (renderMode || fixtureGeometry)
        return;
    auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", path, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        note = L"Windows could not open this item.";
        InvalidateRect(window, nullptr, FALSE);
    }
}

void Island::LabelInput(HWND input, const wchar_t *name) {
    if (accessibleProperties && input)
        accessibleProperties->SetHwndPropStr(input, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME,
                                             name);
}

void Island::EnsureTaskEdit() {
    if (taskEdit || renderMode || capturingPane || layoutOnly)
        return;
    taskEdit = CreateWindowExW(0, L"EDIT", taskDraft.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1,
                               window, reinterpret_cast<HMENU>(400), nullptr, nullptr);
    LabelInput(taskEdit, L"New task, up to 120 characters");
    SendMessageW(taskEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Add a task…"));
    SendMessageW(taskEdit, EM_SETLIMITTEXT, 120, 0);
    EnsureEditResources();
    SendMessageW(taskEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    SendMessageW(taskEdit, EM_SETSEL, draftStart, draftEnd);
    PositionInputs();
}

void Island::EnsureEditResources() {
    if (!editFont)
        editFont = CreateFontW(-static_cast<int>(std::lround(13 * textScale * dpi / 96)), 0, 0, 0, FW_NORMAL, FALSE,
                               FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    if (!editBackground)
        editBackground = CreateSolidBrush(
            RGB((theme::CardHover >> 16) & 255, (theme::CardHover >> 8) & 255, theme::CardHover & 255));
}

void Island::EnsureAppSearch() {
    if (appSearch || renderMode || capturingPane || layoutOnly)
        return;
    appSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, window,
                                reinterpret_cast<HMENU>(401), nullptr, nullptr);
    LabelInput(appSearch, L"Search installed apps");
    SendMessageW(appSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search installed apps…"));
    SendMessageW(appSearch, EM_SETLIMITTEXT, 120, 0);
    EnsureEditResources();
    SendMessageW(appSearch, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    PositionInputs();
    if (!appQuery.empty())
        SetWindowTextW(appSearch, appQuery.c_str());
}

bool Island::SaveTasks() {
    if (renderMode || previewMode)
        return true;
    std::vector<std::pair<std::wstring, bool>> data;
    for (const auto &task : tasks)
        data.emplace_back(task.title, task.done);
    if (!taskStore.Save(data)) {
        note = L"Could not save tasks. Your draft is kept; try again.";
        return false;
    }
    return true;
}

void Island::OpenApp(const AppEntry &app) {
    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(app.parsingName.c_str(), nullptr, &item, 0, nullptr))) {
        note = L"This application is no longer available.";
        return;
    }
    SHELLEXECUTEINFOW action{sizeof(action)};
    action.fMask = SEE_MASK_IDLIST | SEE_MASK_NOASYNC;
    action.hwnd = window;
    action.lpVerb = L"open";
    action.lpIDList = item;
    action.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&action))
        note = L"Windows could not open this application.";
    else {
        auto key = AppKey(app);
        auto &recent = appPreferences.recent;
        recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
        recent.insert(recent.begin(), key);
        if (recent.size() > 16)
            recent.resize(16);
        if (!previewMode && !appPreferences.Save())
            note = L"Opened app; could not save recent launches.";
        FilterApps(false);
    }
    CoTaskMemFree(item);
}

void Island::ChooseWallpaperFolder() {
    ModalGuard modal(modalOpen);
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose a wallpaper folder");
    if (FAILED(dialog->Show(window)))
        return;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)))
        return;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return;
    if (!catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    wallpaperSource = 1;
    wallpaperFolder = path;
    selectedWallpaper = 0;
    wallpaperFocus.Snap(0);
    wallpapersRequested = true;
    wallpaperLoading = true;
    catalog->DiscoverWallpapers(path);
    CoTaskMemFree(path);
    note = L"Loading pictures from your folder…";
}

void Island::PowerAction(int id) {
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        note = modules::String(IDS_POWER_INERT);
        return;
    }
    ModalGuard modal(modalOpen);
    const wchar_t *prompt = id == SleepNow     ? L"Put this PC to sleep?"
                            : id == RestartNow ? L"Restart Windows? Save your work before continuing."
                                               : L"Shut down Windows? Save your work before continuing.";
    if (MessageBoxW(window, prompt, L"Dako", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    if (id == SleepNow) {
        if (!SetSuspendState(FALSE, FALSE, FALSE))
            note = L"Windows could not put this PC to sleep.";
        return;
    }
    HANDLE token = nullptr;
    bool enabled = false;
    TOKEN_PRIVILEGES previous{};
    DWORD previousSize = sizeof(previous);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            SetLastError(ERROR_SUCCESS);
            enabled = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &previousSize) &&
                      GetLastError() == ERROR_SUCCESS;
        }
    }
    bool succeeded =
        enabled && ExitWindowsEx(id == RestartNow ? EWX_REBOOT : EWX_POWEROFF,
                                 SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
    if (token) {
        if (enabled)
            AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        CloseHandle(token);
    }
    if (!succeeded)
        note = L"Windows could not complete this power action.";
}

void Island::Invoke(int id) {
    if (id >= 5100 && id < 5120) {
        size_t index = modulePage * ModuleSlots() + static_cast<size_t>(id - 5100);
        if (index < modulePanel.rows.size()) InvokeModuleAction(modulePanel.rows[index].action);
        return;
    }
    if (id == 5150 || id == 5151 || id == 5152) {
        if (id == 5150 && modulePage) --modulePage;
        if (id == 5151 && (modulePage + 1) * ModuleSlots() < modulePanel.rows.size()) ++modulePage;
        if (id == 5152 && !renderMode && !fixtureGeometry) {
            ModalGuard modal(modalOpen);
            auto query = PromptText(window, modules::String(IDS_MODULE_SEARCH), modules::String(IDS_MODULE_SEARCH),
                                    moduleQuery, 120);
            if (query) { moduleQuery = *query; modulePage = 0; }
        }
        if (auto panel = moduleRegistry.View(moduleView, moduleQuery)) modulePanel = std::move(*panel);
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 2900 && id < 3000) {
        FeatureAction(id);
        return;
    }
    if (id >= FlyoutRefresh && id <= FlyoutRemove && id != FlyoutSettings &&
        (flyout == Flyout::Wifi ? connections.wifiBusy || connections.wifiConnecting : connections.bluetoothBusy))
        return;
    if (id == 1903) {
        InspectDetails();
        return;
    }
    if (id == RunningPrevious || id == RunningNext) {
        if (id == RunningPrevious && runningPage > 0)
            --runningPage;
        if (id == RunningNext && (runningPage + 1) * 2 < runningIndices.size())
            ++runningPage;
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 2700 && id < 2720) {
        size_t index = recentPage * PaletteSlots() + static_cast<size_t>(id - 2700);
        if (index < recentDocuments.size() && !renderMode && !fixtureGeometry) {
            const auto &item = recentDocuments[index];
            auto result = ShellExecuteW(window, L"open", item.target.c_str(),
                                        item.arguments.empty() ? nullptr : item.arguments.c_str(),
                                        item.directory.empty() ? nullptr : item.directory.c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) > 32)
                SetExpanded(false);
            else
                note = L"Windows could not open this document.";
        }
        return;
    }
    if (id == 2750 || id == 2751) {
        if (id == 2750 && recentPage)
            --recentPage;
        else if (id == 2751 && (recentPage + 1) * PaletteSlots() < recentDocuments.size())
            ++recentPage;
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 2600 && id < 2620) {
        ExecutePalette(static_cast<size_t>(id - 2600));
        return;
    }
    if (id >= 2800 && id < 2807) {
        if (id < 2805)
            dockOptions.statusMask ^= 1U << (id - 2800);
        else if (id == 2805)
            dockOptions.showDesktop = !dockOptions.showDesktop;
        else
            dockOptions.showTaskView = !dockOptions.showTaskView;
        if (!SaveSettings())
            note = L"Dock choices apply for this session; settings could not be saved.";
        UpdateDocks();
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id == 2850) {
        OpenPalette();
        return;
    }
    if (id == 2852 || id == 2853) {
        if (id == 2852 && palettePage)
            --palettePage;
        else if (id == 2853 && (palettePage + 1) * PaletteSlots() < paletteItems.size())
            ++palettePage;
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if ((id >= 2860 && id < 2865) || (id >= 2870 && id < 2875)) {
        if (id == 2871)
            OpenFlyout(Flyout::Wifi);
        else if (id == 2872) {
            SetTab(Tab::Controls);
            RebuildLayout();
            FocusAt(IndexForKey(StableKey(Volume)));
        } else
            SetTab(Tab::Today);
        return;
    }
    if (id >= 2400 && id < 2402) {
        size_t index = runningPage * 2 + static_cast<size_t>(id - 2400);
        if (index < runningIndices.size() && !fixtureGeometry && !renderMode) {
            if (desktop::WindowList::Activate(runningWindows[runningIndices[index]].window))
                SetExpanded(false);
            else
                note = L"Windows did not allow activation. Elevated windows may require an elevated Dako.";
        }
        return;
    }
    if (id >= WeatherCity && id <= CalendarClear) {
        TodayAction(id);
        return;
    }
    if (id == 1900 || id == 1901) {
        OpenCompactMenu(id == 1900);
        return;
    }
    if (id == 1902) {
        ModalGuard guard(modalOpen);
        MessageBoxW(window, note.empty() ? L"No pending status." : note.c_str(), L"Dako status", MB_OK);
        return;
    }
    note.clear();
    if (id == 400 || id == 401 || id == 402 || id == 403 || id == Volume || id == Brightness) {
        FocusAt(IndexForKey(StableKey(id)));
        return;
    }
    if (id >= 1400 && id < 1410 && id - 1400 < static_cast<int>(railApps.size())) {
        OpenApp(apps[railApps[id - 1400]]);
        RebuildLayout();
        return;
    }
    if (id >= 220 && id < 240 && id - 220 < static_cast<int>(tasks.size())) {
        auto saved = tasks;
        tasks.erase(tasks.begin() + id - 220);
        if (!SaveTasks())
            tasks = std::move(saved);
        RebuildLayout();
        ArmMotion();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 900 && id < 1000 && AppIndex(id - 900) < apps.size()) {
        auto key = AppKey(apps[AppIndex(id - 900)]);
        auto &favorites = appPreferences.favorites;
        auto found = std::find(favorites.begin(), favorites.end(), key);
        if (found == favorites.end())
            favorites.push_back(key);
        else
            favorites.erase(found);
        if (!renderMode && !previewMode && !appPreferences.Save())
            note = L"Could not save starred apps. Try again.";
        UpdateDocks();
        FilterApps(false);
        RebuildLayout();
        ArmMotion();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 1000 && id < 1128 && id - 1000 < static_cast<int>(connections.networks.size())) {
        selectedWifi = id - 1000;
        connectionPage = static_cast<size_t>(selectedWifi) / ConnectionSlots();
        if (wifiPassword)
            SetWindowTextW(wifiPassword, L"");
        PositionInputs();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 1200 && id < 1328 && id - 1200 < static_cast<int>(connections.devices.size())) {
        selectedBluetooth = id - 1200;
        connectionPage = static_cast<size_t>(selectedBluetooth) / ConnectionSlots();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id >= 100 && id < 107) {
        SetTab(static_cast<Tab>(id - 100));
        return;
    }
    if (id >= 200 && id < 220 && id - 200 < static_cast<int>(tasks.size())) {
        tasks[id - 200].done = !tasks[id - 200].done;
        if (!SaveTasks())
            tasks[id - 200].done = !tasks[id - 200].done;
    } else if (id >= 300 && id < 400 && AppIndex(id - 300) < apps.size())
        OpenApp(apps[AppIndex(id - 300)]);
    else if (id >= 500 && id < 500 + static_cast<int>(DesktopCatalog::MaxWallpapers) &&
             id - 500 < static_cast<int>(wallpapers.size()))
        selectedWallpaper = id - 500;
    else
        switch (id) {
        case Expand:
            if (miniMode) {
                miniMode = false;
                tab = Tab::Controls;
            }
            SetExpanded(true);
            break;
        case Wifi:
            OpenFlyout(Flyout::Wifi);
            break;
        case Bluetooth:
            OpenFlyout(Flyout::Bluetooth);
            break;
        case Audio:
            outputPage = 0;
            OpenFlyout(Flyout::Audio);
            break;
        case Night:
            Open(L"ms-settings:nightlight");
            break;
        case Dark:
        case Airplane:
        case AirplaneSettings:
        case ProjectDisplay:
        case ScreenSnip:
        case OutputSettings:
        case OutputPrevious:
        case OutputNext:
            ControlAction(id);
            break;
        case StatsSort:
            statsMemory = !statsMemory;
            break;
        case Mute:
            ToggleSystemMute();
            break;
        case Previous:
        case Play:
        case Next:
            if (media)
                media->Transport(id - Previous);
            break;
        case AddTask: {
            wchar_t text[121]{};
            if (taskEdit)
                GetWindowTextW(taskEdit, text, 121);
            if (tasks.size() >= TaskStore::Capacity)
                note = L"There is room for 20 tasks. Delete one to add another.";
            else if (wcslen(text)) {
                tasks.push_back({text, false, nextTask++});
                if (SaveTasks()) {
                    SetWindowTextW(taskEdit, L"");
                    taskDraft.clear();
                } else
                    tasks.pop_back();
            } else if (taskEdit)
                SetFocus(taskEdit);
            break;
        }
        case ClipboardRefresh:
            ReadClipboard();
            break;
        case ClipboardClear:
            clipboard.clear();
            clipboardState = L"Preview cleared";
            if (clipboardEdit)
                SetWindowTextW(clipboardEdit, L"");
            break;
        case WallpaperSettings:
            Open(L"ms-settings:personalization-background");
            break;
        case PowerSettings:
            Open(L"ms-settings:powersleep");
            break;
        case AppsPrevious:
            if (appPage > 0)
                --appPage;
            break;
        case AppsNext:
            if (appPage + 1 < appPages.size())
                ++appPage;
            break;
        case WallpaperFolder:
            if (wallpaperSource != 1 && !wallpaperFolder.empty())
                WallpaperSource(1);
            else
                ChooseWallpaperFolder();
            break;
        case WallpaperCollection:
        case WallpaperCurrent:
        case WallpaperPrevious:
        case WallpaperNext:
        case WallpaperApply:
        case WallpaperTarget:
        case WallpaperFit:
            WallpaperAction(id);
            break;
        case SleepNow:
        case RestartNow:
        case ShutdownNow:
            PowerAction(id);
            break;
        case FlyoutBack:
            SetTab(flyout == Flyout::Palette || flyout == Flyout::Recent ? Tab::Apps : Tab::Controls);
            break;
        case FlyoutRefresh:
            if (connectivity) {
                if (flyout == Flyout::Wifi)
                    connectivity->RefreshWifi(true);
                else
                    connectivity->RefreshBluetooth(true);
            }
            break;
        case FlyoutToggle: {
            if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
                note = L"Radio changes are disabled in preview.";
                break;
            }
            if (!PrepareSystemChange())
                break;
            if (!nativeControls)
                RefreshControls();
            if (nativeControls) {
                try {
                    using namespace winrt::Windows::Devices::Radios;
                    bool off = flyout == Flyout::Wifi ? connections.wifiOn : connections.bluetoothOn;
                    nativeControls->SetAirplane(off, Radio::RequestAccessAsync(),
                                                flyout == Flyout::Wifi ? RadioKind::WiFi : RadioKind::Bluetooth);
                } catch (const winrt::hresult_error &) {
                    note = L"Windows did not allow radio control. Open Radio settings.";
                }
            }
            break;
        }
        case FlyoutSettings:
        case FlyoutConnect:
        case FlyoutDisconnect:
        case FlyoutPair:
        case FlyoutRemove:
            Open(flyout == Flyout::Wifi ? L"ms-settings:network-wifi" : L"ms-settings:bluetooth");
            break;
        case FlyoutPrevious:
            if (connectionPage > 0)
                --connectionPage;
            break;
        case FlyoutNext: {
            size_t count = flyout == Flyout::Wifi ? connections.networks.size() : connections.devices.size();
            if ((connectionPage + 1) * ConnectionSlots() < count)
                ++connectionPage;
            break;
        }
        case FocusStart:
            if (focusRunning) {
                focusSeconds =
                    static_cast<int>((focusDeadline > GetTickCount64() ? focusDeadline - GetTickCount64() : 0) + 999) /
                    1000;
                focusRunning = false;
            } else {
                if (focusSeconds <= 0)
                    focusSeconds = focusDuration;
                focusDeadline = GetTickCount64() + static_cast<ULONGLONG>(focusSeconds) * 1000;
                focusRunning = true;
            }
            UpdateTimers();
            break;
        case FocusReset:
            focusRunning = false;
            focusSeconds = focusDuration;
            UpdateTimers();
            break;
        case FocusOne:
        case FocusShort:
        case FocusTen:
        case FocusMedium:
        case FocusTwenty:
        case FocusLong:
            focusRunning = false;
            focusDuration = focusSeconds = id == FocusOne      ? 60
                                           : id == FocusShort  ? 300
                                           : id == FocusTen    ? 600
                                           : id == FocusMedium ? 900
                                           : id == FocusTwenty ? 1200
                                                               : 1500;
            UpdateTimers();
            break;
        case Lock:
            if (!renderMode && !previewMode) {
                if (!LockWorkStation())
                    note = L"Windows could not lock this session.";
            } else
                note = L"Power actions are disabled in preview.";
            break;
        case Quit:
            PostMessageW(window, WM_CLOSE, 0, 0);
            break;
        }
    wallpaperFocus.goal = static_cast<float>(selectedWallpaper);
    if (reducedMotion || renderMode)
        wallpaperFocus.Snap(wallpaperFocus.goal);
    else if (wallpaperFocus.Moving())
        ArmMotion();
    RebuildLayout();
    RequestVisibleIcons();
    if (!reducedMotion && !renderMode)
        ArmMotion();
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
    InvalidateRect(window, nullptr, FALSE);
}

void Island::FocusAt(int index) {
    if (index < 0 || index >= static_cast<int>(hits.size()) || !hits[index].enabled)
        return;
    keyboardOpen = true;
    focusedKey = hits[index].key;
    int action = hits[index].id;
    if (tab == Tab::Today && action >= 200 && action < 240) {
        size_t row = static_cast<size_t>(action >= 220 ? action - 220 : action - 200);
        auto list = TaskListRect();
        float top = list.top + static_cast<float>(row) * 40 - taskScroll;
        if (top < list.top)
            taskScroll -= list.top - top;
        else if (top + 36 > list.bottom)
            taskScroll += top + 36 - list.bottom;
        auto b = Body();
        float listTop = list.top - scroll, listBottom = list.bottom - scroll;
        if (listBottom > b.bottom)
            scroll += listBottom - b.bottom;
        if (listTop < b.top)
            scroll = std::max(0.0f, scroll + listTop - b.top);
        RebuildLayout();
        index = IndexForKey(focusedKey);
        if (index < 0)
            return;
    }
    auto h = hits[index];
    auto b = Body();
    if (h.clip.top == b.top && h.clip.bottom == b.bottom) {
        if (h.rect.top < b.top)
            scroll = std::max(0.0f, scroll + h.rect.top - b.top);
        else if (h.rect.bottom > b.bottom)
            scroll += h.rect.bottom - b.bottom;
        RebuildLayout();
        index = IndexForKey(focusedKey);
    }
    focused = index;
    if (index >= 0 && hits[index].id >= 300 && hits[index].id < 400 && AppIndex(hits[index].id - 300) < apps.size())
        selectedAppKey = AppKey(apps[AppIndex(hits[index].id - 300)]);
    if (index >= 0) {
        auto &hit = hits[index];
        if (hit.id == 400)
            EnsureTaskEdit();
        if (hit.id == 401)
            EnsureAppSearch();
        if (hit.id == 402)
            EnsurePassword();
        if (hit.id == 403)
            EnsureClipboardEdit();
        RebuildLayout();
        PositionInputs();
        index = IndexForKey(focusedKey);
        if (index >= 0 && hits[index].input && IsWindowVisible(hits[index].input))
            SetFocus(hits[index].input);
        else
            SetFocus(window);
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window, OBJID_CLIENT, h.child);
    }
    if (index >= 0 && hits[index].id >= 1400 && hits[index].id < 1410) {
        auto rect = hits[index].rect;
        railPointer.goal = (rect.left + rect.right) / 2;
        railStrength.goal = 1;
        ArmMotion();
    }
    InvalidateRect(window, nullptr, FALSE);
}

void Island::Key(WPARAM key) {
    if (key == VK_ESCAPE && DockEnabled() && expanded && flyout == Flyout::None) {
        SetExpanded(false);
        if (docks)
            docks->Focus(panelMonitor);
        return;
    }
    if (key == VK_F2) {
        InspectDetails();
        return;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && (key == VK_NEXT || key == VK_PRIOR)) {
        auto found = std::find(PrimaryTabs.begin(), PrimaryTabs.end(), tab);
        size_t index = found == PrimaryTabs.end() ? 0 : static_cast<size_t>(found - PrimaryTabs.begin());
        SetTab(PrimaryTabs[(index + (key == VK_NEXT ? 1 : 4)) % 5]);
        return;
    }
    RebuildLayout();
    if (key == VK_ESCAPE) {
        if (flyout != Flyout::None)
            SetTab(Tab::Controls);
        else
            SetExpanded(false);
        return;
    }
    int native = GetFocus() == taskEdit        ? 400
                 : GetFocus() == appSearch     ? 401
                 : GetFocus() == wifiPassword  ? 402
                 : GetFocus() == clipboardEdit ? 403
                                               : 0;
    if (native)
        focusedKey = StableKey(native);
    focused = IndexForKey(focusedKey);
    if (key == VK_TAB && !hits.empty()) {
        int direction = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1, index = focused;
        if (index < 0)
            index = direction > 0 ? -1 : 0;
        for (size_t i = 0; i < hits.size(); ++i) {
            index = (index + direction + static_cast<int>(hits.size())) % static_cast<int>(hits.size());
            if (hits[index].enabled) {
                FocusAt(index);
                break;
            }
        }
        return;
    }
    if (key == VK_NEXT || key == VK_PRIOR) {
        scroll += key == VK_NEXT ? Body().bottom - Body().top : Body().top - Body().bottom;
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, Layout().contentHeight - Layout().body.Height()));
        RebuildLayout();
        PositionInputs();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (focused < 0)
        return;
    int id = hits[focused].id;
    if (!Actionable() && !(id == Expand && !expanded))
        return;
    if (key == VK_DELETE && id >= 200 && id < 220) {
        Invoke(id + 20);
        return;
    }
    if (id >= 100 && id < 107 && (key == VK_LEFT || key == VK_RIGHT)) {
        auto found = std::find(PrimaryTabs.begin(), PrimaryTabs.end(), static_cast<Tab>(id - 100));
        size_t index = found == PrimaryTabs.end() ? 0 : static_cast<size_t>(found - PrimaryTabs.begin());
        auto next = PrimaryTabs[(index + (key == VK_RIGHT ? 1 : 4)) % 5];
        SetTab(next);
        focusedKey = StableKey(100 + static_cast<int>(next));
        return;
    }
    if (tab == Tab::Wallpaper && flyout == Flyout::None && focused >= 0 && hits[focused].id >= 500 &&
        hits[focused].id < 564 && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN)) {
        int columns = Body().right - Body().left >= 600 ? 3 : Body().right - Body().left >= 380 ? 2 : 1;
        int delta = key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : key == VK_UP ? -columns : columns;
        if (!wallpapers.empty())
            selectedWallpaper = static_cast<size_t>(
                std::clamp(static_cast<int>(selectedWallpaper) + delta, 0, static_cast<int>(wallpapers.size()) - 1));
        RebuildLayout();
        FocusAt(IndexForKey(StableKey(500 + static_cast<int>(selectedWallpaper))));
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id == Brightness) {
        int delta = (key == VK_LEFT || key == VK_DOWN) ? -5 : (key == VK_RIGHT || key == VK_UP) ? 5 : 0;
        if (delta || key == VK_HOME || key == VK_END) {
            controlState.brightness = key == VK_HOME  ? 0
                                      : key == VK_END ? 100
                                                      : std::clamp(controlState.brightness + delta, 0, 100);
            if (controls::MayWrite(renderMode, fixtureGeometry, previewMode) && nativeControls && PrepareSystemChange())
                nativeControls->SetBrightness(controlState.monitor, controlState.brightness);
            NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, hits[focused].child);
            InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (id == Volume) {
        if (key == VK_LEFT || key == VK_DOWN)
            SetSystemVolume(system.volume - 0.02f);
        if (key == VK_RIGHT || key == VK_UP)
            SetSystemVolume(system.volume + 0.02f);
        if (key == VK_SPACE)
            ToggleSystemMute();
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, hits[focused].child);
        volumeMotion.goal = system.volume;
        ArmMotion();
        InvalidateRect(window, nullptr, FALSE);
    } else if (key == VK_RETURN || key == VK_SPACE) {
        auto &f = feedback[focusedKey];
        f.value = 1;
        f.goal = 0;
        Invoke(id);
    } else if ((key == VK_DOWN || key == VK_UP) && tab == Tab::Apps) {
        int direction = key == VK_DOWN ? 1 : -1;
        for (int i = focused + direction; i >= 0 && i < static_cast<int>(hits.size()); i += direction)
            if (hits[i].id >= 300 && hits[i].id < 400) {
                FocusAt(i);
                break;
            }
    }
}

void Island::InspectDetails() {
    RebuildLayout();
    std::wstring detail;
    int index = IndexForKey(focusedKey);
    if (index < 0)
        index = IndexForKey(hoveredKey);
    if (index >= 0)
        detail = hits[index].name;
    for (const auto &hit : hits)
        if (ReadingControl(hit.id) && hit.id != 1800) {
            if (!detail.empty())
                detail += L"\n\n";
            detail += hit.name;
        }
    if (!note.empty()) {
        if (!detail.empty())
            detail += L"\n\n";
        detail += note;
    }
    if (detail.empty())
        detail = L"Use Tab to focus a control, then F2 to read its full name. Ctrl+Page Up/Down changes views.";
    ModalGuard guard(modalOpen);
    MessageBoxW(window, detail.c_str(), L"Dako — full details", MB_OK | MB_ICONINFORMATION);
}

void Island::OpenCompactMenu(bool views) {
    ModalGuard guard(modalOpen);
    HMENU menu = CreatePopupMenu();
    auto add = [&](int id, const std::wstring &label, bool enabled = true) {
        AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), static_cast<UINT_PTR>(id), label.c_str());
    };
    if (views) {
        for (Tab primary : PrimaryTabs) {
            int i = static_cast<int>(primary);
            constexpr const wchar_t *names[]{L"Controls", L"Today",     L"Stats", L"Clipboard",
                                             L"Apps",     L"Wallpaper", L"Power"};
            add(100 + i, names[i]);
        }
    } else if (flyout == Flyout::Module) {
        add(FlyoutBack, modules::String(IDS_BACK));
        add(5150, modules::String(IDS_PREVIOUS_ITEMS), modulePage > 0);
        add(5151, modules::String(IDS_NEXT_ITEMS), (modulePage + 1) * ModuleSlots() < modulePanel.rows.size());
        add(5152, modules::String(IDS_MODULE_SEARCH));
    } else if (flyout == Flyout::Features) {
        add(FlyoutBack, modules::String(IDS_BACK));
        const UINT pages[]{IDS_FEATURES, IDS_PRESETS, IDS_LEDGER, IDS_COSTS};
        for (int page = 0; page < 4; ++page) add(2901 + page, modules::String(pages[page]));
        if (featuresPage == 0) {
            size_t index = 0;
            for (const auto *descriptor : moduleRegistry.Descriptors())
                add(2920 + static_cast<int>(index++),
                    modules::String(moduleRegistry.Enabled(descriptor->id) ? IDS_REMOVE : IDS_ENABLE) + L" · " +
                    modules::String(descriptor->name));
        } else if (featuresPage == 1) {
            const UINT presets[]{IDS_MINIMAL, IDS_TASKBAR, IDS_POWER_USER, IDS_CREATOR};
            for (int i = 0; i < 4; ++i) add(2910 + i, modules::String(presets[i]));
        } else if (featuresPage == 3)
            add(2940, modules::String(IDS_REFRESH_COSTS));
        if (featuresPage == 0) {
            add(2942, modules::String(IDS_EXPORT_SETTINGS), !previewMode && !renderMode && !fixtureGeometry);
            add(2943, modules::String(IDS_IMPORT_SETTINGS), !previewMode && !renderMode && !fixtureGeometry);
        } else if (featuresPage == 2)
            add(2941, modules::String(privacy::Policy().Offline() ? IDS_OFFLINE : IDS_ONLINE));
        if (featuresPage == 2) {
            add(2944, modules::String(IDS_PREVIOUS_OPERATIONS), privacyPage > 0);
            add(2945, modules::String(IDS_NEXT_OPERATIONS), (privacyPage + 1) * 8 < privacySnapshot.events.size());
        }
    } else if (flyout == Flyout::Palette) {
        add(FlyoutBack, L"Back to Apps");
        add(401, L"Search commands and apps");
        add(2852, L"Previous results", palettePage > 0);
        add(2853, L"Next results", (palettePage + 1) * PaletteSlots() < paletteItems.size());
    } else if (flyout == Flyout::DockStatus) {
        add(FlyoutBack, L"Back to island");
        add(2850, L"Search all commands");
        for (int i = 0; i < 5; ++i)
            add(2800 + i, L"Toggle dock status widget " + std::to_wstring(i + 1));
    } else if (flyout == Flyout::Recent) {
        add(FlyoutBack, L"Back to Apps");
        add(2750, L"Previous documents", recentPage > 0);
        add(2751, L"Next documents", (recentPage + 1) * PaletteSlots() < recentDocuments.size());
    } else if (flyout == Flyout::Audio) {
        add(FlyoutBack, L"Back to Controls");
        add(OutputSettings, L"Open sound settings");
        add(OutputPrevious, L"Previous outputs", outputPage > 0);
        add(OutputNext, L"Next outputs");
    } else if (flyout != Flyout::None) {
        bool wifi = flyout == Flyout::Wifi,
             busy = wifi ? connections.wifiBusy || connections.wifiConnecting : connections.bluetoothBusy;
        add(FlyoutRefresh, wifi ? L"Refresh networks" : L"Find devices", !busy);
        add(FlyoutToggle, L"Toggle radio", !busy);
        add(FlyoutSettings, L"More settings");
        add(FlyoutPrevious, L"Previous devices", connectionPage > 0);
        add(FlyoutNext, L"Next devices",
            (connectionPage + 1) * ConnectionSlots() <
                (wifi ? connections.networks.size() : connections.devices.size()));
        if (wifi && selectedWifi >= 0) {
            const auto &network = connections.networks[selectedWifi];
            add(network.connected ? FlyoutDisconnect : FlyoutConnect, L"Wi-Fi settings ↗", !busy);
        }
        if (!wifi && selectedBluetooth >= 0) {
            const auto &device = connections.devices[selectedBluetooth];
            add(device.connected ? FlyoutDisconnect
                : device.paired  ? FlyoutConnect
                                 : FlyoutPair,
                L"Bluetooth settings ↗", !busy);
        }
    } else
        switch (tab) {
        case Tab::Controls:
            add(Wifi, L"Wi-Fi");
            add(Bluetooth, L"Bluetooth");
            add(Volume, L"Volume");
            add(Audio, L"Audio outputs");
            add(Dark, L"Toggle dark mode");
            if (controlState.brightnessSupported)
                add(Brightness, L"Display brightness");
            if (controlState.radiosAvailable)
                add(Airplane, L"Toggle supported radios");
            add(AirplaneSettings, L"Radio settings");
            add(ProjectDisplay, L"Project display");
            add(ScreenSnip, L"Screen snip");
            break;
        case Tab::Today:
            add(400, L"New task", tasks.size() < TaskStore::Capacity);
            add(AddTask, L"Add task", tasks.size() < TaskStore::Capacity);
            add(FocusStart, focusRunning ? L"Pause focus" : L"Start focus");
            add(FocusReset, L"Reset focus");
            break;
        case Tab::Stats:
            add(StatsSort, statsMemory ? L"Rank processes by CPU" : L"Rank processes by memory");
            break;
        case Tab::Clipboard:
            add(403, L"Read current text");
            add(ClipboardRefresh, L"Refresh");
            add(ClipboardClear, L"Clear preview");
            break;
        case Tab::Apps:
            add(401, L"Search installed apps");
            add(AppsPrevious, L"Previous apps", appPage > 0);
            add(AppsNext, L"Next apps", appPage + 1 < appPages.size());
            break;
        case Tab::Wallpaper:
            add(WallpaperPrevious, L"Previous wallpaper", selectedWallpaper > 0);
            add(WallpaperNext, L"Next wallpaper", selectedWallpaper + 1 < wallpapers.size());
            add(WallpaperApply, wallpaperApplying ? L"Changing…" : L"Set wallpaper",
                !wallpaperApplying && !wallpapers.empty());
            add(WallpaperCollection, L"Collection");
            add(WallpaperFolder, L"Choose folder");
            add(WallpaperCurrent, L"Current wallpapers");
            add(WallpaperTarget, L"Cycle target display");
            add(WallpaperFit, L"Cycle fit for all displays");
            break;
        case Tab::Power:
            add(Lock, L"Lock");
            add(SleepNow, L"Sleep");
            add(RestartNow, L"Restart");
            add(ShutdownNow, L"Shut down");
            break;
        }
    if (!views) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(2900, modules::String(IDS_FEATURES));
        if (tab == Tab::Today) {
            add(TodayOptions, L"Today settings…");
        }
        add(Lock, L"Lock");
        add(SleepNow, L"Sleep…");
        add(RestartNow, L"Restart…");
        add(ShutdownNow, L"Shut down…");
        add(1903, L"Full details…\tF2");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(1902, L"Read status…", !note.empty());
        add(Quit, L"Exit and restore Windows");
    }
    RECT bounds{};
    GetWindowRect(window, &bounds);
    float scale = dpi / 96;
    int x = bounds.left + static_cast<int>((PanelX() + Layout().header.left * textScale) * scale),
        y = bounds.top + static_cast<int>((PanelY() + Layout().headerHeight * textScale) * scale);
    SetForegroundWindow(window);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command)
        Invoke(command);
}

void Island::ContextMenu() {
    ModalGuard modal(modalOpen);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 18, L"Full details…\tF2");
    AppendMenuW(menu, MF_STRING, 1, expanded ? L"Hide dock" : L"Show dock");
    AppendMenuW(menu, MF_STRING, 4,
                shell.Active() ? L"Restore Windows taskbar\tCtrl+Alt+Shift+T"
                               : L"Hide Windows taskbar\tCtrl+Alt+Shift+T");
    AppendMenuW(menu, MF_STRING, 19, dockMode ? L"Prefer top notch layout" : L"Prefer floating dock layout");
    AppendMenuW(menu, MF_STRING, 20, L"Focus dock controls");
    AppendMenuW(menu, MF_STRING, 5, miniMode ? L"Full controls" : L"Compact media player");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (motionOverride ? MF_CHECKED : 0), 10, L"Reduced motion");
    AppendMenuW(menu, MF_STRING | (opaqueMaterial ? MF_CHECKED : 0), 11, L"Opaque material");
    AppendMenuW(menu, MF_STRING | (systemAppearance ? MF_CHECKED : 0), 12, L"Follow system appearance");
    AppendMenuW(menu, MF_STRING | (!systemAppearance && lightAppearance ? MF_CHECKED : 0), 13, L"Light appearance");
    AppendMenuW(menu, MF_STRING | (!systemAppearance && !lightAppearance ? MF_CHECKED : 0), 14, L"Dark appearance");
    AppendMenuW(menu, MF_STRING | (shell.MaximizeNew() ? MF_CHECKED : 0), 15, L"Maximize new windows in desktop mode");
    AppendMenuW(menu, MF_STRING, 16, (L"Browser clearance: " + Number(browserClearance) + L" DIP (cycle)").c_str());
    AppendMenuW(menu, MF_STRING | (fixedAccent ? MF_CHECKED : 0), 17, L"Portfolio orange accent");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Exit and restore Windows\tCtrl+Alt+Shift+Q");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command == 18)
        InspectDetails();
    else if (command == 1) {
        SetExpanded(!expanded);
        keyboardOpen = expanded;
    } else if (command == 2)
        PostMessageW(window, WM_CLOSE, 0, 0);
    else if (command == 4) {
        ToggleDesktopMode();
    } else if (command == 19) {
        StopDesktop();
        dockMode = !dockMode;
        browserContext = false;
        fullscreenContext = false;
        if (!SaveSettings())
            note = L"Layout changed for this session; settings could not be saved.";
        StartDesktop();
        RefreshGeometry();
        if (dockMode && !desktopRequested)
            note = L"The floating dock needs desktop mode. Use Dako as my taskbar to enable it.";
    } else if (command == 20 && docks)
        docks->Focus(panelMonitor);
    else if (command >= 10 && command <= 17) {
        if (command == 10) {
            motionOverride = !motionOverride;
            reducedMotion = systemReduced || motionOverride;
            if (reducedMotion)
                SnapMotion();
        }
        if (command == 11) {
            opaqueMaterial = !opaqueMaterial;
            ReleaseGraphics();
        }
        if (command == 12)
            systemAppearance = true;
        if (command == 13) {
            systemAppearance = false;
            lightAppearance = true;
        }
        if (command == 14) {
            systemAppearance = false;
            lightAppearance = false;
        }
        if (command == 15)
            shell.SetMaximizeNew(!shell.MaximizeNew());
        if (command == 16)
            browserClearance = browserClearance >= 192 ? 112 : browserClearance + 40;
        if (command == 17)
            fixedAccent = !fixedAccent;
        if (!SaveSettings())
            note = L"Could not save settings. Changes apply to this session.";
        ConfigureAppearance();
        RefreshGeometry();
    } else if (command == 5) {
        miniMode = !miniMode;
        tab = Tab::Controls;
        flyout = Flyout::None;
        SetExpanded(true);
        keyboardOpen = true;
    }
}

void Island::SaveRender(const std::wstring &path) {
    privacy::Write audit(L"diagnostics", L"file-write", path, L"fixture-render");
    ReleaseGraphics();
    ComPtr<IWICImagingFactory> images;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images)));
    ComPtr<IWICBitmap> bitmap;
    UINT imageWidth = static_cast<UINT>(std::lround((renderMode ? width : envelopeWidth) * dpi / 96)),
         imageHeight = static_cast<UINT>(std::lround((renderMode ? height : envelopeHeight) * dpi / 96));
    Check(images->CreateBitmap(imageWidth, imageHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap));
    Check(factory->CreateWicBitmapRenderTarget(
        bitmap.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi,
                                     dpi),
        &target));
    Check(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush));
    Draw();
    ComPtr<IWICStream> stream;
    Check(images->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(images->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(imageWidth, imageHeight));
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    Check(frame->SetPixelFormat(&pixelFormat));
    Check(frame->WriteSource(bitmap.Get(), nullptr));
    Check(frame->Commit());
    Check(encoder->Commit());
    audit.Complete(true);
    ReleaseGraphics();
}

void Island::AssertScene() {
    RebuildLayout();
    auto l = Layout();
    if (SurfaceWidth() > envelopeWidth + 0.1f || SurfaceHeight() > envelopeHeight - PanelY() + 0.1f)
        winrt::throw_hresult(E_UNEXPECTED);
    if (expanded && !miniMode && l.emergency) {
        bool actions = false;
        for (const auto &h : hits)
            if (h.id == 1901 && VisibleHit(h))
                actions = true;
        if (!actions)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    if (flyout == Flyout::Audio && l.emergency && IndexForKey(StableKey(FlyoutBack)) < 0)
        winrt::throw_hresult(E_UNEXPECTED);
    for (const auto &h : hits) {
        if (h.key.empty() || h.child <= 0)
            winrt::throw_hresult(E_UNEXPECTED);
        if (h.clip.left < 0 || h.clip.top < 0 || h.clip.right > LayoutWidth() + 0.1f ||
            h.clip.bottom > LayoutHeight() + 0.1f)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    for (size_t i = 0; i < hits.size(); ++i)
        for (size_t j = i + 1; j < hits.size(); ++j) {
            if (hits[i].key == hits[j].key)
                winrt::throw_hresult(E_UNEXPECTED);
        }
    if (tab == Tab::Apps && !filteredApps.empty() && flyout == Flyout::None && !Layout().emergency) {
        auto first = std::find_if(hits.begin(), hits.end(), [](const Hit &h) { return h.id == 300; });
        if (first == hits.end() || first->rect.bottom > Body().bottom + 0.1f)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    if (flyout == Flyout::None && !miniMode && expanded && !Layout().emergency) {
        for (Tab primary : PrimaryTabs) {
            int i = static_cast<int>(primary);
            auto rect = TabRect(i);
            int index = HitAt((rect.left + rect.right) / 2 * textScale, (rect.top + rect.bottom) / 2 * textScale);
            if (index < 0 || hits[index].id != 100 + i)
                winrt::throw_hresult(E_UNEXPECTED);
        }
    }
}

void Island::RenderSuite(const std::wstring &directory) {
    fixtureGeometry = true;
    reducedMotion = true;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    systemAppearance = false;
    lightAppearance = true;
    ConfigureAppearance();
    hardwareState.cpu = L"Fixture processor";
    hardwareState.gpu = L"Fixture graphics";
    hardwareState.gpuValid = true;
    hardwareState.gpuPercent = 12;
    hardwareState.vramGiB = 8;
    hardwareState.totalGiB = 32;
    hardwareState.processes = 229;
    hardwareState.top = {
        {L"Browser", 1400, 101}, {L"Browser", 820, 102}, {L"Music", 410, 103}, {L"Terminal", 300, 104}};
    system.cpuValid = system.memoryValid = system.diskValid = system.networkValid = true;
    system.totalCpu = 37;
    system.memoryLoad = 61;
    system.availableGiB = 12.5;
    system.diskRoot = L"C:\\";
    system.diskTotalGiB = 512;
    system.diskUsedGiB = 256;
    system.volume = 0.42f;
    system.wifi = L"Fixture network";
    system.wifiConnected = true;
    system.bluetooth = L"Fixture headphones";
    system.audioName = L"Fixture audio output";
    controlState.darkKnown = true;
    controlState.brightnessSupported = true;
    controlState.brightness = 64;
    controlState.radiosAvailable = true;
    controlState.outputs = {{L"fixture:speakers", L"Fixture speakers", true},
                            {L"fixture:headphones", L"Fixture headphones", false}};
    wallpaperDisplays = {{L"fixture:left", L"Display 1", L"", {0, 0, 1920, 1080}},
                         {L"fixture:right", L"Display 2", L"", {1920, 0, 3840, 1080}}};
    wallpaperTarget = wallpaperDisplays.front().id;
    hardwareState.volumes = {{L"C:", 512, 256, true}, {L"D:", 1024, 420, true}, {L"E:", 0, 0, false}};
    hardwareState.uptimeSeconds = 183600;
    hardwareState.batteryPresent = true;
    hardwareState.batterySeconds = 7200;
    for (size_t i = 0; i < hardwareState.top.size(); ++i) {
        hardwareState.top[i].cpu = 32.0 / (i + 1);
        hardwareState.top[i].cpuValid = true;
    }
    hardwareState.memoryTop = hardwareState.top;
    for (uint64_t i = 1; i <= 60; ++i)
        hardwareState.history.Push(i * 1000, {30 + 20 * std::sin(i * .3), 60 + 4 * std::cos(i * .2),
                                              1000000.0 * (1 + std::sin(i * .2)), 100000, true, true, true});
    wallpapers = DesktopCatalog::LoadWallpapers(DesktopCatalog::BundledWallpaperRoot());
    if (wallpapers.size() != 18)
        winrt::throw_hresult(E_UNEXPECTED);
    auto bundled = wallpapers;
    weather = {true, 18, 21, 12, 2, L"Fixture city", L"Offline weather fixture"};
    weatherCity = L"Fixture city";
    subscriptionIcs = L"BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:20261023T180000\nSUMMARY:Evening walk\nEND:VEVENT\n"
                      L"BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261024\nSUMMARY:Weekend plans\nEND:VEVENT\n"
                      L"BEGIN:VEVENT\nDTSTART:20261026T090000\nSUMMARY:Weekly planning\nRRULE:FREQ=WEEKLY;COUNT=8\n"
                      L"END:VEVENT\nEND:VCALENDAR";
    RebuildCalendar();
    for (int i = 0; i < 5; ++i)
        tasks.push_back({i == 0 ? L"A long task title with mixed scripts · 日本語 · emoji ✨ that must trim safely"
                                : L"Task " + std::to_wstring(i + 1),
                         i == 1, nextTask++});
    for (int i = 0; i < 108; ++i)
        apps.emplace_back(L"Application " + std::to_wstring(i + 1), L"fixture:" + std::to_wstring(i));
    appPreferences.favorites = {L"fixture:0", L"fixture:4", L"fixture:7"};
    appPreferences.recent = {L"fixture:6", L"fixture:5", L"fixture:2"};
    FilterApps();
    tab = Tab::Apps;
    expanded = true;
    appQuery = L"Application 108";
    FilterApps();
    if (dockHeight.goal != SurfaceHeight())
        winrt::throw_hresult(E_UNEXPECTED);
    appQuery.clear();
    FilterApps();
    if (dockHeight.goal != SurfaceHeight())
        winrt::throw_hresult(E_UNEXPECTED);
    expanded = false;
    clipboard = L"Current text preview\r\nMultiple lines, mixed scripts 日本語 and emoji ✨.\r\nThis reader has native "
                L"scrolling and selection. No history is collected.";
    clipboardState = L"Current text · select or scroll to read";
    constexpr const wchar_t *names[]{L"controls", L"today", L"stats", L"clipboard", L"apps", L"wallpaper", L"power"};
    size_t captures = 0;
    auto reportPath = std::filesystem::path(directory) / L"verification.txt";
    privacy::Write verification(L"diagnostics", L"file-write", reportPath.wstring(), L"fixture-report");
    std::wofstream report(reportPath, std::ios::trunc);
    auto capture = [&](const std::wstring &name) {
        report << "render " << name << std::endl;
        width = SurfaceWidth();
        height = SurfaceHeight();
        dockWidth.Snap(width);
        dockHeight.Snap(height);
        dockReveal.Snap(1);
        expanded = true;
        transitioning = false;
        paneBlend.Snap(1);
        SaveRender(directory + L"\\" + name + L".png");
        AssertScene();
        ++captures;
    };
    for (int i = 0; i < 7; ++i) {
        tab = static_cast<Tab>(i);
        scroll = 0;
        capture(names[i]);
    }
    tab = Tab::Wallpaper;
    selectedWallpaper = wallpapers.size() - 1;
    wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));
    capture(L"wallpaper-last");
    note = L"Could not apply wallpaper. Choose another image.";
    capture(L"wallpaper-error");
    note.clear();
    auto sixtyFour = bundled;
    while (sixtyFour.size() < 64) {
        auto entry = bundled[sixtyFour.size() % bundled.size()];
        entry.path = L"fixture:" + std::to_wstring(sixtyFour.size());
        sixtyFour.push_back(entry);
    }
    ReplaceWallpapers(sixtyFour);
    selectedWallpaper = 63;
    wallpaperFocus.Snap(63);
    capture(L"wallpaper-64");
    ReplaceWallpapers({bundled.front()});
    if (selectedWallpaper != 0 || wallpaperFocus.value != 0)
        winrt::throw_hresult(E_UNEXPECTED);
    capture(L"wallpaper-one");
    ReplaceWallpapers({});
    if (selectedWallpaper != 0 || wallpaperFocus.value != 0)
        winrt::throw_hresult(E_UNEXPECTED);
    note = L"No pictures found. Choose another folder.";
    capture(L"wallpaper-empty");
    note.clear();
    ReplaceWallpapers(bundled);
    connections.wifiAvailable = connections.wifiOn = connections.bluetoothAvailable = connections.bluetoothOn = true;
    WifiNetwork home;
    home.name = L"Fixture secured network with a long visible name";
    home.secure = home.connectable = true;
    home.quality = 92;
    connections.networks = {home};
    flyout = Flyout::Wifi;
    selectedWifi = 0;
    capture(L"wifi-password");
    connections.networks[0].connected = true;
    connections.networks[0].profile = L"Fixture";
    capture(L"wifi");
    connections.wifiMessage = L"Credentials rejected. Try again or choose More settings.";
    capture(L"wifi-error");
    connections.wifiMessage.clear();
    BluetoothEntry device;
    device.name = L"Fixture headphones";
    device.paired = device.connected = true;
    device.native.Address.ullLong = 1;
    connections.devices = {device};
    flyout = Flyout::Bluetooth;
    selectedBluetooth = 0;
    capture(L"bluetooth");
    connections.bluetoothBusy = true;
    connections.bluetoothMessage = L"Searching for devices…";
    capture(L"bluetooth-busy");
    connections.bluetoothBusy = false;
    connections.bluetoothMessage.clear();
    flyout = Flyout::None;
    miniMode = true;
    tab = Tab::Controls;
    currentMedia.available = currentMedia.canPrevious = currentMedia.canPlay = currentMedia.canNext = true;
    currentMedia.playing = true;
    currentMedia.title = L"Fixture music";
    currentMedia.artist = L"Fixture artist";
    currentMedia.position = 42;
    currentMedia.duration = 180;
    capture(L"media-mini");
    miniMode = false;
    fixedAccent = false;
    SetAccent(0x83B8DE);
    capture(L"theme-blue");
    SetAccent(0xD898BC);
    capture(L"theme-pink");
    fixedAccent = true;
    ConfigureAppearance();
    capture(L"theme-light");
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"theme-dark");
    ConfigureAppearance(true);
    capture(L"theme-high-contrast");
    lightAppearance = true;
    ConfigureAppearance();
    tab = Tab::Controls;
    system.volume = 0;
    capture(L"controls-volume-zero");
    system.volume = .01f;
    capture(L"controls-volume-one");
    system.volume = 1;
    capture(L"controls-volume-full");
    system.muted = true;
    capture(L"controls-muted");
    system.muted = false;
    fixtureAudioAvailable = false;
    system.volume = 0;
    system.audioName = L"No audio device";
    capture(L"controls-no-audio");
    fixtureAudioAvailable = true;
    system.audioName = L"Fixture audio output";
    system.volume = .42f;
    // Pairwise edge states use inert data, never the user's clipboard or devices.
    auto savedConnections = connections;
    flyout = Flyout::Wifi;
    selectedWifi = -1;
    capture(L"wifi-unselected");
    connections.networks.clear();
    connections.wifiOn = false;
    connections.wifiMessage = L"Wi-Fi is switched off. Use Actions or More settings.";
    capture(L"wifi-radio-off");
    connections.wifiAvailable = false;
    connections.wifiMessage = L"No adapter available. Use More settings.";
    capture(L"wifi-no-adapter");
    connections.wifiAvailable = true;
    connections.wifiMessage = L"Windows denied access. Choose More settings to review permissions.";
    capture(L"wifi-permission-error");
    connections = savedConnections;
    flyout = Flyout::Bluetooth;
    selectedBluetooth = 0;
    connections.devices[0].paired = connections.devices[0].connected = false;
    capture(L"bluetooth-unpaired");
    connections.bluetoothMessage = L"Pairing canceled. Try again or choose More settings.";
    capture(L"bluetooth-error");
    connections.devices.clear();
    selectedBluetooth = -1;
    connections.bluetoothMessage = L"No results. Try Refresh.";
    capture(L"bluetooth-empty");
    connections.bluetoothOn = false;
    connections.bluetoothMessage = L"Bluetooth is switched off.";
    capture(L"bluetooth-radio-off");
    connections.bluetoothAvailable = false;
    connections.bluetoothMessage = L"No adapter available. Use More settings.";
    capture(L"bluetooth-no-adapter");
    connections = savedConnections;
    flyout = Flyout::None;
    auto savedTasks = tasks;
    tasks.clear();
    tab = Tab::Today;
    capture(L"today-empty");
    tasks = savedTasks;
    auto fixtureWeather = weather;
    auto fixtureCalendar = calendarEvents;
    weather = {};
    weatherCity.clear();
    calendarEvents.clear();
    capture(L"today-offline-empty");
    weatherCity = L"Fixture city";
    weather.message = L"Loading weather…";
    capture(L"today-loading");
    weather.message = L"Weather unavailable. Reopen Today to retry.";
    capture(L"today-error");
    weather = fixtureWeather;
    calendarEvents = fixtureCalendar;
    while (tasks.size() < 20)
        tasks.push_back({L"Scrolling task " + std::to_wstring(tasks.size() + 1), false, nextTask++});
    taskScroll = 10000;
    capture(L"today-20-tasks");
    taskScroll = 0;
    tasks = savedTasks;
    for (float dpiScale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * dpiScale;
        envelopeWidth = std::min(900.0f, 1366 / dpiScale);
        envelopeHeight = std::min(760.0f, 768 / dpiScale);
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
            textScale = text;
            scroll = 0;
            capture(L"today-matrix-" + std::to_wstring(static_cast<int>(dpiScale * 100)) + L"-" +
                    std::to_wstring(static_cast<int>(text * 100)));
        }
    }
    textScale = 1;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"today-dark");
    lightAppearance = true;
    ConfigureAppearance(true);
    capture(L"today-high-contrast");
    ConfigureAppearance();

    tab = Tab::Apps;
    appQuery = L"No matching fixture application";
    FilterApps();
    capture(L"apps-empty");
    appQuery.clear();
    FilterApps();
    tab = Tab::Clipboard;
    auto savedClipboard = clipboard;
    auto savedClipboardState = clipboardState;
    clipboard.clear();
    clipboardState = L"Clipboard empty";
    capture(L"clipboard-empty");
    clipboard = std::wstring(8192, L'x');
    clipboardState = L"Showing first 8,192 UTF-16 units. Scroll or select to read.";
    capture(L"clipboard-long");
    clipboard = savedClipboard;
    clipboardState = savedClipboardState;
    tab = Tab::Wallpaper;
    auto savedWallpapers = wallpapers;
    ReplaceWallpapers({});
    wallpaperLoading = true;
    capture(L"wallpaper-loading");
    wallpaperLoading = false;
    ReplaceWallpapers(savedWallpapers);
    wallpaperApplying = true;
    capture(L"wallpaper-applying");
    wallpaperApplying = false;
    tab = Tab::Controls;
    miniMode = true;
    auto savedMedia = currentMedia;
    currentMedia = MediaState{};
    capture(L"media-unavailable");
    currentMedia = savedMedia;
    currentMedia.duration = 0;
    capture(L"media-live-stream");
    currentMedia.duration = 180;
    currentMedia.playing = false;
    capture(L"media-paused");
    currentMedia.title = L"Long media title with mixed scripts \u65e5\u672c\u8a9e and emoji \U0001F3B5 that remains "
                         L"available in full details";
    capture(L"media-long");
    currentMedia = savedMedia;
    miniMode = false;
    for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * scale;
        envelopeWidth = std::min(900.0f, 1366 / scale);
        envelopeHeight = std::min(760.0f, 768 / scale);
        for (int i = 0; i < 7; ++i) {
            tab = static_cast<Tab>(i);
            scroll = 0;
            note = i == 5 ? L"Fixture error stays below the action footer." : L"";
            capture(std::wstring(L"matrix-") + names[i] + L"-" + std::to_wstring(static_cast<int>(scale * 100)));
        }
        tab = Tab::Today;
        scroll = 10000;
        capture(L"matrix-today-scrolled-" + std::to_wstring(static_cast<int>(scale * 100)));
    }
    dpi = 192;
    envelopeWidth = 683;
    envelopeHeight = 272;
    scroll = 0;
    note.clear();
    tab = Tab::Apps;
    capture(L"browser-apps-200");
    flyout = Flyout::Wifi;
    selectedWifi = 0;
    connections.networks[0].connected = false;
    connections.networks[0].profile.clear();
    capture(L"browser-wifi-200");
    flyout = Flyout::None;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    scroll = 0;
    note.clear();
    tab = Tab::Controls;
    capture(L"controls-final");
    // Input identity must survive order changes without activating the replacement.
    tab = Tab::Apps;
    RebuildLayout();
    auto first = std::find_if(hits.begin(), hits.end(), [](const Hit &h) { return h.id == 300; });
    if (first == hits.end())
        winrt::throw_hresult(E_UNEXPECTED);
    pressedKey = first->key;
    pressedId = 300;
    appQuery = L"Application 108";
    FilterApps();
    RebuildLayout();
    if (!pressedKey.empty())
        winrt::throw_hresult(E_UNEXPECTED);
    appQuery.clear();
    FilterApps();
    for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
        textScale = text;
        dpi = 192;
        envelopeWidth = 683;
        envelopeHeight = 272;
        scroll = 0;
        flyout = Flyout::None;
        note = L"Could not complete this operation. Open Actions and read status to retry.";
        for (int i = 0; i < 7; ++i) {
            tab = static_cast<Tab>(i);
            scroll = 0;
            capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-" + names[i]);
        }
        flyout = Flyout::Wifi;
        selectedWifi = 0;
        scroll = 0;
        capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-wifi");
        flyout = Flyout::None;
        miniMode = true;
        capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-mini");
        miniMode = false;
    }
    textScale = 1;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    note.clear();
    // Hidden native EDIT verification is separate from software pixels.
    tab = Tab::Today;
    renderMode = false;
    previewMode = true;
    dpi = 96;
    scroll = 0;
    RebuildLayout();
    EnsureTaskEdit();
    wchar_t cue[64]{};
    if (!SendMessageW(taskEdit, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(cue), 64) ||
        std::wstring(cue) != L"Add a task…")
        winrt::throw_hresult(E_UNEXPECTED);
    SetWindowTextW(taskEdit, L"V2 native draft — 日本語");
    SendMessageW(taskEdit, EM_SETSEL, 3, 8);
    RECT suggested{};
    GetWindowRect(window, &suggested);
    SendMessageW(window, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&suggested));
    wchar_t draft[121]{};
    GetWindowTextW(taskEdit, draft, 121);
    DWORD start = 0, end = 0;
    SendMessageW(taskEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    if (std::wstring(draft) != L"V2 native draft — 日本語" || start != 3 || end != 8)
        winrt::throw_hresult(E_UNEXPECTED);
    SendMessageW(window, WM_DPICHANGED, MAKELONG(96, 96), reinterpret_cast<LPARAM>(&suggested));
    DestroyWindow(taskEdit);
    taskEdit = nullptr;
    taskDraft.clear();
    renderMode = true;
    previewMode = false;
    TaskStore store((std::filesystem::path(directory) / L"tasks-fixture.ini").wstring());
    std::vector<std::pair<std::wstring, bool>> saved{{L"Mixed scripts 日本語 ✨", false}, {L"Completed", true}};
    if (!store.Save(saved) || store.Load() != saved)
        winrt::throw_hresult(E_UNEXPECTED);
    TaskStore failure((std::filesystem::path(directory) / L"verification.txt" / L"tasks.ini").wstring());
    if (failure.Save(saved))
        winrt::throw_hresult(E_UNEXPECTED);
    report << "PASS native EDIT draft/caret/selection across synthetic DPI transaction; atomic task round trip and "
              "injected save failure."
           << std::endl;
    auto dockCapture = [&](const std::wstring &name, UINT scale, float text, int variant = 0) {
        if (!desktop::DockShell::RenderFixture((std::filesystem::path(directory) / (name + L".png")).wstring(), scale,
                                               text, variant))
            winrt::throw_hresult(E_UNEXPECTED);
        ++captures;
    };
    for (UINT scale : {96U, 120U, 144U, 168U, 192U})
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f})
            dockCapture(L"dock-matrix-" + std::to_wstring(scale * 100 / 96) + L"-" +
                            std::to_wstring(static_cast<int>(text * 100)),
                        scale, text);
    for (UINT scale : {96U, 120U, 144U, 168U, 192U})
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f})
            dockCapture(L"preview-matrix-" + std::to_wstring(scale * 100 / 96) + L"-" +
                            std::to_wstring(static_cast<int>(text * 100)),
                        scale, text, 3);
    dockCapture(L"preview", 96, 1, 3);
    dockCapture(L"preview-unavailable", 96, 1, 4);
    dockCapture(L"dock", 96, 1);
    dockCapture(L"dock-empty", 96, 1, 1);
    dockCapture(L"dock-overflow", 96, 1, 2);
    lightAppearance = false;
    ConfigureAppearance();
    dockCapture(L"dock-dark", 96, 1);
    dockCapture(L"preview-dark", 96, 1, 3);
    lightAppearance = true;
    ConfigureAppearance(true);
    dockCapture(L"dock-high-contrast", 96, 1, 7);
    dockCapture(L"preview-high-contrast", 96, 1, 3);
    ConfigureAppearance();
    auto previousWindows = runningWindows;
    for (int i = 0; i < 5; ++i) {
        desktop::WindowEntry entry;
        entry.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(100 + i));
        entry.title = L"Fixture running document " + std::to_wstring(i + 1);
        entry.identity = L"fixture-running:" + std::to_wstring(i);
        entry.active = i == 0;
        runningWindows.push_back(entry);
    }
    FilterRunning();
    tab = Tab::Apps;
    scroll = 0;
    capture(L"apps-running");
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"apps-running-dark");
    lightAppearance = true;
    ConfigureAppearance(true);
    capture(L"apps-running-high-contrast");
    ConfigureAppearance();
    for (float dpiScale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * dpiScale;
        envelopeWidth = std::min(900.0f, 1366 / dpiScale);
        envelopeHeight = std::min(760.0f, 768 / dpiScale);
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
            textScale = text;
            capture(L"apps-running-matrix-" + std::to_wstring(static_cast<int>(dpiScale * 100)) + L"-" +
                    std::to_wstring(static_cast<int>(text * 100)));
        }
    }
    runningWindows = std::move(previousWindows);
    FilterRunning();
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    scroll = 0;
    flyout = Flyout::None;
    tab = Tab::Controls;
    controlState.brightnessSupported = false;
    capture(L"v32-controls-unsupported");
    controlState.radiosAvailable = false;
    capture(L"v32-controls-no-radios");
    controlState.radiosAvailable = true;
    controlState.brightnessSupported = true;
    controlState.dark = true;
    capture(L"v32-controls-dark");
    controlState.dark = false;
    controlState.radiosOff = true;
    capture(L"v32-airplane-off");
    controlState.radiosOff = false;
    note = L"Windows denied radio control. Open Radio settings.";
    capture(L"v32-airplane-denied");
    note.clear();
    flyout = Flyout::Audio;
    capture(L"v32-outputs");
    auto savedOutputs = controlState.outputs;
    controlState.outputs.clear();
    capture(L"v32-outputs-empty");
    controlState.outputs = savedOutputs;
    flyout = Flyout::None;
    tab = Tab::Stats;
    statsMemory = true;
    capture(L"v32-stats-memory");
    statsMemory = false;
    auto savedHardware = hardwareState;
    hardwareState.history = {};
    hardwareState.top.clear();
    hardwareState.volumes.clear();
    capture(L"v32-stats-starting");
    hardwareState = savedHardware;
    tab = Tab::Wallpaper;
    wallpaperSource = 2;
    capture(L"v32-wallpaper-current");
    wallpaperSource = 0;
    wallpaperTarget = wallpaperDisplays.back().id;
    wallpaperFit = DWPOS_FIT;
    capture(L"v32-wallpaper-display-fit");
    for (int mode = 0; mode < 2; ++mode) {
        if (mode)
            ConfigureAppearance(true);
        else {
            lightAppearance = false;
            ConfigureAppearance();
        }
        for (int view = 0; view < 4; ++view) {
            tab = view == 0 || view == 3 ? Tab::Controls : view == 1 ? Tab::Stats : Tab::Wallpaper;
            flyout = view == 3 ? Flyout::Audio : Flyout::None;
            scroll = 0;
            capture(L"v32-theme-" + std::to_wstring(mode) + L"-" + std::to_wstring(view));
        }
    }
    lightAppearance = true;
    ConfigureAppearance();
    for (int scale : {100, 125, 150, 175, 200})
        for (int text : {125, 150, 200, 225}) {
            dpi = 96 * scale / 100.0f;
            textScale = text / 100.0f;
            envelopeWidth = 1366 * 96 / dpi;
            envelopeHeight = 768 * 96 / dpi;
            for (int view = 0; view < 4; ++view) {
                tab = view == 0 || view == 3 ? Tab::Controls : view == 1 ? Tab::Stats : Tab::Wallpaper;
                flyout = view == 3 ? Flyout::Audio : Flyout::None;
                scroll = 0;
                capture(L"v32-matrix-" + std::to_wstring(view) + L"-" + std::to_wstring(scale) + L"-" +
                        std::to_wstring(text));
            }
        }
    flyout = Flyout::None;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    for (int scale : {100, 125, 150, 175, 200})
        for (int text : {125, 150, 200, 225}) {
            UINT actualDpi = static_cast<UINT>(96 * scale / 100);
            float factor = text / 100.0f;
            auto suffix = std::to_wstring(scale) + L"-" + std::to_wstring(text);
            dockCapture(L"dock-magnified-" + suffix, actualDpi, factor, 5);
            dockCapture(L"dock-overflow-" + suffix, actualDpi, factor, 2);
            dockCapture(L"dock-reduced-" + suffix, actualDpi, factor, 6);
            ConfigureAppearance(true);
            dockCapture(L"dock-contrast-" + suffix, actualDpi, factor, 7);
            ConfigureAppearance();
            dpi = static_cast<float>(actualDpi);
            textScale = factor;
            envelopeWidth = 1366 * 96 / dpi;
            envelopeHeight = 768 * 96 / dpi;
            flyout = Flyout::Palette;
            paletteQuery.clear();
            BuildPalette();
            capture(L"palette-matrix-" + suffix);
            paletteQuery = L"No such fixture command";
            BuildPalette();
            capture(L"palette-empty-" + suffix);
            flyout = Flyout::DockStatus;
            capture(L"status-matrix-" + suffix);
            flyout = Flyout::Recent;
            recentDocuments = {{L"Fixture report.pdf", L"fixture:report", L"", L""},
                               {L"日本語 document", L"fixture:document", L"", L""}};
            capture(L"recent-matrix-" + suffix);
            recentDocuments.clear();
            recentMessage = L"No public recent documents. Custom Jump List tasks are unavailable.";
            capture(L"recent-empty-" + suffix);
            flyout = Flyout::Features;
            scroll = 0;
            for (int page = 0; page < 4; ++page) {
                featuresPage = page;
                if (page == 2) {
                    privacySnapshot = {true, 0, 3, {}, {{L"dock", L"window-event-hook", L"running-window-events"}}};
                    privacy::Event request;
                    request.owner = L"today";
                    request.capability = L"network";
                    request.target = L"fixture.invalid";
                    request.reason = L"calendar-subscription";
                    request.result = privacy::Result::Blocked;
                    privacySnapshot.events = {request};
                }
                capture(L"features-" + std::to_wstring(page) + L"-" + suffix);
            }
            moduleRegistry.ApplyPreset(modules::Preset::Taskbar);
            featuresPage = 0;
            capture(L"features-enabled-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
            BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().moduleAction != L"windows.display")
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"features-palette-" + suffix);
            flyout = Flyout::Module;
            moduleView = L"windows";
            modulePage = 0;
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"module-view-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = L"ms-settings:privacy-webcam";
            BuildPalette();
            if (paletteItems.size() != 1 || !paletteItems.front().moduleAction.starts_with(L"windows.page-result-"))
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-link-palette-" + suffix);
            flyout = Flyout::Module;
            moduleQuery = paletteQuery;
            modulePanel = *moduleRegistry.View(moduleView, moduleQuery);
            capture(L"settings-link-island-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = L"ms-settings:defaultapps?registeredAUMID=Example!App"; BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().label.find(L"Example%21App") == std::wstring::npos)
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-app-parameter-" + suffix);
            paletteQuery = L"ms-settings:sound-properties?endpointId={0.0.0.00000000}.{aaaaaaaa-0000-1111-2222-bbbbbbbbbbbb}";
            BuildPalette();
            if (paletteItems.size() != 1) winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-device-parameter-" + suffix);
            moduleQuery.clear();
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_COMMAND_RESTART);
            BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().action != RestartNow) winrt::throw_hresult(E_UNEXPECTED);
            capture(L"system-commands-" + suffix);
            paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
            moduleRegistry.ApplyPreset(modules::Preset::Minimal);
            BuildPalette();
            if (!paletteItems.empty()) winrt::throw_hresult(E_UNEXPECTED);
            flyout = Flyout::Module;
            for (const auto *id : {L"utilities", L"clipboard", L"snippets", L"keep-awake", L"files", L"shelf", L"recycle-bin", L"drives"}) {
                moduleRegistry.Grant(id);
                if (!moduleRegistry.Enable(id)) winrt::throw_hresult(E_UNEXPECTED);
            }
            moduleRegistry.Invoke(L"utilities.calculate", window, L"2*(3+4)");
            moduleRegistry.TakeView();
            moduleView = L"utilities";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"utilities-" + suffix);
            moduleView = L"clipboard";
            moduleRegistry.Invoke(L"clipboard.open", window);
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"clipboard-history-" + suffix);
            for (const auto &action : moduleRegistry.Actions())
                if (action.id.starts_with(L"clipboard.preview-") && action.text.find(modules::String(IDS_CLIP_IMAGE)) != std::wstring::npos) {
                    moduleRegistry.Invoke(action.id, window);
                    break;
                }
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"clipboard-image-" + suffix);
            moduleRegistry.Invoke(L"snippets.add", window, L"Fixture signature | Regards,\\nDako");
            moduleRegistry.TakeView();
            moduleView = L"snippets";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"snippets-" + suffix);
            moduleView = L"keep-awake";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"keep-awake-" + suffix);
            moduleRegistry.Invoke(L"keep-awake.rule-apps", window, L"editor.exe; player.exe");
            modulePanel = *moduleRegistry.View(moduleView, L"");
            modulePage = 7 / ModuleSlots();
            capture(L"keep-awake-rules-" + suffix);
            modulePage = 10 / ModuleSlots();
            capture(L"keep-awake-rule-apps-" + suffix);
            modulePage = 0;
            moduleView = L"recycle-bin";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"recycle-bin-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_RECYCLE_EMPTY); BuildPalette();
            if (paletteItems.empty() || paletteItems.front().moduleAction != L"recycle-bin.empty") winrt::throw_hresult(E_UNEXPECTED);
            capture(L"recycle-bin-palette-" + suffix);
            flyout = Flyout::Module;
            moduleView = L"drives";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"removable-drives-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_DRIVE_EJECT); BuildPalette();
            if (paletteItems.empty() || !paletteItems.front().moduleAction.starts_with(L"drives.eject-")) winrt::throw_hresult(E_UNEXPECTED);
            capture(L"drive-eject-palette-" + suffix);
            paletteQuery.clear(); flyout = Flyout::Module;
            moduleView = L"shelf";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"shelf-" + suffix);
            for (const auto &action : moduleRegistry.Actions()) if (action.id.starts_with(L"shelf.preview-")) {
                moduleRegistry.Invoke(action.id, window); break;
            }
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"shelf-item-" + suffix);
            moduleView = L"files";
            modulePanel = *moduleRegistry.View(moduleView, L"report");
            capture(L"indexed-files-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_FILES_KEYWORD) + L" report";
            BuildPalette();
            if (paletteItems.empty() || !paletteItems.front().moduleAction.starts_with(L"files.result-"))
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"indexed-palette-" + suffix);
            paletteQuery.clear();
            moduleRegistry.ApplyPreset(modules::Preset::Minimal);
            modulePanel = {};
        }
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    flyout = Flyout::Features;
    for (int appearance = 0; appearance < 2; ++appearance) {
        lightAppearance = false;
        ConfigureAppearance(appearance == 1);
        for (int page = 0; page < 4; ++page) {
            featuresPage = page;
            capture(L"features-theme-" + std::to_wstring(appearance) + L"-" + std::to_wstring(page));
        }
    }
    ConfigureAppearance();
    flyout = Flyout::None;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    report << "PASS captures=" << captures
           << "; geometry, stable identity, catalog replacement, transformed scene and 5 DPI tiers. Software fixtures; "
              "no desktop/hardware sign-off."
           << std::endl;
    tab = Tab::Controls;
    SetExpanded(false);
    SaveRender(directory + L"\\compact.png");
    verification.Complete(static_cast<bool>(report));
}

#include "DesktopIntegration.h"
#include "PaletteView.h"
#include "FeaturesView.h"
#include "ModuleView.h"
#include "ControlIntegration.h"
#include "ControlsView.h"
#include "TodayView.h"
#include "AppsView.h"
#include "StatsView.h"
#include "WallpaperView.h"

// Standard MSAA children let Windows' UI Automation bridge expose the custom-drawn
// controls as named, focusable buttons and a slider instead of an inaccessible canvas.
class IslandAccessible final : public IAccessible, public IEnumVARIANT {
    ULONG cursor_ = 0;
    std::atomic<ULONG> references_{1};
    HWND window_;
    Island *App() const {
        wchar_t className[64]{};
        if (!IsWindow(window_) || !GetClassNameW(window_, className, 64) || wcscmp(className, WindowClass))
            return nullptr;
        return reinterpret_cast<Island *>(GetWindowLongPtrW(window_, GWLP_USERDATA));
    }
    int Child(VARIANT value, Island *app) const {
        return app && value.vt == VT_I4 && value.lVal > 0 ? app->IndexForChild(value.lVal) : -1;
    }
    HRESULT String(BSTR *output, const std::wstring &value) const {
        if (!output)
            return E_POINTER;
        *output = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
        return *output ? S_OK : E_OUTOFMEMORY;
    }

  public:
    explicit IslandAccessible(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
        if (!output)
            return E_POINTER;
        if (id == IID_IUnknown || id == IID_IDispatch || id == IID_IAccessible) {
            *output = static_cast<IAccessible *>(this);
            AddRef();
            return S_OK;
        }
        if (id == IID_IEnumVARIANT) {
            *output = static_cast<IEnumVARIANT *>(this);
            AddRef();
            return S_OK;
        }
        *output = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, VARIANT *values, ULONG *fetched) override {
        if (!values || (!fetched && count != 1))
            return E_POINTER;
        auto *app = App();
        ULONG n = 0;
        while (app && n < count && cursor_ < app->hits.size()) {
            VariantInit(&values[n]);
            values[n].vt = VT_I4;
            values[n].lVal = app->hits[cursor_++].child;
            ++n;
        }
        if (fetched)
            *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        auto *app = App();
        ULONG size = app ? static_cast<ULONG>(app->hits.size()) : 0, next = std::min(size, cursor_ + count);
        bool full = next - cursor_ == count;
        cursor_ = next;
        return full ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        cursor_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT **output) override {
        if (!output)
            return E_POINTER;
        auto *copy = new IslandAccessible(window_);
        copy->cursor_ = cursor_;
        *output = static_cast<IEnumVARIANT *>(copy);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override {
        if (!count)
            return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *,
                                     UINT *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **parent) override {
        if (!parent)
            return E_POINTER;
        *parent = nullptr;
        return CreateStdAccessibleObject(window_, OBJID_WINDOW, IID_IDispatch, reinterpret_cast<void **>(parent));
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG *count) override {
        if (!count)
            return E_POINTER;
        auto *app = App();
        *count = app ? static_cast<LONG>(app->hits.size()) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT value, IDispatch **child) override {
        if (!child)
            return E_POINTER;
        *child = nullptr;
        auto *app = App();
        int i = Child(value, app);
        if (i >= 0 && app->hits[i].input)
            return AccessibleObjectFromWindow(app->hits[i].input, static_cast<DWORD>(OBJID_CLIENT), IID_IDispatch,
                                              reinterpret_cast<void **>(child));
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR *name) override {
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0)
            return String(name, app->hits[index].name);
        if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
            return String(name, L"Dako desktop island");
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR *value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0 && app->hits[index].id == Volume)
            return String(value, Number(app->system.volume * 100) + L" percent");
        if (index >= 0 && app->hits[index].id == Brightness)
            return String(value, std::to_wstring(app->controlState.brightness) + L" percent");
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR *description) override {
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0 && app->hits[index].id == Volume)
            return String(description, L"System volume, minimum 0, maximum 100 percent. Left and right arrows adjust "
                                       L"by two percent. Space mutes.");
        if (index >= 0 && app->hits[index].id == Brightness)
            return String(description, L"Selected display brightness. Arrow keys change five percent. Home or End "
                                       L"selects the minimum or maximum.");
        if (index >= 0)
            return String(description,
                          L"F2 reads full details. Tab moves between controls. Page Down/Up scrolls content.");
        return String(description,
                      (app ? app->note : L"") + L" Use Tab to move between controls, Enter or Space to activate, and "
                                                L"Escape to collapse. Ctrl+Page Down/Up changes views.");
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT *role) override {
        if (!role)
            return E_POINTER;
        VariantInit(role);
        role->vt = VT_I4;
        auto *app = App();
        int index = Child(child, app);
        int id = index >= 0 ? app->hits[index].id : 0;
        role->lVal = index < 0                                   ? ROLE_SYSTEM_PANE
                     : ReadingControl(id) ? ROLE_SYSTEM_STATICTEXT
                     : (id >= 100 && id < 107) || (id >= 2901 && id <= 2904) ? ROLE_SYSTEM_PAGETAB
                     : (id == Volume || id == Brightness)        ? ROLE_SYSTEM_SLIDER
                     : (id >= 200 && id < 220) || (id >= 900 && id < 1000) || id == FlyoutToggle || id == Dark ||
                             id == Airplane || (id >= 2800 && id < 2807) || (id >= 2920 && id < 2940) || id == 2941
                         ? ROLE_SYSTEM_CHECKBUTTON
                     : id >= 400 && id <= 403 ? ROLE_SYSTEM_TEXT
                     : (id >= 500 && id < 564) || (id >= 2400 && id < 2402) || (id >= 2600 && id < 2620) ||
                             (id >= 2700 && id < 2720) || (id >= 300 && id < 400) || (id >= 1000 && id < 1328)
                         ? ROLE_SYSTEM_LISTITEM
                         : ROLE_SYSTEM_PUSHBUTTON;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT *state) override {
        if (!state)
            return E_POINTER;
        VariantInit(state);
        state->vt = VT_I4;
        state->lVal = 0;
        auto *app = App();
        int index = Child(child, app);
        if (index >= 0) {
            int hitId = app->hits[index].id;
            bool reading = ReadingControl(hitId);
            state->lVal = reading ? STATE_SYSTEM_READONLY : STATE_SYSTEM_FOCUSABLE;
            if (!app->VisibleHit(app->hits[index]) ||
                (!app->Actionable() && !(app->hits[index].id == Expand && !app->expanded && !app->YieldResting())))
                state->lVal |= STATE_SYSTEM_OFFSCREEN;
            if (!app->hits[index].enabled && !reading)
                state->lVal |= STATE_SYSTEM_UNAVAILABLE;
            if (app->focused == index && GetFocus() == window_)
                state->lVal |= STATE_SYSTEM_FOCUSED;
            int id = app->hits[index].id;
            if (id == 2941 && app->privacySnapshot.offline) state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 2901 && id <= 2904 && id - 2901 == app->featuresPage)
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 2920 && id < 2940) {
                auto descriptors = app->moduleRegistry.Descriptors();
                size_t module = static_cast<size_t>(id - 2920);
                if (module < descriptors.size() && app->moduleRegistry.Enabled(descriptors[module]->id))
                    state->lVal |= STATE_SYSTEM_CHECKED;
            }
            if (id >= 100 && id < 107 && id - 100 == static_cast<int>(app->tab))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 200 && id < 220 && id - 200 < static_cast<int>(app->tasks.size()) && app->tasks[id - 200].done)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 900 && id < 1000 && app->AppIndex(id - 900) < app->apps.size() &&
                app->Favorite(app->apps[app->AppIndex(id - 900)]))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 2800 && id < 2805 && (app->dockOptions.statusMask & (1U << (id - 2800))))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == 2805 && app->dockOptions.showDesktop)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == 2806 && app->dockOptions.showTaskView)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == Dark && app->controlState.dark)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == Airplane && app->controlState.radiosOff)
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id == FlyoutToggle &&
                (app->flyout == Flyout::Wifi ? app->connections.wifiOn : app->connections.bluetoothOn))
                state->lVal |= STATE_SYSTEM_CHECKED;
            if (id >= 500 && id < 564 && id - 500 == static_cast<int>(app->selectedWallpaper))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id >= 300 && id < 400 && app->AppIndex(id - 300) < app->apps.size() &&
                app->AppKey(app->apps[app->AppIndex(id - 300)]) == app->selectedAppKey)
                state->lVal |= STATE_SYSTEM_SELECTED;
            if ((id >= 1000 && id < 1128 && id - 1000 == app->selectedWifi) ||
                (id >= 1200 && id < 1328 && id - 1200 == app->selectedBluetooth))
                state->lVal |= STATE_SYSTEM_SELECTED;
            if (id == 402)
                state->lVal |= STATE_SYSTEM_PROTECTED;
            if (id == 403)
                state->lVal |= STATE_SYSTEM_READONLY;
            if ((id == WallpaperApply && app->wallpaperApplying) ||
                (id == FlyoutConnect && app->connections.wifiConnecting))
                state->lVal |= STATE_SYSTEM_BUSY;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR *help) override {
        if (!help)
            return E_POINTER;
        *help = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *file, VARIANT, LONG *topic) override {
        if (file)
            *file = nullptr;
        if (topic)
            *topic = -1;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR *shortcut) override {
        if (!shortcut)
            return E_POINTER;
        *shortcut = nullptr;
        if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
            return String(shortcut, L"Ctrl+Alt+Space");
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *focus) override {
        if (!focus)
            return E_POINTER;
        VariantInit(focus);
        auto *app = App();
        if (GetFocus() == window_) {
            focus->vt = VT_I4;
            focus->lVal = app && app->focused >= 0 ? app->hits[app->focused].child : CHILDID_SELF;
        } else if (app)
            for (const auto &hit : app->hits)
                if (hit.input && hit.input == GetFocus()) {
                    focus->vt = VT_I4;
                    focus->lVal = hit.child;
                    break;
                }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *selection) override {
        if (!selection)
            return E_POINTER;
        VariantInit(selection);
        auto *app = App();
        if (!app)
            return S_FALSE;
        int id = app->flyout == Flyout::Wifi        ? 1000 + app->selectedWifi
                 : app->flyout == Flyout::Bluetooth ? 1200 + app->selectedBluetooth
                 : app->tab == Tab::Wallpaper       ? 500 + static_cast<int>(app->selectedWallpaper)
                                                    : 100 + static_cast<int>(app->tab);
        if (app->tab == Tab::Apps && app->flyout == Flyout::None)
            for (const auto &hit : app->hits)
                if (hit.id >= 300 && hit.id < 400 && app->AppIndex(hit.id - 300) < app->apps.size() &&
                    app->AppKey(app->apps[app->AppIndex(hit.id - 300)]) == app->selectedAppKey) {
                    id = hit.id;
                    break;
                }
        for (const auto &hit : app->hits)
            if (hit.id == id) {
                selection->vt = VT_I4;
                selection->lVal = hit.child;
                return S_OK;
            }
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR *action) override {
        if (!action)
            return E_POINTER;
        *action = nullptr;
        auto *app = App();
        int index = Child(child, app);
        return index >= 0 && app->hits[index].enabled
                   ? String(action, app->hits[index].id == Volume ? L"Adjust" : L"Press")
                   : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags, VARIANT child) override {
        auto *app = App();
        int index = Child(child, app);
        if (index < 0 || !app->hits[index].enabled || !(flags & SELFLAG_TAKEFOCUS))
            return E_INVALIDARG;
        PostMessageW(window_, FocusControl, app->hits[index].child, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(LONG *left, LONG *top, LONG *width, LONG *height, VARIANT child) override {
        if (!left || !top || !width || !height)
            return E_POINTER;
        auto *app = App();
        int index = Child(child, app);
        RECT bounds{};
        GetWindowRect(window_, &bounds);
        if (index >= 0) {
            auto r = app->hits[index].rect;
            const auto &h = app->hits[index];
            float extra = std::abs(h.skew) * (r.bottom - r.top) / 2;
            r.left -= extra;
            r.right += extra;
            auto clipped = layout::Intersection({r.left, r.top, r.right, r.bottom},
                                                {h.clip.left, h.clip.top, h.clip.right, h.clip.bottom});
            r = Island::R(clipped);
            float scale = app->dpi / 96.0f, logicalScale = app->expanded ? app->textScale : 1.0f;
            float originX = app->expanded     ? app->PanelX()
                            : app->renderMode ? 0
                                              : (app->envelopeWidth - theme::DockWidth) / 2;
            float originY = app->expanded ? app->PanelY() : app->HandleY();
            *left = bounds.left + static_cast<LONG>((originX + r.left * logicalScale) * scale);
            *top = bounds.top + static_cast<LONG>((originY + r.top * logicalScale) * scale);
            *width = static_cast<LONG>((r.right - r.left) * logicalScale * scale);
            *height = static_cast<LONG>((r.bottom - r.top) * logicalScale * scale);
        } else {
            *left = bounds.left;
            *top = bounds.top;
            *width = bounds.right - bounds.left;
            *height = bounds.bottom - bounds.top;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction, VARIANT start, VARIANT *end) override {
        if (!end)
            return E_POINTER;
        VariantInit(end);
        auto *app = App();
        if (!app || app->hits.empty())
            return S_FALSE;
        int index = Child(start, app), destination = -1;
        if (direction == NAVDIR_FIRSTCHILD)
            destination = 0;
        else if (direction == NAVDIR_LASTCHILD)
            destination = static_cast<int>(app->hits.size()) - 1;
        else if (direction == NAVDIR_NEXT && index >= 0 && index + 1 < static_cast<int>(app->hits.size()))
            destination = index + 1;
        else if (direction == NAVDIR_PREVIOUS && index > 0)
            destination = index - 1;
        if (destination < 0)
            return S_FALSE;
        end->vt = VT_I4;
        end->lVal = app->hits[destination].child;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x, LONG y, VARIANT *child) override {
        if (!child)
            return E_POINTER;
        VariantInit(child);
        auto *app = App();
        if (!app)
            return S_FALSE;
        POINT point{x, y};
        ScreenToClient(window_, &point);
        int index = app->HitAt(point.x * 96.0f / app->dpi, point.y * 96.0f / app->dpi, true);
        child->vt = VT_I4;
        child->lVal = index >= 0 ? app->hits[index].child : CHILDID_SELF;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto *app = App();
        int index = Child(child, app);
        if (index < 0 || !app->hits[index].enabled || !app->CanInvoke(app->hits[index]))
            return E_INVALIDARG;
        PostMessageW(window_, InvokeControl, app->hits[index].child, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
};

void VerifyAccessibility(Island &app) {
    app.accessible.Attach(new IslandAccessible(app.window));
    app.tab = Tab::Controls;
    app.SetExpanded(true);
    app.RebuildLayout();
    auto verify = [&](int action, LONG role, LONG required) {
        auto found = std::find_if(app.hits.begin(), app.hits.end(), [&](const Hit &h) { return h.id == action; });
        if (found == app.hits.end())
            winrt::throw_hresult(E_UNEXPECTED);
        VARIANT child{}, actual{}, state{};
        child.vt = VT_I4;
        child.lVal = found->child;
        Check(app.accessible->get_accRole(child, &actual));
        Check(app.accessible->get_accState(child, &state));
        if (actual.lVal != role || (state.lVal & required) != required)
            winrt::throw_hresult(E_UNEXPECTED);
    };
    verify(100, ROLE_SYSTEM_PAGETAB, STATE_SYSTEM_SELECTED);
    verify(Volume, ROLE_SYSTEM_SLIDER, STATE_SYSTEM_FOCUSABLE);
    app.tab = Tab::Today;
    app.RebuildLayout();
    verify(2001, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2002, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2004, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2005, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2123, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(TodayOptions, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    verify(201, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    verify(400, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE);
    app.tab = Tab::Stats;
    app.RebuildLayout();
    verify(2020, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    verify(2024, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    app.tab = Tab::Apps;
    app.RebuildLayout();
    verify(900, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    auto savedRunning = app.runningWindows;
    desktop::WindowEntry fixture;
    fixture.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(90001));
    fixture.title = L"Fixture accessible running app";
    fixture.identity = L"fixture-accessible";
    app.runningWindows = {fixture};
    app.FilterRunning();
    app.RebuildLayout();
    verify(2400, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.runningWindows = std::move(savedRunning);
    app.FilterRunning();
    app.RebuildLayout();
    app.flyout = Flyout::Palette;
    app.paletteQuery = L"Open Today";
    app.BuildPalette();
    app.RebuildLayout();
    verify(401, ROLE_SYSTEM_TEXT, STATE_SYSTEM_FOCUSABLE);
    verify(2600, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::DockStatus;
    app.dockOptions.statusMask = 31;
    app.RebuildLayout();
    verify(2800, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_CHECKED);
    verify(2850, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::Recent;
    app.recentDocuments = {{L"Fixture document", L"fixture:document", L"", L""}};
    app.RebuildLayout();
    verify(2700, ROLE_SYSTEM_LISTITEM, STATE_SYSTEM_FOCUSABLE);
    app.flyout = Flyout::Features;
    app.featuresPage = 0;
    app.scroll = 0;
    app.RebuildLayout();
    verify(2901, ROLE_SYSTEM_PAGETAB, STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTED);
    verify(2920, ROLE_SYSTEM_CHECKBUTTON, STATE_SYSTEM_FOCUSABLE);
    verify(6000, ROLE_SYSTEM_STATICTEXT, STATE_SYSTEM_READONLY);
    app.featuresPage = 1;
    app.RebuildLayout();
    verify(2910, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.moduleRegistry.ApplyPreset(modules::Preset::Taskbar);
    app.flyout = Flyout::Module;
    app.moduleView = L"windows";
    app.modulePage = 0;
    app.modulePanel = *app.moduleRegistry.View(L"windows", L"");
    app.RebuildLayout();
    verify(5100, ROLE_SYSTEM_PUSHBUTTON, STATE_SYSTEM_FOCUSABLE);
    app.moduleRegistry.ApplyPreset(modules::Preset::Minimal);
    app.flyout = Flyout::None;
    app.RebuildLayout();
    ComPtr<IEnumVARIANT> enumeration;
    Check(app.accessible.As(&enumeration));
    ULONG fetched = 0;
    VARIANT value{};
    Check(enumeration->Next(1, &value, &fetched));
    if (fetched != 1 || app.IndexForChild(value.lVal) < 0)
        winrt::throw_hresult(E_UNEXPECTED);
    app.SetExpanded(false);
    app.RebuildLayout();
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *app = reinterpret_cast<Island *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
        app = static_cast<Island *>(create->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == app->shell.taskbarCreated && !app->renderMode) {
            app->shell.ExplorerRestarted();
            if (app->docks)
                app->docks->ExplorerRestarted();
            app->UpdateDocks();
            app->RefreshGeometry();
            return 0;
        }
        if (message == app->shellHookMessage) {
            if (app->docks && wparam == HSHELL_FLASH)
                app->docks->Flash(reinterpret_cast<HWND>(lparam));
            if (app->windowList)
                app->windowList->Request();
            return 0;
        }
        switch (message) {
        case WM_APP + 72:
            app->InvokeModuleAction(L"clipboard.open");
            return 0;
        case modules::Changed:
            if (auto reply = app->clipboardReadTask.Consume()) {
                privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(app->clipboardReadToken));
                app->clipboardReadToken = 0;
                if (app->expanded && app->tab == Tab::Clipboard && app->flyout == Flyout::None) {
                    app->clipboard = ::clipboard::PayloadText(reply->payload).substr(0, 8192);
                    if (!app->clipboard.empty() && app->clipboard.back() >= 0xD800 && app->clipboard.back() <= 0xDBFF)
                        app->clipboard.pop_back();
                    app->clipboardState = modules::String(reply->status == ::clipboard::TransferStatus::Excluded ?
                        IDS_CLIP_EXCLUDED : reply->status == ::clipboard::TransferStatus::Ok ?
                        IDS_CLIP_PREVIEW_READY : IDS_CLIP_UNSUPPORTED);
                    if (app->clipboardEdit) SetWindowTextW(app->clipboardEdit, app->clipboard.c_str());
                }
            }
            app->moduleRegistry.Poll();
            if (!app->expanded) {
                app->modulePanel = {};
                app->paletteItems.clear();
                app->commandCatalog.Clear();
                app->RebuildLayout();
                return 0;
            }
            if (app->flyout == Flyout::Palette) app->BuildPalette();
            else {
                app->paletteItems.clear();
                app->commandCatalog.Clear();
            }
            if (app->flyout != Flyout::Module) app->modulePanel = {};
            if (app->flyout == Flyout::Module) {
                auto panel = app->moduleRegistry.View(app->moduleView, app->moduleQuery);
                if (panel) {
                    app->modulePanel = std::move(*panel);
                    if (app->modulePage * app->ModuleSlots() >= app->modulePanel.rows.size()) app->modulePage = 0;
                } else app->OpenFeatures();
            }
            if (app->expanded) {
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case privacy::Changed:
            privacy::Policy().Acknowledge();
            if (app->expanded && app->flyout == Flyout::Features && app->featuresPage == 2) {
                app->privacySnapshot = privacy::Policy().Read();
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_CREATE:
            app->Initialize();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            // An opening panel already owns native input focus, even before its first spring tick.
            if (!app->renderMode && !app->expanded && app->dockReveal.value == 0) {
                app->PaintNotch(paint.hdc);
                app->hits.clear();
                app->AddHit(Expand, {0, 0, theme::DockWidth, theme::DockHeight}, L"Reveal dock");
                EndPaint(window, &paint);
                return 0;
            }
            try {
                app->Draw();
            } catch (...) {
                EndPaint(window, &paint);
                throw;
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == OBJID_CLIENT) {
                if (!app->accessible)
                    app->accessible.Attach(new IslandAccessible(window));
                return LresultFromObject(IID_IAccessible, wparam, app->accessible.Get());
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (app->composition.Active() && FAILED(app->composition.Resize(LOWORD(lparam), HIWORD(lparam), app->dpi)))
                app->ReleaseGraphics();
            if (app->hostTarget)
                app->hostTarget->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            app->dpi = static_cast<float>(HIWORD(wparam));
            app->ReleaseGraphics();
            app->wallpaperReveal.Cancel();
            if (app->editFont) {
                DeleteObject(app->editFont);
                app->editFont = nullptr;
            }
            app->EnsureEditResources();
            for (HWND input : {app->taskEdit, app->appSearch, app->wifiPassword, app->clipboardEdit})
                if (input)
                    SendMessageW(input, WM_SETFONT, reinterpret_cast<WPARAM>(app->editFont), TRUE);
            // Keep EDIT HWNDs: draft, selection, caret, and IME state survive this transaction.
            if (app->wifiPassword)
                SetWindowTextW(app->wifiPassword, L"");
            const RECT *suggested = reinterpret_cast<RECT *>(lparam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            app->RefreshGeometry();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            app->RefreshControls();
            app->refreshMonitor = nullptr;
            if (app->docks)
                app->docks->RefreshMonitors();
            if (app->windowList)
                app->windowList->Request();
            app->RefreshGeometry();
            return 0;
        case desktop::WindowEventMessage:
            if (app->windowList)
                app->windowList->Request();
            app->UpdateContext();
            return 0;
        case desktop::WindowListChanged:
            if (app->windowList)
                app->runningWindows = app->windowList->Snapshot();
            app->FilterRunning();
            app->UpdateDocks();
            if (app->flyout == Flyout::Palette)
                app->BuildPalette();
            if (app->expanded && app->tab == Tab::Apps) {
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case desktop::DockChanged:
            if (wparam == 1 && app->expanded)
                app->SetExpanded(false);
            if (app->docks)
                app->RefreshGeometry();
            return 0;
        case WM_DWMCOMPOSITIONCHANGED:
            app->ReleaseGraphics();
            app->ConfigureMaterial();
            app->RefreshGeometry();
            return 0;
        case NotchHotZone::PointerNear:
            if (wparam) {
                app->SetExpanded(true);
                return 0;
            }
            app->lastProximity = steady_clock::now();
            SetCoalescableTimer(window, ProximityTimer, 100, nullptr, 50);
            return 0;
        case NotchHotZone::ContextChanged:
            app->UpdateContext();
            app->RefreshGeometry();
            return 0;
        case today::Changed:
            if (app->weatherService)
                app->weather = app->weatherService->Snapshot();
            if (app->calendarService) {
                auto state = app->calendarService->Snapshot();
                app->subscriptionIcs = std::move(state.content);
                app->calendarMessage = std::move(state.message);
                app->RebuildCalendar();
            }
            app->UpdateDocks();
            if (app->expanded && app->tab == Tab::Today) {
                app->RefreshToday();
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_SETTINGCHANGE: {
            app->RefreshControls();
            if (app->catalog) {
                app->catalog->SettingsChanged();
                if (app->expanded && app->tab == Tab::Wallpaper)
                    app->catalog->DiscoverDisplays();
            }
            BOOL enabled = TRUE;
            SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
            app->systemReduced = !enabled;
            app->reducedMotion = app->systemReduced || app->motionOverride;
            app->ReadTextScale();
            app->ConfigureAppearance();
            app->UpdateDocks();
            if (app->docks)
                app->docks->Appearance(app->reducedMotion, app->highContrast, app->textScale);
            if (app->reducedMotion)
                app->SnapMotion();
            app->RefreshGeometry();
            return 0;
        }
        case WM_POWERBROADCAST:
            app->UpdateDocks();
            if (wparam == PBT_APMRESUMEAUTOMATIC) {
                if (app->media)
                    app->media->Rebase();
                if (app->tab == Tab::Stats && app->expanded)
                    app->SampleStats();
                app->SnapMotion();
            }
            return TRUE;
        case MotionFrame:
            if (app->frameClock.Accepts(wparam) && app->scheduler.active) {
                app->frameTickReady = true;
                app->Transition();
            }
            return 0;
        case WM_TIMER:
            if (wparam == desktop::ShellTimer)
                app->shell.Reconcile();
            else if (wparam == TransitionTimer)
                app->Transition();
            else if (wparam == StatsTimer) {
                app->SampleStats();
                InvalidateRect(window, nullptr, FALSE);
            } else if (wparam == MediaTimer) {
                if (app->media)
                    app->currentMedia = app->media->Snapshot();
                InvalidateRect(window, nullptr, FALSE);
            } else if (wparam == ProximityTimer)
                app->ProximityTick();
            else if (wparam == FocusTimer) {
                if (app->focusRunning) {
                    ULONGLONG now = GetTickCount64();
                    app->focusSeconds =
                        static_cast<int>((app->focusDeadline > now ? app->focusDeadline - now : 0) + 999) / 1000;
                    if (!app->focusSeconds) {
                        app->focusRunning = false;
                        app->note = L"Focus session complete.";
                        app->UpdateTimers();
                    }
                }
                if (app->expanded && !app->miniMode && app->flyout == Flyout::None) {
                    SYSTEMTIME clock{};
                    GetLocalTime(&clock);
                    int minute = clock.wHour * 60 + clock.wMinute;
                    if (app->tab == Tab::Today) {
                        app->RefreshToday();
                        app->RebuildLayout();
                        app->ArmMotion();
                    }
                    if (app->tab == Tab::Today || minute != app->lastHeaderMinute)
                        InvalidateRect(window, nullptr, FALSE);
                    app->lastHeaderMinute = minute;
                }
            } else if (wparam == ConnectivityTimer && app->connectivity) {
                if (app->flyout == Flyout::Wifi)
                    app->connectivity->RefreshWifi();
                else if (app->flyout == Flyout::Bluetooth)
                    app->connectivity->RefreshBluetooth();
            }
            return 0;
        case desktop::ShellChanged:
            app->shell.WindowEvent(static_cast<DWORD>(wparam), reinterpret_cast<HWND>(lparam));
            return 0;

        case desktop::TrayMessage:
            if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP)
                app->ContextMenu();
            else if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) {
                app->SetExpanded(!app->expanded);
                app->keyboardOpen = app->expanded;
                SetForegroundWindow(window);
            }
            return 0;
        case ConnectivityChanged:
            app->RefreshConnectionsView();
            return 0;
        case RevealDock:
            if (wparam == 1)
                app->SetTab(Tab::Stats);
            else
                app->SetExpanded(true);
            app->launchPreview = true;
            app->keyboardOpen = true;
            SetForegroundWindow(window);
            SetFocus(window);
            return 0;
        case StateChanged:
            app->system.RefreshVolume();
            app->volumeMotion.goal = app->system.volume;
            if (app->media) {
                auto next = app->media->Snapshot();
                if (next.title != app->currentMedia.title || next.artist != app->currentMedia.artist) {
                    app->contentBlend.Snap(app->reducedMotion ? 1.0f : 0.0f);
                    app->contentBlend.goal = 1;
                }
                if (!next.transportError.empty())
                    app->note = next.transportError;
                app->currentMedia = std::move(next);
            }
            if (app->expanded)
                app->ArmMotion();
            app->UpdateDocks();
            app->UpdateTimers();
            if (app->expanded)
                InvalidateRect(window, nullptr, FALSE);
            return 0;
        case AudioDeviceChanged:
            if (app->fixtureGeometry)
                return 0;
            app->system.BindAudio(window);
            app->RefreshControls();
            app->UpdateDocks();
            app->volumeMotion.goal = app->system.volume;
            if (app->expanded)
                app->ArmMotion();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case controls::Changed:
            if (wparam == 1) {
                app->RefreshControls();
                return 0;
            }
            if (app->nativeControls) {
                auto next = app->nativeControls->Read();
                if (app->connectivity && app->flyout == Flyout::Wifi)
                    app->connectivity->RefreshWifi(false);
                if (app->connectivity && app->flyout == Flyout::Bluetooth)
                    app->connectivity->RefreshBluetooth(false);
                HMONITOR desired =
                    app->panelMonitor ? app->panelMonitor : MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
                if (next.monitor != desired)
                    return 0;
                if (!app->draggingBrightness)
                    app->controlState = std::move(next);
                if (!app->controlState.message.empty())
                    app->note = app->controlState.message;
                app->RebuildLayout();
                app->PositionInputs();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_DEVICECHANGE:
            app->RefreshControls();
            if (app->catalog && app->tab == Tab::Wallpaper)
                app->catalog->DiscoverDisplays();
            return 0;
        case CatalogChanged:
            if (app->catalog) {
                std::vector<AppEntry> apps;
                std::vector<WallpaperEntry> wallpapers;
                std::wstring error;
                app->catalog->Snapshot(apps, wallpapers, error, static_cast<unsigned>(wparam));
                if (wparam == 4) {
                    DESKTOP_WALLPAPER_POSITION fit = DWPOS_FILL;
                    app->catalog->DisplaySnapshot(app->wallpaperDisplays, fit);
                    if (app->expanded && app->tab == Tab::Wallpaper && app->wallpaperSource == 2)
                        app->catalog->DiscoverCurrentWallpapers();
                    if (!app->wallpaperFitChosen)
                        app->wallpaperFit = fit;
                    if (!app->wallpaperTargetChosen) {
                        MONITORINFO info{sizeof(info)};
                        GetMonitorInfoW(app->panelMonitor ? app->panelMonitor
                                                          : MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY),
                                        &info);
                        for (const auto &display : app->wallpaperDisplays)
                            if (EqualRect(&display.bounds, &info.rcMonitor))
                                app->wallpaperTarget = display.id;
                    }
                } else if (wparam == 1) {
                    app->appsLoading = false;
                    if (!error.empty())
                        app->note = error;
                    app->apps = std::move(apps);
                    app->FilterApps(false);
                    if (app->flyout == Flyout::Palette)
                        app->BuildPalette();
                    app->RequestVisibleIcons();
                    app->UpdateDocks();
                } else if (wparam == 2) {
                    app->wallpaperLoading = false;
                    app->ReplaceWallpapers(std::move(wallpapers));
                    app->note = std::move(error);
                }

                app->RebuildLayout();
                app->PositionInputs();
                NotifyWinEvent(EVENT_OBJECT_REORDER, window, OBJID_CLIENT, CHILDID_SELF);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case parity::RecentChanged:
            if (app->recentService) {
                app->recentDocuments = app->recentService->Read(app->recentMessage);
                app->recentPage = 0;
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case ThemeChanged:
            app->SetAccent(static_cast<unsigned>(wparam));
            return 0;
        case HardwareChanged:
            if (app->hardware)
                app->hardwareState = app->hardware->Snapshot();
            if (app->expanded && app->tab == Tab::Stats) {
                app->RebuildLayout();
                app->ArmMotion();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WallpaperApplied:
            app->wallpaperApplying = false;
            if (wparam) {
                app->appliedWallpaper = app->pendingWallpaper;
                if (app->catalog)
                    app->catalog->SettingsChanged();
                if (app->catalog) {
                    app->catalog->DiscoverDisplays();
                    if (app->wallpaperSource == 2)
                        app->catalog->DiscoverCurrentWallpapers();
                }
                app->note = L"Wallpaper changed. Fit applies to all displays.";
            } else {
                app->wallpaperReveal.Cancel();
                app->note = lparam == static_cast<LPARAM>(wallpaper::Result::RollbackFailed)
                                ? L"Wallpaper failed and Windows could not restore fit. Review Wallpaper settings."
                                : L"Could not apply wallpaper. Choose another image.";
            }
            app->pendingWallpaper.clear();
            app->RebuildLayout();
            NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, window, OBJID_CLIENT, CHILDID_SELF);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_COMMAND:
            if ((HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) &&
                reinterpret_cast<HWND>(lparam) == app->taskEdit) {
                app->composerFocus.goal = HIWORD(wparam) == EN_SETFOCUS ? 1.0f : 0.0f;
                if (HIWORD(wparam) == EN_SETFOCUS)
                    app->focusedKey = app->StableKey(400);
                app->ArmMotion();
                InvalidateRect(window, nullptr, FALSE);
            }
            if (reinterpret_cast<HWND>(lparam) == app->taskEdit && HIWORD(wparam) == EN_CHANGE) {
                wchar_t draft[121]{};
                GetWindowTextW(app->taskEdit, draft, 121);
                app->taskDraft = draft;
                SendMessageW(app->taskEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&app->draftStart),
                             reinterpret_cast<LPARAM>(&app->draftEnd));
            }
            if (reinterpret_cast<HWND>(lparam) == app->appSearch && HIWORD(wparam) == EN_CHANGE) {
                if (!app->expanded && app->flyout == Flyout::Palette) return 0;
                int length = std::clamp(GetWindowTextLengthW(app->appSearch), 0,
                    app->flyout == Flyout::Palette ? 4096 : 120);
                std::wstring query(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(app->appSearch, query.data(), length + 1);
                query.resize(length);
                if (app->flyout == Flyout::Palette) {
                    app->paletteQuery = std::move(query);
                    app->palettePage = 0;
                    app->BuildPalette();
                } else {
                    app->appQuery = std::move(query);
                    app->FilterApps();
                }
                app->RebuildLayout();
                app->RequestVisibleIcons();
                app->ArmMotion();
                InvalidateRect(window, nullptr, FALSE);
            }
            if (reinterpret_cast<HWND>(lparam) == app->wifiPassword && HIWORD(wparam) == EN_CHANGE) {
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            auto dc = reinterpret_cast<HDC>(wparam);
            auto input = reinterpret_cast<HWND>(lparam);
            unsigned background = input == app->appSearch       ? theme::Card
                                  : input == app->clipboardEdit ? theme::Inset
                                  : input == app->taskEdit
                                      ? dock::Mix(theme::CardHover, theme::AccentSoft, app->composerFocus.value)
                                      : theme::CardHover;
            COLORREF fill = RGB((background >> 16) & 255, (background >> 8) & 255, background & 255);
            SetTextColor(dc, RGB((theme::Text >> 16) & 255, (theme::Text >> 8) & 255, theme::Text & 255));
            SetBkColor(dc, fill);
            SetDCBrushColor(dc, fill);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        case InvokeControl: {
            app->RebuildLayout();
            int index = app->IndexForChild(static_cast<LONG>(wparam));
            if (index >= 0 && app->CanInvoke(app->hits[index]))
                app->Invoke(app->hits[index].id);
            return 0;
        }
        case FocusControl:
            app->RebuildLayout();
            app->FocusAt(app->IndexForChild(static_cast<LONG>(wparam)));
            return 0;
        case WM_MOUSEMOVE: {
            if ((wparam & MK_LBUTTON) && !app->pressedKey.empty() && app->flyout == Flyout::Module &&
                app->moduleView == L"shelf" && app->pressedId >= 5100 && app->pressedId < 5120 &&
                (std::abs(GET_X_LPARAM(lparam) - app->moduleDragStart.x) >= GetSystemMetricsForDpi(SM_CXDRAG, static_cast<UINT>(app->dpi)) ||
                 std::abs(GET_Y_LPARAM(lparam) - app->moduleDragStart.y) >= GetSystemMetricsForDpi(SM_CYDRAG, static_cast<UINT>(app->dpi)))) {
                size_t index = app->modulePage * app->ModuleSlots() + static_cast<size_t>(app->pressedId - 5100);
                if (index < app->modulePanel.rows.size()) {
                    auto action = app->modulePanel.rows[index].action;
                    if (action.starts_with(L"shelf.copy-")) {
                        app->pressedKey.clear(); app->pressedId = 0; ReleaseCapture();
                        ModalGuard modal(app->modalOpen);
                        app->moduleRegistry.Drag(action, window);
                        return 0;
                    }
                }
            }
            if (app->draggingBrightness) {
                app->SetBrightnessAt(GET_X_LPARAM(lparam), false);
                return 0;
            }
            if (app->draggingVolume) {
                app->SetVolumeAt(GET_X_LPARAM(lparam));
                return 0;
            }
            app->keyboardOpen = false;
            app->launchPreview = false;
            if (!app->tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                app->tracking = true;
            }
            int hit = app->HitAt(GET_X_LPARAM(lparam) * 96.0f / app->dpi, GET_Y_LPARAM(lparam) * 96.0f / app->dpi);
            app->SetHover(hit);
            if (app->tab == Tab::Wallpaper && app->flyout == Flyout::None && !app->reducedMotion) {
                auto stage = app->Layout().stage;
                float px = (GET_X_LPARAM(lparam) * 96.0f / app->dpi - app->PanelX()) / app->textScale;
                app->wallpaperParallax.goal =
                    std::clamp((px - stage.CenterX()) / std::max(1.0f, stage.Width() / 2), -1.0f, 1.0f);
                if (app->wallpaperParallax.Moving())
                    app->ArmMotion();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            app->tracking = false;
            app->SetHover(-1);
            app->railStrength.goal = 0;
            if (app->railStrength.Moving())
                app->ArmMotion();
            return 0;
        case WM_MOUSEACTIVATE:
            return app->expanded ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &point);
            float x = point.x * 96.0f / app->dpi, y = point.y * 96.0f / app->dpi;
            bool handle = !app->YieldResting() && std::abs(x - app->envelopeWidth / 2) <= theme::DockWidth / 2 &&
                          y >= 0 && y <= theme::DockHeight;
            if (!app->renderMode && !(handle || (app->expanded && app->PresentedContains(x, y))))
                return HTTRANSPARENT;
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE && app->expanded && !app->modalOpen && !app->previewMode &&
                !(app->flyout == Flyout::Module && app->moduleView == L"shelf" && app->moduleRegistry.Enabled(L"shelf"))) {
                HWND next = reinterpret_cast<HWND>(lparam);
                if (!next || GetWindow(next, GW_OWNER) != window)
                    app->SetExpanded(false);
            }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            int hit = app->HitAt(GET_X_LPARAM(lparam) * 96.0f / app->dpi, GET_Y_LPARAM(lparam) * 96.0f / app->dpi);
            app->focused = hit;
            app->focusedKey = hit >= 0 ? app->hits[hit].key : L"";
            app->pressedKey = app->focusedKey;
            app->pressedId = hit >= 0 ? app->hits[hit].id : 0;
            app->moduleDragStart = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (hit >= 0)
                SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
            if (hit >= 0 && app->hits[hit].id == Brightness) {
                app->draggingBrightness = true;
                SetCapture(window);
                app->SetBrightnessAt(GET_X_LPARAM(lparam), false);
            }
            if (hit >= 0 && app->hits[hit].id == Volume) {
                app->draggingVolume = true;
                SetCapture(window);
                app->SetVolumeAt(GET_X_LPARAM(lparam));
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (app->draggingBrightness) {
                app->SetBrightnessAt(GET_X_LPARAM(lparam), true);
                app->draggingBrightness = false;
                ReleaseCapture();
                return 0;
            }
            if (app->draggingVolume) {
                app->draggingVolume = false;
                ReleaseCapture();
                return 0;
            }
            int hit = app->HitAt(GET_X_LPARAM(lparam) * 96.0f / app->dpi, GET_Y_LPARAM(lparam) * 96.0f / app->dpi);
            std::wstring pressed = app->pressedKey;
            app->pressedKey.clear();
            ReleaseCapture();
            if (hit >= 0 && app->hits[hit].key == pressed && app->hits[hit].id == app->pressedId) {
                auto &pulse = app->feedback[pressed];
                pulse.value = 1;
                pulse.goal = 0;
                app->Invoke(app->hits[hit].id);
            }
            app->pressedId = 0;
            app->ArmMotion();
            return 0;
        }
        case WM_CAPTURECHANGED:
            app->draggingBrightness = false;
            app->draggingVolume = false;
            app->pressedKey.clear();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            if (!app->expanded) {
                if (GET_WHEEL_DELTA_WPARAM(wparam) < 0)
                    app->SetExpanded(true);
                return 0;
            }
            if (app->tab == Tab::Today && app->flyout == Flyout::None) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                float x = (point.x * 96.0f / app->dpi - app->PanelX()) / app->textScale;
                float y = (point.y * 96.0f / app->dpi - app->PanelY()) / app->textScale + app->scroll;
                auto list = app->TaskListRect();
                if (x >= list.left && x <= list.right && y >= list.top && y <= list.bottom) {
                    app->ScrollTasks(-GET_WHEEL_DELTA_WPARAM(wparam) / 120.0f * 40);
                    return 0;
                }
            }
            app->scroll -= GET_WHEEL_DELTA_WPARAM(wparam) / 120.0f * 48;
            app->scroll = std::clamp(app->scroll, 0.0f,
                                     std::max(0.0f, app->Layout().contentHeight - app->Layout().body.Height()));
            app->RebuildLayout();
            app->PositionInputs();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            app->Key(wparam);
            return 0;
        case WM_HOTKEY:
            if (wparam == 1) {
                app->SetExpanded(!app->expanded);
                if (app->expanded) {
                    app->keyboardOpen = true;
                    SetForegroundWindow(window);
                    SetFocus(window);
                }
            } else if (wparam == 2)
                DestroyWindow(window);
            else if (wparam == 3) {
                app->OpenPalette();
            } else if (wparam == 4) {
                app->ToggleDesktopMode();
            } else if (wparam >= 1000 && wparam < 1009 && app->docks) {
                app->docks->ActivateNumber(static_cast<size_t>(wparam - 1000));
            }
            return 0;
        case WM_CONTEXTMENU:
            app->ContextMenu();
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, app->hovered >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            privacy::Policy().Observe(nullptr);
            app->moduleRegistry.Stop();
            app->clipboardReadTask.Stop();
            if (app->clipboardReadToken) {
                privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(app->clipboardReadToken));
                app->clipboardReadToken = 0;
            }
            app->recentService.reset();
            app->StopDesktop();
            app->hotZone.Stop();
            app->weatherService.reset();
            app->calendarService.reset();
            if (!app->diagnosticsPath.empty()) {
                privacy::Write audit(L"diagnostics", L"file-write", app->diagnosticsPath, L"performance-report");
                std::ofstream metrics(app->diagnosticsPath, std::ios::trunc);
                auto quantile = [](std::vector<double> values, double q) {
                    if (values.empty())
                        return -1.0;
                    std::sort(values.begin(), values.end());
                    return values[static_cast<size_t>(q * (values.size() - 1))];
                };
                metrics << "frame_clock_active=" << app->frameClock.Active()
                        << "\nframe_readiness_available=" << (app->composition.FrameReady() != nullptr) << "\n";
                metrics << "system_transparency=" << app->systemTransparency
                        << "\nbackdrop_hresult=" << static_cast<unsigned long>(app->backdropResult)
                        << "\ntext_scale=" << app->textScale << "\n";
                metrics << "animation_timer_active=" << app->scheduler.active
                        << "\nbackdrop_api_available=" << app->backdropAvailable
                        << "\ncomposition_active=" << app->composition.Active() << "\ndpi=" << app->dpi
                        << "\ntimer_period_ms=" << app->motionInterval << "\n";
                metrics << "animation_ticks=" << app->frameIntervals.size()
                        << "\ntick_p50_ms=" << quantile(app->frameIntervals, 0.5)
                        << "\ntick_p95_ms=" << quantile(app->frameIntervals, 0.95)
                        << "\npaint_p95_ms=" << quantile(app->paintTimes, 0.95)
                        << "\nThese are message/CPU paint timings, not presented-frame or GPU measurements.\n";
                metrics.flush();
                audit.Complete(static_cast<bool>(metrics));
            }
            if (app->accessibleProperties) {
                MSAAPROPID property = PROPID_ACC_NAME;
                for (HWND input : {app->taskEdit, app->appSearch, app->wifiPassword, app->clipboardEdit})
                    if (input)
                        app->accessibleProperties->ClearHwndProps(input, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                                                  &property, 1);
            }
            app->nativeControls.reset();
            app->frameClock.Stop();
            app->wallpaperReveal.Cancel();
            if (app->backdropWindow) {
                DestroyWindow(app->backdropWindow);
                app->backdropWindow = nullptr;
            }
            app->shell.Stop();
            app->shell.RemoveTray();
            KillTimer(window, TransitionTimer);
            KillTimer(window, StatsTimer);
            KillTimer(window, MediaTimer);
            KillTimer(window, ProximityTimer);
            KillTimer(window, FocusTimer);
            KillTimer(window, ConnectivityTimer);
            if (app->toggleHotkey)
                UnregisterHotKey(window, 1);
            if (app->quitHotkey)
                UnregisterHotKey(window, 2);
            if (app->taskbarHotkey)
                UnregisterHotKey(window, 4);
            if (app->paletteHotkey)
                UnregisterHotKey(window, 3);
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        }
    } catch (const winrt::hresult_error &error) {
        app->PreviewTrace("native-hresult-error");
        OutputDebugStringW(error.message().c_str());
        if (message == WM_CREATE)
            return -1;
        if (message == WM_PAINT) {
            ValidateRect(window, nullptr);
            app->ReleaseGraphics();
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
