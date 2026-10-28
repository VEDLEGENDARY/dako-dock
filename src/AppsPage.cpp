#include "Island.h"

size_t Island::AppIndex(size_t slot) const {
    return appPage < appPages.size() && appPages[appPage].begin + slot < appPages[appPage].end
               ? filteredApps[appPages[appPage].begin + slot]
               : apps.size();
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

std::wstring Island::AppKey(const AppEntry &app) const { return app.parsingName.empty() ? app.name : app.parsingName; }

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

bool Island::CompactApps() const { return Layout().body.Height() < 124; }
bool Island::ShowAppRail() const {
    return Layout().body.Height() >= (runningIndices.empty() ? 200.0f : 300.0f) && !railApps.empty();
}
void Island::RepageApps() {
    std::vector<int> groups;
    for (auto index : filteredApps)
        groups.push_back(AppGroup(apps[index]));
    float row = CompactApps() ? 40.0f : 44.0f, heading = CompactApps() ? 20.0f : 24.0f;
    appPages = layout::Paginate(groups, std::max(row + heading, Body().bottom - AppsListTop()), row, heading);
    if (appPage >= appPages.size())
        appPage = appPages.empty() ? 0 : appPages.size() - 1;
}
size_t Island::AppSlots() const {
    return appPage < appPages.size() ? appPages[appPage].end - appPages[appPage].begin : 0;
}
D2D1_RECT_F Island::SearchRect() const {
    auto b = Body();
    return {b.left + 36, b.top + (CompactApps() ? 4.0f : 8.0f), b.right - 12, b.top + (CompactApps() ? 28.0f : 36.0f)};
}
void Island::Apps() {
    FilterRunning();
    RepageApps();
    auto b = Body();
    bool compact = CompactApps();
    Fill({b.left, b.top, b.right, b.top + (compact ? 32.0f : 44.0f)}, theme::Card, 12);
    Text(L"\xE721", {b.left + 8, b.top + 8, b.left + 32, b.top + 36}, type::Icon, theme::Muted);
    AddHit(401, SearchRect(), L"Search installed apps");
    EnsureAppSearch();
    if (renderMode)
        Text(appQuery.empty() ? L"Search installed apps…" : appQuery, SearchRect(), type::Body, theme::Muted);
    float nextY = b.top + (compact ? 40.0f : 56.0f);
    if (ShowAppRail()) {
        Fill({b.left, nextY, b.right, nextY + 68}, theme::Inset, 14);
        Text(L"Pinned", {b.left + 12, nextY, b.right - 12, nextY + 20}, type::Caption, theme::Muted);
        size_t count = std::min(railApps.size(), static_cast<size_t>(std::max(1.0f, (b.right - b.left - 24) / 40)));
        for (size_t i = 0; i < count; ++i) {
            int id = 1400 + static_cast<int>(i);
            const auto &app = apps[railApps[i]];
            float x = b.left + 12 + static_cast<float>(i) * 40;
            D2D1_RECT_F rect{x, nextY + 24, x + 32, nextY + 56};
            AddHit(id, rect, L"Launch pinned " + app.name);
            Fill(Tactile(id, rect), theme::Card, 8);
            DrawAppIcon(app, rect);
            FocusRing(id, rect, 8);
        }
        nextY += 76;
    }
    if (b.bottom - b.top >= 224 && !runningIndices.empty()) {
        Text(L"Running · " + std::to_wstring(runningIndices.size()), {b.left + 4, nextY, b.right - 82, nextY + 24},
             type::Caption, theme::Muted);
        SmallButton(RunningPrevious, {b.right - 72, nextY, b.right - 42, nextY + 24}, L"\xE76B",
                    L"Previous running windows", runningPage > 0);
        SmallButton(RunningNext, {b.right - 34, nextY, b.right - 4, nextY + 24}, L"\xE76C", L"Next running windows",
                    (runningPage + 1) * 2 < runningIndices.size());
        for (size_t slot = 0; slot < 2; ++slot) {
            size_t index = runningPage * 2 + slot;
            if (index >= runningIndices.size())
                break;
            const auto &entry = runningWindows[runningIndices[index]];
            float y = nextY + 24 + static_cast<float>(slot) * 36;
            D2D1_RECT_F row{b.left, y, b.right, y + 32};
            int id = 2400 + static_cast<int>(slot);
            AddHit(id, row, L"Activate " + entry.title + (entry.minimized ? L", minimized" : L""));
            Fill(Tactile(id, row), entry.active ? theme::AccentSoft : theme::Card, 8);
            Text(entry.title, {b.left + 12, y, b.right - 12, y + 32}, type::Control,
                 highContrast && entry.active ? theme::SelectedText : theme::Text);
            FocusRing(id, row, 8);
        }
    }
    nextY = AppsListTop();
    int group = -1;
    for (size_t slot = 0; slot < AppSlots(); ++slot) {
        size_t index = AppIndex(slot);
        if (index >= apps.size())
            break;
        const auto &entry = apps[index];
        int current = AppGroup(entry);
        if (group != current) {
            group = current;
            Text(group == 0   ? L"Pinned"
                 : group == 1 ? L"Recent · launched through Dako"
                              : L"Installed applications",
                 {b.left + 4, nextY, b.right - 4, nextY + (compact ? 20.0f : 24.0f)}, type::Caption, theme::Muted);
            nextY += compact ? 20.0f : 24.0f;
        }
        int id = 300 + static_cast<int>(slot);
        float y = RowY(StableKey(id), nextY);
        nextY += compact ? 40.0f : 44.0f;
        D2D1_RECT_F row{b.left, y, b.right - 44, y + (compact ? 36.0f : 40.0f)};
        AddHit(id, row, entry.name);
        bool selected = Favorite(entry) || AppKey(entry) == selectedAppKey;
        Fill(Tactile(id, row), selected ? theme::AccentSoft : theme::Card, 10);
        DrawAppIcon(entry, {b.left + 8, y + 6, b.left + 36, y + 34});
        Text(entry.name, {b.left + 48, y, b.right - 56, y + 40}, type::Body,
             highContrast && selected ? theme::SelectedText : theme::Text);
        FocusRing(id, row);
        int star = 900 + static_cast<int>(slot);
        SmallButton(star, {b.right - 36, y + 2, b.right, y + 38}, Favorite(entry) ? L"\xE735" : L"\xE734",
                    (Favorite(entry) ? L"Unpin " : L"Pin ") + entry.name);
    }
    if (filteredApps.empty())
        Paragraph(apps.empty() ? (appsLoading ? L"Loading installed apps…"
                                              : L"No installed applications available. Reopen Apps to retry.")
                               : L"No matching apps. Change the search text.",
                  {b.left + 12, nextY + 16, b.right - 12, b.bottom - 8});
}

void Island::DrawAppIcon(const AppEntry &app, D2D1_RECT_F rect) {
    if (layoutOnly)
        return;
    auto found = appIcons.find(AppKey(app));
    if (found == appIcons.end() && app.iconPixels && !app.iconPixels->empty()) {
        ComPtr<ID2D1Bitmap> icon;
        target->CreateBitmap(
            D2D1::SizeU(app.iconWidth, app.iconHeight), app.iconPixels->data(), app.iconWidth * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &icon);
        if (appIcons.size() >= 48)
            appIcons.erase(appIcons.begin());
        found = appIcons.emplace(AppKey(app), icon).first;
    }
    if (found != appIcons.end() && found->second)
        target->DrawBitmap(found->second.Get(), rect);
    else {
        Fill(rect, theme::CardHover, 8);
        Text(app.name.substr(0, 1), rect, type::Control, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
