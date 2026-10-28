#include "Island.h"
#include "SettingsPageModel.h"
#include "IslandBodyScope.h"

size_t Island::PaletteSlots() const {
    return std::min(size_t{6}, static_cast<size_t>(std::max(1.0f, (Body().bottom - Body().top - 48) / 44)));
}
void Island::BuildPalette() {
    moduleRegistry.Poll();
    moduleRegistry.Query(paletteQuery);
    std::vector<PaletteItem> all;
    const wchar_t *views[]{L"Controls", L"Today", L"Stats", L"Clipboard", L"Apps", L"Wallpaper", L"Power"};
    for (Tab view : PrimaryTabs) {
        int action = 100 + static_cast<int>(view);
        all.push_back(
            {L"view:" + std::to_wstring(action), L"Open " + std::wstring(views[static_cast<int>(view)]), L"", action});
    }
    const std::pair<int, const wchar_t *> actions[]{{4000, L"Show desktop"},
                                                    {4001, L"Task view"},
                                                    {2851, L"Dock status and tray controls"},
                                                    {Audio, L"Audio outputs"},
                                                    {OutputSettings, L"Windows Sound settings"},
                                                    {AirplaneSettings, L"Windows Radio settings"},
                                                    {ProjectDisplay, L"Project display"},
                                                    {ScreenSnip, L"Screen snip"},
                                                    {PowerSettings, L"Windows Power settings"},
                                                    {WallpaperSettings, L"Windows Wallpaper settings"},
                                                    {Lock, L"Lock Windows"}};
    for (const auto &action : actions)
        all.push_back({L"action:" + std::to_wstring(action.first), action.second, L"", action.first});
    for (const auto &[id, label] : {std::pair{static_cast<int>(SleepNow), IDS_COMMAND_SLEEP},
                                    std::pair{static_cast<int>(RestartNow), IDS_COMMAND_RESTART},
                                    std::pair{static_cast<int>(Dark), IDS_COMMAND_DARK}})
        all.push_back({L"action:" + std::to_wstring(id), modules::String(label), L"", id});
    all.push_back({L"features", modules::String(IDS_FEATURES), L"", 2900});
    for (const auto &action : moduleRegistry.Actions())
        all.push_back({L"module:" + action.id, action.text.empty() ? modules::String(action.label) : action.text, L"",
                       0, nullptr, action.id});
    for (const auto &app : apps)
        all.push_back({L"app:" + AppKey(app), app.name, AppKey(app)});
    for (const auto &item : runningWindows)
        all.push_back({L"window:" + std::to_wstring(reinterpret_cast<UINT_PTR>(item.window)),
                       L"Switch to " + item.title, L"", 0, item.window});
    paletteItems.clear();
    commandCatalog.Clear();
    for (size_t index = 0; index < all.size(); ++index) {
        const auto &item = all[index];
        bool learn = !item.target && !item.moduleAction.starts_with(L"clipboard.") &&
                     !item.moduleAction.starts_with(L"utilities.result-") &&
                     !item.moduleAction.starts_with(L"files.result-") && !item.moduleAction.starts_with(L"shelf.") &&
                     !item.moduleAction.starts_with(L"windows.page-result-") &&
                     !item.moduleAction.starts_with(L"drives.eject-") && item.key.size() <= 512;
        commandCatalog.Add({item.key, item.label, L"", learn, index}, [this, item] { ExecuteCommand(item); });
    }
    auto match = [](const command::CatalogAction &action, const std::wstring &query) {
        if (query.empty())
            return 0;
        if (CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, action.label.c_str(), -1, query.c_str(), -1,
                            nullptr, nullptr, 0) == CSTR_EQUAL)
            return 3;
        int found = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE, action.label.c_str(),
                                    -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0);
        return found < 0 ? -1 : found == 0 ? 2 : 1;
    };
    // Canonical target spelling can differ from typed escaping (for example ! versus %21).
    auto normalizedSettings = settingspage::Prefix(paletteQuery) ? settingspage::Uri(paletteQuery) : std::nullopt;
    for (size_t index : commandCatalog.Search(normalizedSettings ? *normalizedSettings : paletteQuery, match))
        paletteItems.push_back(std::move(all[commandCatalog.At(index).source]));
    if (palettePage * PaletteSlots() >= paletteItems.size())
        palettePage = 0;
}
void Island::OpenPalette() {
    miniMode = false;
    flyout = Flyout::Palette;
    tab = Tab::Apps;
    scroll = 0;
    palettePage = 0;
    paletteQuery.clear();
    if (connectivity)
        connectivity->CancelDiscovery();
    if (!renderMode && !fixtureGeometry && !catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    if (catalog && !appsRequested) {
        appsRequested = appsLoading = true;
        catalog->DiscoverApps();
    }
    BuildPalette();
    SetExpanded(true);
    UpdateTimers();
    keyboardOpen = true;
    launchPreview = true;
    EnsureAppSearch();
    if (appSearch) {
        SetWindowTextW(appSearch, L"");
        SendMessageW(appSearch, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(modules::String(IDS_PALETTE_HINT).c_str()));
    }
    SetForegroundWindow(window);
    RebuildLayout();
    FocusAt(IndexForKey(StableKey(401)));
}
void Island::ExecutePalette(size_t slot) {
    size_t index = palettePage * PaletteSlots() + slot;
    if (index >= paletteItems.size())
        return;
    auto item = paletteItems[index];
    commandCatalog.Invoke(item.key);
    if (commandCatalog.Dirty() && !renderMode && !fixtureGeometry && !previewMode) {
        if (SettingsStore(preferencesPath).Update(L"CommandRanking", commandCatalog.Save()))
            commandCatalog.SavedSuccessfully();
        else
            note = modules::String(IDS_SAVE_FAILED);
    }
}
void Island::ExecuteCommand(const PaletteItem &item) {
    if (!item.moduleAction.empty()) {
        InvokeModuleAction(item.moduleAction);
    } else if (item.target) {
        if (renderMode || fixtureGeometry)
            return;
        if (desktop::WindowList::Activate(item.target))
            SetExpanded(false);
        else
            note = L"Windows did not allow this window to become active.";
    } else if (!item.payload.empty()) {
        LaunchPin(item.payload);
    } else if (item.action == 4000 || item.action == 4001) {
        if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
            note = L"Desktop actions are disabled in preview.";
            return;
        }
        WORD key = item.action == 4000 ? 'D' : VK_TAB;
        INPUT input[4]{};
        for (auto &value : input)
            value.type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_LWIN;
        input[1].ki.wVk = key;
        input[2].ki.wVk = key;
        input[2].ki.dwFlags = KEYEVENTF_KEYUP;
        input[3].ki.wVk = VK_LWIN;
        input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SetExpanded(false);
        if (SendInput(4, input, sizeof(INPUT)) != 4)
            note = L"Windows did not accept the desktop shortcut.";
    } else if (item.action == 2851) {
        flyout = Flyout::DockStatus;
        SetExpanded(true);
    } else {
        flyout = Flyout::None;
        Invoke(item.action);
    }
    UpdateTimers();
    InvalidateRect(window, nullptr, FALSE);
}
void Island::Palette() {
    auto l = Layout();
    auto b = Body();
    if (l.emergency)
        CompactHeader(true);
    else {
        auto h = l.header;
        SmallButton(FlyoutBack, {h.left, 8, h.left + 40, 44}, L"\xE72B", L"Back to Apps");
        Text(L"Search Dako", {h.left + 52, 8, h.right, 44}, type::Section);
    }
    {
        DockBodyScope body(*this);
        D2D1_RECT_F input{b.left, b.top + 4, b.right, b.top + 36};
        Fill(input, theme::CardHover, 10);
        AddHit(401, input, modules::String(IDS_PALETTE_NAME));
        if (renderMode)
            Text(paletteQuery.empty() ? modules::String(IDS_PALETTE_HINT) : paletteQuery,
                 {input.left + 12, input.top, input.right - 12, input.bottom}, type::Body, theme::Muted);
        EnsureAppSearch();
        size_t slots = PaletteSlots();
        for (size_t slot = 0; slot < slots && palettePage * PaletteSlots() + slot < paletteItems.size(); ++slot) {
            const auto &item = paletteItems[palettePage * PaletteSlots() + slot];
            float y = b.top + 48 + static_cast<float>(slot) * 44;
            TextButton(2600 + static_cast<int>(slot), {b.left, y, b.right, y + 38}, item.label);
        }
        if (paletteItems.empty())
            Paragraph(L"No matches.", {b.left + 12, b.top + 52, b.right - 12, b.bottom - 12});
    }
    if (!l.emergency) {
        auto f = Footer();
        TextButton(2852, {f.left, f.top, f.left + 80, f.bottom}, L"Previous", false, palettePage > 0);
        TextButton(2853, {f.right - 80, f.top, f.right, f.bottom}, L"Next", false,
                   (palettePage + 1) * PaletteSlots() < paletteItems.size());
    }
}
void Island::DockPopover() {
    auto l = Layout();
    auto b = Body();
    if (l.emergency)
        CompactHeader(true);
    else {
        auto h = l.header;
        SmallButton(FlyoutBack, {h.left, 8, h.left + 40, 44}, L"\xE72B", L"Back to island");
        Text(L"Status & tray", {h.left + 52, 8, h.right, 44}, type::Section);
    }
    {
        DockBodyScope body(*this);
        const wchar_t *labels[]{L"Weather", L"Wi-Fi", L"Volume", L"Battery", L"Clock & calendar"};
        const int actions[]{101, Wifi, Volume, 101, 101};
        for (int i = 0; i < 5; ++i) {
            float y = b.top + static_cast<float>(i) * 42;
            bool shown = (dockOptions.statusMask & (1U << i)) != 0;
            SmallButton(2800 + i, {b.left, y, b.left + 40, y + 34}, shown ? L"\xE73E" : L"\xE739",
                        std::wstring(shown ? L"Hide " : L"Show ") + labels[i] + L" on dock");
            TextButton(actions[i] == 101 ? 2860 + i : 2870 + i, {b.left + 48, y, b.right, y + 34}, labels[i]);
        }
        float y = b.top + 222;
        TextButton(2805, {b.left, y, b.right, y + 32},
                   dockOptions.showDesktop ? L"Show desktop icon: On" : L"Show desktop icon: Off");
        TextButton(2806, {b.left, y + 40, b.right, y + 72},
                   dockOptions.showTaskView ? L"Task view icon: On" : L"Task view icon: Off");
    }
    if (!l.emergency) {
        auto f = Footer();
        TextButton(2850, R(layout::Rect{f.left, f.top, f.right, f.bottom}), L"Search all commands");
    }
}

void Island::OpenRecent(const std::wstring &appId, HMONITOR monitor) {
    if (renderMode || fixtureGeometry)
        return;
    OpenFromDock(desktop::DockRecent, monitor);
    flyout = Flyout::Recent;
    recentPage = 0;
    recentDocuments.clear();
    if (!recentService)
        recentService = std::make_unique<parity::RecentDocuments>(window);
    recentService->Request(appId);
    recentDocuments = recentService->Read(recentMessage);
    SetExpanded(true);
    UpdateTimers();
    RebuildLayout();
    PositionInputs();
    InvalidateRect(window, nullptr, FALSE);
}
void Island::RecentView() {
    auto l = Layout();
    auto b = Body();
    if (l.emergency)
        CompactHeader(true);
    else {
        auto h = l.header;
        SmallButton(FlyoutBack, {h.left, 8, h.left + 40, 44}, L"\xE72B", L"Back to Apps");
        Text(L"Recent documents", {h.left + 52, 8, h.right, 44}, type::Section);
    }
    {
        DockBodyScope body(*this);
        size_t slots = PaletteSlots();
        for (size_t slot = 0; slot < slots && recentPage * slots + slot < recentDocuments.size(); ++slot) {
            float y = b.top + static_cast<float>(slot) * 44;
            TextButton(2700 + static_cast<int>(slot), {b.left, y, b.right, y + 38},
                       recentDocuments[recentPage * slots + slot].name);
        }
        if (recentDocuments.empty())
            Paragraph(recentMessage, {b.left + 12, b.top + 12, b.right - 12, b.bottom - 12});
    }
    if (!l.emergency) {
        auto f = Footer();
        TextButton(2750, {f.left, f.top, f.left + 80, f.bottom}, L"Previous", false, recentPage > 0);
        TextButton(2751, {f.right - 80, f.top, f.right, f.bottom}, L"Next", false,
                   (recentPage + 1) * PaletteSlots() < recentDocuments.size());
    }
}
