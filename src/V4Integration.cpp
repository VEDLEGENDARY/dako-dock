#include "Island.h"

void Island::LoadV4Settings() {
    options = v4::ReadValues(preferencesPath);
    ApplyV4Values();
}

void Island::ApplyV4Values() {
    edge = options.Text(L"Edge") == L"Top" ? anchor::Edge::Top : anchor::Edge::Bottom;
    accentColor = static_cast<unsigned>(std::stoul(options.Text(L"Accent"), nullptr, 16));
    interaction.Configure({options.Int(L"HoverDelay") / 1000.0f, options.Int(L"LeaveDelay") / 1000.0f, .8f, .3f});
    theme::Radius = static_cast<float>(options.Int(L"Radius"));
    dockOptions.radius = theme::Radius;
    dockOptions.reversedScroll = options.Text(L"Scroll") == L"Reversed";
    dockOptions.autoHide = options.Bool(L"AutoHide");
    dockOptions.island = true;
    dockOptions.edge = edge;
    dockOptions.fullscreenPolicy = options.Text(L"Fullscreen") == L"Hide over games"        ? 1
                                   : options.Text(L"Fullscreen") == L"Hide over fullscreen" ? 2
                                                                                            : 0;
    dockOptions.iconSize = static_cast<float>(options.Int(L"IconSize"));
    dockOptions.magnify = options.Bool(L"Magnify");
    dockOptions.labels = options.Bool(L"Labels");
    dockOptions.runningDots = options.Bool(L"RunningDots");
    dockOptions.opacity = options.Int(L"Opacity") / 100.0f;
    dockOptions.hideApps = v4::Split(options.Text(L"HideApps"));
    dockOptions.monitorMode = options.Text(L"Monitor");
    visibleTabs.clear();
    constexpr const wchar_t *names[]{L"Controls", L"Today", L"Stats", L"Clipboard", L"Apps", L"Wallpaper", L"Power"};
    for (const auto &name : v4::Split(options.Text(L"Tabs")))
        for (size_t i = 0; i < std::size(names); ++i)
            if (name == names[i])
                visibleTabs.push_back(static_cast<Tab>(i));
    if (!expanded)
        for (size_t i = 0; i < std::size(names); ++i)
            if (options.Text(L"DefaultTab") == names[i])
                tab = static_cast<Tab>(i);
    if (visibleTabs.empty())
        visibleTabs.assign(PrimaryTabs.begin(), PrimaryTabs.end());
    if (!expanded && std::find(visibleTabs.begin(), visibleTabs.end(), tab) == visibleTabs.end())
        tab = visibleTabs.front();
    if (!options.Text(L"WeatherCity").empty())
        weatherCity = options.Text(L"WeatherCity");
    if (!options.Text(L"WallpaperFolder").empty())
        wallpaperFolder = options.Text(L"WallpaperFolder");
}

const std::vector<Tab> &Island::EnabledTabs() {
    if (visibleTabs.empty())
        visibleTabs.assign(PrimaryTabs.begin(), PrimaryTabs.end());
    return visibleTabs;
}

void Island::OpenSettings() {
    if (renderMode || fixtureGeometry)
        return;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    auto path = std::filesystem::path(executable).parent_path() / L"DakoSettings.exe";
    ShellExecuteW(window, L"open", path.c_str(), nullptr, path.parent_path().c_str(), SW_SHOWNORMAL);
}

void Island::SettingsUpdated(WPARAM preview) {
    if (renderMode || fixtureGeometry || previewMode)
        return;
    auto oldEdge = edge;
    auto oldMonitor = options.Text(L"Monitor");
    bool oldDesktop = desktopRequested;
    bool oldAutoHide = options.Bool(L"AutoHide");
    if (!preview)
        LoadV4Settings();
    if (preview && settingsWatch) {
        if (auto packet = settingsWatch->Consume(); packet && packet->command == 0) {
            auto setting = v4::Find(packet->key);
            if (setting && v4::Valid(*setting, packet->value))
                options.data[packet->key] = packet->value;
        } else
            LoadV4Settings();
    }
    ApplyV4Values();
    UpdateInteractionMedia();
    dockOptions.edge = edge;
    accentColor = static_cast<unsigned>(std::stoul(options.Text(L"Accent"), nullptr, 16));
    motionOverride = options.Bool(L"ReducedMotion");
    reducedMotion = systemReduced || motionOverride;
    systemAppearance = options.Text(L"Theme") == L"System";
    lightAppearance = options.Text(L"Theme") == L"Paper";
    if (!preview) {
        privacy::Policy().SetOffline(options.Bool(L"Offline"));
        bool requested = options.Bool(L"ReplaceTaskbar");
        if (requested && !oldDesktop) {
            desktopPromptDone = false;
            ConfirmDesktopChoice();
        } else if (!requested)
            desktopRequested = false;
        if (toggleHotkey)
            UnregisterHotKey(window, 1);
        auto shortcut = options.Text(L"Hotkey");
        UINT key = shortcut == L"Space" ? VK_SPACE : static_cast<UINT>(shortcut[0]);
        toggleHotkey = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key) != FALSE;
        RefreshToday();
    }
    if (!preview) {
        if (moduleRegistry.Enabled(L"clipboard"))
            moduleRegistry.Invoke(options.Bool(L"ClipboardEncrypted") ? L"clipboard.persist-on"
                                                                      : L"clipboard.persist-off",
                                  window, L"", true);
        PostMessageW(window, modules::Changed, 0, 0);
        ConfigureSlideshow();
    }
    ReadTextScale();
    ConfigureAppearance();
    if (oldDesktop != desktopRequested)
        StartDesktop();
    else if (docks &&
             (oldEdge != edge || oldMonitor != options.Text(L"Monitor") || oldAutoHide != options.Bool(L"AutoHide")))
        docks->RefreshMonitors();
    RefreshGeometry();
    RetargetLayout();
    RebuildLayout();
}
