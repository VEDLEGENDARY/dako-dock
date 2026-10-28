#include "Island.h"
#include "ClipboardPaste.h"

bool Island::CanInvoke(const Hit &hit) const {
    return hit.enabled && VisibleHit(hit) && ((hit.id == Expand && !expanded && !YieldResting()) || Actionable());
}

bool Island::Actionable() const { return expanded && !capturingPane; }

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
    if (id >= 2040 && id < 2044 && tab == Tab::Stats) {
        const auto &ranked =
            statsMemory && !hardwareState.memoryTop.empty() ? hardwareState.memoryTop : hardwareState.top;
        if (static_cast<size_t>(id - 2040) < ranked.size())
            return L"process:" + std::to_wstring(ranked[static_cast<size_t>(id - 2040)].identity);
    }
    if (id >= 8600 && id < 8625 && static_cast<size_t>(id - 8600) < clipboardRecords.size())
        return L"clipboard-record:" + std::to_wstring(clipboardRecords[static_cast<size_t>(id - 8600)].id);

    if ((id >= 5100 && id < 5120) || (id >= 7200 && id < 7220)) {
        size_t slot = static_cast<size_t>(id - (id >= 7200 ? 7200 : 5100));
        size_t index = modulePage * ModuleSlots() + slot;
        if (index < modulePanel.rows.size())
            return L"module:" + moduleView + L":" +
                   (modulePanel.rows[index].action.empty() ? std::to_wstring(index) : modulePanel.rows[index].action);
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

bool Island::VisibleHit(const Hit &h) const {
    auto r = h.rect, c = h.clip;
    return r.bottom > c.top && r.top < c.bottom && r.right > c.left && r.left < c.right;
}

bool Island::PresentedContains(float x, float y) const {
    if (renderMode)
        return x >= 0 && x <= width && y >= 0 && y <= height;
    float left = (envelopeWidth - width) / 2;
    auto polygon =
        anchor::ForEdge(edge, dock::Attached(left, PanelY(), width, height, theme::Radius), PanelY(), height);
    return dockReveal.value > .003f && anchor::Contains(polygon, x, y);
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

void Island::Invoke(int id) {
    if (id == 8970 || id == 8971) {
        TodayAction(id);
        return;
    }
    if (id >= 8600 && id < 8800) {
        ClipboardAction(id);
        RebuildLayout();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id == 2980) {
        if (renderMode || fixtureGeometry || previewMode) {
            note = L"Speed tests are disabled in preview.";
            return;
        }
        if (!options.Bool(L"SpeedTest") || privacy::Policy().Offline()) {
            note = L"Enable speed testing and turn off Offline in Dako settings.";
            OpenSettings();
            return;
        }
        if (!speedTest)
            speedTest = std::make_unique<SpeedTest>(window);
        speedTest->Start();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    if (id == 2990) {
        OpenSettings();
        return;
    }
    if (id >= 5100 && id < 5120) {
        size_t index = modulePage * ModuleSlots() + static_cast<size_t>(id - 5100);
        if (index < modulePanel.rows.size())
            InvokeModuleAction(modulePanel.rows[index].action);
        return;
    }
    if (id == 5150 || id == 5151 || id == 5152) {
        if (id == 5150 && modulePage)
            --modulePage;
        if (id == 5151 && (modulePage + 1) * ModuleSlots() < modulePanel.rows.size())
            ++modulePage;
        if (id == 5152 && !renderMode && !fixtureGeometry) {
            ModalGuard modal(modalOpen);
            auto query = PromptText(window, modules::String(IDS_MODULE_SEARCH), modules::String(IDS_MODULE_SEARCH),
                                    moduleQuery, 120);
            if (query) {
                moduleQuery = *query;
                modulePage = 0;
            }
        }
        if (auto panel = moduleRegistry.View(moduleView, moduleQuery))
            modulePanel = std::move(*panel);
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
    if (tab == Tab::Clipboard && flyout == Flyout::None) {
        if (key == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            clipboardFilter = (clipboardFilter + ((GetKeyState(VK_SHIFT) & 0x8000) ? 3 : 1)) % 4;
            clipboardOffset = 0;
            RebuildLayout();
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        if ((key == VK_UP || key == VK_DOWN) && !clipboardRecords.empty()) {
            auto found = std::find_if(clipboardRecords.begin(), clipboardRecords.end(),
                                      [&](const auto &item) { return item.id == clipboardSelected; });
            int index = found == clipboardRecords.end() ? 0 : static_cast<int>(found - clipboardRecords.begin());
            index = std::clamp(index + (key == VK_DOWN ? 1 : -1), 0, static_cast<int>(clipboardRecords.size()) - 1);
            if (static_cast<size_t>(index) < clipboardOffset)
                clipboardOffset = static_cast<size_t>(index);
            if (static_cast<size_t>(index) >= clipboardOffset + 4)
                clipboardOffset = static_cast<size_t>(index) - 3;
            ClipboardAction(8600 + index);
            RebuildLayout();
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        if (key == VK_DELETE) {
            ClipboardAction(8752);
            RebuildLayout();
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        if (key == VK_RETURN || ((GetKeyState(VK_CONTROL) & 0x8000) && key == 'C')) {
            ClipboardAction(key == VK_RETURN ? 8755 : 8750);
            return;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && key == 'P') {
            ClipboardAction(8751);
            RebuildLayout();
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
    }

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
        const auto &tabs = EnabledTabs();
        auto found = std::find(tabs.begin(), tabs.end(), tab);
        size_t index = found == tabs.end() ? 0 : static_cast<size_t>(found - tabs.begin());
        SetTab(tabs[(index + (key == VK_NEXT ? 1 : tabs.size() - 1)) % tabs.size()]);
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
        for (int page = 0; page < 4; ++page)
            add(2901 + page, modules::String(pages[page]));
        if (featuresPage == 0) {
            size_t index = 0;
            for (const auto *descriptor : moduleRegistry.Descriptors())
                add(2920 + static_cast<int>(index++),
                    modules::String(moduleRegistry.Enabled(descriptor->id) ? IDS_REMOVE : IDS_ENABLE) + L" · " +
                        modules::String(descriptor->name));
        } else if (featuresPage == 1) {
            const UINT presets[]{IDS_MINIMAL, IDS_TASKBAR, IDS_POWER_USER, IDS_CREATOR};
            for (int i = 0; i < 4; ++i)
                add(2910 + i, modules::String(presets[i]));
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
    AppendMenuW(menu, MF_STRING, 2990, L"Dako settings…");
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
                    app->clipboardState =
                        modules::String(reply->status == ::clipboard::TransferStatus::Excluded ? IDS_CLIP_EXCLUDED
                                        : reply->status == ::clipboard::TransferStatus::Ok     ? IDS_CLIP_PREVIEW_READY
                                                                                               : IDS_CLIP_UNSUPPORTED);
                    if (app->clipboardEdit)
                        SetWindowTextW(app->clipboardEdit, app->clipboard.c_str());
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
            if (app->flyout == Flyout::Palette)
                app->BuildPalette();
            else {
                app->paletteItems.clear();
                app->commandCatalog.Clear();
            }
            if (app->flyout != Flyout::Module)
                app->modulePanel = {};
            if (app->flyout == Flyout::Module) {
                auto panel = app->moduleRegistry.View(app->moduleView, app->moduleQuery);
                if (panel) {
                    app->modulePanel = std::move(*panel);
                    if (app->modulePage * app->ModuleSlots() >= app->modulePanel.rows.size())
                        app->modulePage = 0;
                } else
                    app->OpenFeatures();
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
        case v4::SettingsChanged:
            app->SettingsUpdated(wparam);
            return 0;
        case WM_APP + 85:
            if (app->expanded)
                InvalidateRect(window, nullptr, FALSE);
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
                    app->accessible.Attach(CreateIslandAccessible(window));
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
            if (wparam == 2) {
                app->InteractionPointer(false);
                return 0;
            }
            if (wparam == 1) {
                app->miniMode = false;
                app->SetExpanded(true);
                return 0;
            }
            app->lastInteraction = steady_clock::now();
            app->InteractionPointer(true);
            return 0;
        case NotchHotZone::ContextChanged:
            app->UpdateContext();
            if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST))
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        case SlideshowDue:
            app->RunSlideshow(wparam);
            return 0;
        case SlideshowCatalogChanged:
            if (wparam == 2)
                app->SlideshowReady(lparam);
            return 0;
        case SlideshowApplied:
            app->SlideshowFinished(wparam, lparam);
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
            } else if (wparam == InteractionTimer)
                app->InteractionTick();
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
                if (app->docks) {
                    POINT point{};
                    GetCursorPos(&point);
                    HMONITOR monitor = app->options.Text(L"Monitor") == L"Primary"
                                           ? app->docks->ActiveMonitor()
                                           : MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
                    if (app->options.Text(L"Monitor") == L"Cursor" && monitor != app->docks->ActiveMonitor())
                        app->docks->RefreshMonitors();
                    app->panelMonitor = monitor;
                    RECT pill = app->docks->PillBounds(monitor);
                    app->dockAnchor.x = (pill.left + pill.right) / 2;
                    app->RefreshGeometry();
                }
                app->miniMode = false;
                app->interaction.OnToggle();
                app->ApplyInteraction();
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
            if (!app->fixtureGeometry && !app->renderMode) {
                app->UpdateInteractionMedia();
            }
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
            if (reinterpret_cast<HWND>(lparam) == app->clipboardEdit && HIWORD(wparam) == EN_CHANGE) {
                wchar_t query[121]{};
                GetWindowTextW(app->clipboardEdit, query, 121);
                app->clipboardQuery = query;
                app->RebuildLayout();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
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
                if (!app->expanded && app->flyout == Flyout::Palette)
                    return 0;
                int length =
                    std::clamp(GetWindowTextLengthW(app->appSearch), 0, app->flyout == Flyout::Palette ? 4096 : 120);
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
            app->InteractionPointer(true);
            if ((wparam & MK_LBUTTON) && !app->pressedKey.empty() && app->flyout == Flyout::Module &&
                app->moduleView == L"shelf" && app->pressedId >= 5100 && app->pressedId < 5120 &&
                (std::abs(GET_X_LPARAM(lparam) - app->moduleDragStart.x) >=
                     GetSystemMetricsForDpi(SM_CXDRAG, static_cast<UINT>(app->dpi)) ||
                 std::abs(GET_Y_LPARAM(lparam) - app->moduleDragStart.y) >=
                     GetSystemMetricsForDpi(SM_CYDRAG, static_cast<UINT>(app->dpi)))) {
                size_t index = app->modulePage * app->ModuleSlots() + static_cast<size_t>(app->pressedId - 5100);
                if (index < app->modulePanel.rows.size()) {
                    auto action = app->modulePanel.rows[index].action;
                    if (action.starts_with(L"shelf.copy-")) {
                        app->pressedKey.clear();
                        app->pressedId = 0;
                        ReleaseCapture();
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
            app->InteractionPointer(false);
            app->tracking = false;
            app->SetHover(-1);
            app->railStrength.goal = 0;
            if (app->railStrength.Moving())
                app->ArmMotion();
            return 0;
        case WM_MOUSEACTIVATE:
            if (GetForegroundWindow() != window)
                app->clipboardPasteTarget = ::clipboard::CapturePasteTarget();
            return app->expanded ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_WINDOWPOSCHANGING:
            if (auto position = reinterpret_cast<WINDOWPOS *>(lparam); !(position->flags & SWP_NOZORDER))
                position->hwndInsertAfter = HWND_TOPMOST;
            break;
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
                !(app->flyout == Flyout::Module && app->moduleView == L"shelf" &&
                  app->moduleRegistry.Enabled(L"shelf"))) {
                HWND next = reinterpret_cast<HWND>(lparam);
                if (!next || GetWindow(next, GW_OWNER) != window)
                    app->SetExpanded(false);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (app->tab == Tab::Controls) {
                int hit = app->HitAt(GET_X_LPARAM(lparam) * 96.0f / app->dpi, GET_Y_LPARAM(lparam) * 96.0f / app->dpi);
                if (hit >= 0 && app->hits[hit].id == 2970 && app->media) {
                    auto rect = app->hits[hit].rect;
                    double x = (GET_X_LPARAM(lparam) * 96.0f / app->dpi - app->PanelX()) / app->textScale;
                    app->media->Seek(std::clamp((x - rect.left) / (rect.right - rect.left), 0.0, 1.0) *
                                     app->currentMedia.duration);
                    return 0;
                }
            }
            {
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
            if (!app->expanded || app->miniMode) {
                bool expand = anchor::WheelExpands(app->edge, GET_WHEEL_DELTA_WPARAM(wparam));
                if (app->options.Text(L"Scroll") == L"Reversed")
                    expand = !expand;
                if (expand) {
                    app->miniMode = false;
                    app->SetExpanded(true);
                }
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
            if (GetForegroundWindow() != window)
                app->clipboardPasteTarget = ::clipboard::CapturePasteTarget();
            if (wparam == 1) {
                app->miniMode = false;
                app->interaction.OnToggle();
                app->ApplyInteraction();
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
        case WM_EXITMENULOOP:
            PostMessageW(window, NotchHotZone::PointerNear, 2, 0);
            return 0;
        case WM_ENABLE:
            if (wparam)
                PostMessageW(window, NotchHotZone::PointerNear, 2, 0);
            break;
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
            app->speedTest.reset();
            app->settingsWatch.reset();
            app->slideshowDeadline.reset();
            app->slideshowCatalog.reset();
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
            if (app->backdropWindow) {
                DestroyWindow(app->backdropWindow);
                app->backdropWindow = nullptr;
            }
            app->shell.Stop();
            app->shell.RemoveTray();
            KillTimer(window, TransitionTimer);
            KillTimer(window, StatsTimer);
            KillTimer(window, MediaTimer);
            KillTimer(window, InteractionTimer);
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
