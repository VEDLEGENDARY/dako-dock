#include "Island.h"
#include "ClipboardPaste.h"
#include "ClipboardData.h"
#include "ClipboardVault.h"

void Island::ReadClipboard() {
    clipboard.clear();
    clipboardOffset = 0;
    if (renderMode || fixtureGeometry || previewMode) {
        clipboardState = modules::String(IDS_CLIP_INERT);
    } else if (clipboardReadTask.Busy()) {
        clipboardState = modules::String(IDS_CLIP_READING);
    } else {
        clipboardReadToken = privacy::Policy().Reserve();
        privacy::Policy().Watch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken), L"core-clipboard",
                                L"clipboard-read");
        if (!clipboardReadTask.Start(::clipboard::TransferOp::Read, {}, window, L"core-clipboard")) {
            privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken));
            clipboardReadToken = 0;
            clipboardState = modules::String(IDS_CLIP_UNSUPPORTED);
        } else
            clipboardState = modules::String(IDS_CLIP_READING);
    }
    if (clipboardEdit)
        SetWindowTextW(clipboardEdit, L"");
}

D2D1_RECT_F Island::ClipboardRect() const {
    auto b = Body();
    float right = b.right - (b.right - b.left >= 640 ? 296.0f : 0.0f);
    return {b.left + (b.right - b.left >= 160 ? 36 : 4), b.top + 4, right - (clipboardQuery.empty() ? 4 : 40),
            b.top + 36};
}
void Island::EnsureClipboardEdit() {
    if (clipboardEdit || renderMode || capturingPane || layoutOnly)
        return;
    clipboardEdit = CreateWindowExW(0, L"EDIT", clipboardQuery.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1,
                                    1, window, reinterpret_cast<HMENU>(403), nullptr, nullptr);
    LabelInput(clipboardEdit, L"Search clipboard history");
    EnsureEditResources();
    SendMessageW(clipboardEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    SendMessageW(clipboardEdit, EM_SETLIMITTEXT, 120, 0);
    SendMessageW(clipboardEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search history…"));
    PositionInputs();
}
void Island::ClipboardAction(int id) {
    if (id == 8791) {
        OpenSettings();
        return;
    }
    if (id == 8753) {
        if (clipboardOffset > 0)
            --clipboardOffset;
        return;
    }
    if (id == 8754) {
        if (clipboardOffset + 1 < clipboardRecords.size())
            ++clipboardOffset;
        return;
    }
    if (id >= 8700 && id <= 8703) {
        clipboardFilter = id - 8700;
        return;
    }
    if (id == 8790) {
        if (ConfirmModule(L"clipboard")) {
            moduleRegistry.Enable(L"clipboard");
            if (!previewMode && !renderMode && !fixtureGeometry)
                moduleRegistry.Save(SettingsStore(preferencesPath));
        }
        return;
    }
    if (id >= 8600 && id < 8625 && static_cast<size_t>(id - 8600) < clipboardRecords.size()) {
        auto item = clipboardRecords[static_cast<size_t>(id - 8600)].id;
        bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shift) {
            auto previous = std::find_if(clipboardRecords.begin(), clipboardRecords.end(),
                                         [&](const auto &entry) { return entry.id == clipboardSelected; });
            if (previous != clipboardRecords.end()) {
                auto first = static_cast<size_t>(previous - clipboardRecords.begin());
                auto last = static_cast<size_t>(id - 8600);
                if (first > last)
                    std::swap(first, last);
                for (auto index = first; index <= last; ++index)
                    clipboardSelection.insert(clipboardRecords[index].id);
            }
        } else if (!control)
            clipboardSelection.clear();
        if (control && clipboardSelection.contains(item))
            clipboardSelection.erase(item);
        else
            clipboardSelection.insert(item);
        clipboardSelected = item;
        if (moduleRegistry.Enabled(L"clipboard"))
            moduleRegistry.Invoke(L"clipboard.preview-" + std::to_wstring(item), window);
        return;
    }
    if (id == 8755) {
        if (renderMode || previewMode || fixtureGeometry || !clipboardSelected)
            return;
        if (!::clipboard::PasteTarget(clipboardPasteTarget)) {
            note = L"Paste supports standard Windows edit fields. Use Copy, then paste in your editor.";
            InvalidateRect(window, nullptr, FALSE);
            return;
        }
        auto destination = clipboardPasteTarget;
        SetExpanded(false);
        if (SetForegroundWindow(GetAncestor(destination, GA_ROOT)))
            moduleRegistry.Invoke(L"clipboard.paste-" + std::to_wstring(clipboardSelected), destination);
        return;
    }
    if (id < 8750 || id > 8752 || !clipboardSelected)
        return;
    auto selected = clipboardSelection;
    if (selected.empty())
        selected.insert(clipboardSelected);
    for (auto item : selected) {
        auto prefix = id == 8750 ? L"clipboard.copy-" : id == 8751 ? L"clipboard.pin-" : L"clipboard.remove-";
        moduleRegistry.Invoke(prefix + std::to_wstring(item), window);
        if (id == 8750)
            break;
    }
    if (id == 8752) {
        clipboardSelected = 0;
        clipboardSelection.clear();
    }
}
void Island::ClipboardView() {
    auto b = Body();
    b.bottom = b.top + Layout().contentHeight;
    bool inlineFilters = b.right - b.left >= 640;
    float searchRight = b.right - (inlineFilters ? 296.0f : 0.0f);
    Fill({b.left, b.top, b.right, b.top + 40}, theme::Inset, shape::Control);
    auto search = ClipboardRect();
    if (b.right - b.left >= 160)
        Text(L"\xE721", {b.left + 8, b.top, b.left + 28, b.top + 40}, type::Icon, theme::Muted);
    AddHit(403, search, L"Search clipboard history");
    FocusRing(403, {b.left, b.top, searchRight, b.top + 40});
    if (!clipboardQuery.empty())
        SmallButton(9501, {searchRight - 36, b.top + 4, searchRight - 4, b.top + 36}, L"\xE711",
                    L"Clear history search");
    if (renderMode)
        Text(clipboardQuery.empty() ? L"Search history…" : clipboardQuery, search, type::Body, theme::Muted);
    EnsureClipboardEdit();
    const wchar_t *filters[]{L"All", L"Text", L"Images", L"Links"};
    int columns = std::max(1, std::min(5, static_cast<int>((b.right - b.left) / 72)));
    float filterLeft = inlineFilters ? b.right - 288 : b.left;
    float filterTop = inlineFilters ? b.top + 4 : b.top + 48;
    float chip = inlineFilters ? 56.0f : (b.right - b.left) / columns;
    for (int i = 0; i < 4; ++i)
        TextButton(8700 + i,
                   {filterLeft + (i % columns) * chip, filterTop + (i / columns) * 40,
                    filterLeft + (i % columns + 1) * chip - 4, filterTop + 32 + (i / columns) * 40},
                   filters[i], clipboardFilter == i);
    SmallButton(8791,
                {filterLeft + (4 % columns) * chip, filterTop + (4 / columns) * 40,
                 filterLeft + (4 % columns) * chip + 32, filterTop + 32 + (4 / columns) * 40},
                L"\xE713", L"Clipboard settings");
    float listTop = inlineFilters ? b.top + 48 : b.top + 48 + ((5 + columns - 1) / columns) * 40;
    int actionColumns = std::max(1, std::min(4, static_cast<int>((b.right - b.left) / 112)));
    int actionCount = !moduleRegistry.Enabled(L"clipboard") ? 1 : b.right - b.left > 600 ? 4 : 3;
    float actionHeight = static_cast<float>((actionCount + actionColumns - 1) / actionColumns) * 40;
    clipboardRecords = moduleRegistry.ClipboardRecords();
    if ((renderMode || fixtureGeometry) && clipboardRecords.empty()) {
        clipboard::Item item;
        item.id = 1;
        item.text =
            clipboard.empty() ? L"Your copied text appears here. Pin useful items and search locally." : clipboard;
        item.kind = clipboard::Kind::Text;
        item.pinned = true;
        clipboardRecords.push_back(item);
    }
    std::erase_if(clipboardRecords, [&](const auto &item) {
        bool link = item.text.starts_with(L"https://") || item.text.starts_with(L"http://");
        return (!clipboardQuery.empty() &&
                FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE, item.text.c_str(), -1,
                                clipboardQuery.c_str(), -1, nullptr, nullptr, nullptr, 0) < 0) ||
               (clipboardFilter == 1 && item.kind != clipboard::Kind::Text) ||
               (clipboardFilter == 2 && item.kind != clipboard::Kind::Image) || (clipboardFilter == 3 && !link);
    });
    std::stable_sort(clipboardRecords.begin(), clipboardRecords.end(), [](const auto &a, const auto &c) {
        return a.pinned != c.pinned ? a.pinned : a.captured > c.captured;
    });
    clipboardOffset = std::min(clipboardOffset, clipboardRecords.empty() ? size_t{0} : clipboardRecords.size() - 1);
    if (std::none_of(clipboardRecords.begin(), clipboardRecords.end(),
                     [&](const auto &entry) { return entry.id == clipboardSelected; }))
        clipboardSelected = 0;
    if (!clipboardSelected && !clipboardRecords.empty())
        clipboardSelected = clipboardRecords.front().id;
    bool wide = LayoutWidth() >= 640;
    float split = wide ? b.left + (b.right - b.left) * .47f : b.right;
    float y = listTop;
    int previousGroup = -1;
    for (size_t i = clipboardOffset; i < clipboardRecords.size() && y + 64 < b.bottom - actionHeight; ++i) {
        const auto &item = clipboardRecords[i];
        auto tick = GetTickCount64() + clipboard::MaxRetentionMs;
        uint64_t age = item.captured && tick >= item.captured ? tick - item.captured : 0;
        SYSTEMTIME local{};
        GetLocalTime(&local);
        auto sinceMidnight = (static_cast<uint64_t>(local.wHour) * 3600 + local.wMinute * 60 + local.wSecond) * 1000;
        int group = item.pinned ? 0 : age > sinceMidnight ? 2 : 1;
        if (group != previousGroup) {
            previousGroup = group;
            Text(group == 0   ? L"PINNED"
                 : group == 1 ? L"TODAY"
                              : L"YESTERDAY",
                 {b.left + 8, y, split - 8, y + 20}, type::Overline, theme::Muted);
            y += 24;
        }
        D2D1_RECT_F row{b.left, y, split - 12, y + 40};
        int id = 8600 + static_cast<int>(i);
        bool selected = item.id == clipboardSelected || clipboardSelection.contains(item.id);
        Fill(row, dock::Mix(selected ? theme::AccentSoft : theme::Surface, theme::CardHover, HoverAmount(id)),
             shape::Control);
        AddHit(8600 + static_cast<int>(i), row, (item.pinned ? L"Pinned. " : L"") + item.text);
        Text(item.kind == clipboard::Kind::Image ? L"\xEB9F"
             : item.pinned                       ? L"\xE718"
                                                 : L"\xE8A5",
             {row.left + 8, y, row.left + 28, y + 40}, type::Icon, theme::Muted);
        Text(item.kind == clipboard::Kind::Image ? L"Image" : item.text, {row.left + 36, y, row.right - 46, y + 40},
             type::Body);
        Text(age < 60000     ? L"now"
             : age < 3600000 ? std::to_wstring(age / 60000) + L"m"
                             : std::to_wstring(age / 3600000) + L"h",
             {row.right - 44, y, row.right - 6, y + 40}, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        FocusRing(8600 + static_cast<int>(i), row, shape::Control);
        y += 48;
    }
    auto item = std::find_if(clipboardRecords.begin(), clipboardRecords.end(),
                             [&](const auto &entry) { return entry.id == clipboardSelected; });
    if (wide) {
        D2D1_RECT_F preview{split, listTop, b.right, b.bottom - actionHeight - 8};
        Card(preview);
        if (item != clipboardRecords.end()) {
            auto panel = item->kind == clipboard::Kind::Image ? moduleRegistry.View(L"clipboard", L"") : std::nullopt;
            if (panel && panel->image && !layoutOnly) {
                const auto &image = *panel->image;
                ComPtr<ID2D1Bitmap> bitmap;
                auto properties =
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(image.width, image.height), image.bgra.data(),
                                                   image.width * 4, properties, &bitmap))) {
                    float scale = std::min((preview.right - preview.left - 32) / image.width,
                                           (preview.bottom - preview.top - 84) / image.height);
                    target->DrawBitmap(bitmap.Get(),
                                       {preview.left + 16, preview.top + 16, preview.left + 16 + image.width * scale,
                                        preview.top + 16 + image.height * scale});
                }
            } else
                Paragraph(item->text, {preview.left + 16, preview.top + 16, preview.right - 16, preview.bottom - 88},
                          theme::Text, type::Label);
            auto tick = GetTickCount64() + clipboard::MaxRetentionMs;
            auto age = item->captured && tick >= item->captured ? tick - item->captured : 0;
            std::wstring kind = item->kind == clipboard::Kind::Image   ? L"Image"
                                : item->kind == clipboard::Kind::Files ? L"Files"
                                                                       : L"Text";
            std::wstring size =
                item->kind == clipboard::Kind::Image
                    ? item->text + L" px"
                    : std::to_wstring(item->text.size()) + (item->text.size() == 4096 ? L"+ chars" : L" chars");
            Line(preview.left + 16, preview.bottom - 80, preview.right - 16, preview.bottom - 80, theme::Border);
            Text(L"Type     " + kind, {preview.left + 16, preview.bottom - 72, preview.right - 16, preview.bottom - 52},
                 type::Caption, theme::Muted);
            Text(L"Size     " + size, {preview.left + 16, preview.bottom - 52, preview.right - 16, preview.bottom - 32},
                 type::Caption, theme::Muted);
            Text(L"Copied   " +
                     (age < 60000 ? std::wstring(L"Just now") : std::to_wstring(age / 60000) + L" minutes ago"),
                 {preview.left + 16, preview.bottom - 32, preview.right - 16, preview.bottom - 8}, type::Caption,
                 theme::Muted);
        }
    }
    if (clipboardRecords.empty())
        EmptyState({b.left, listTop, b.right, b.bottom - actionHeight}, L"\xE8A5", L"No matching history",
                   moduleRegistry.Enabled(L"clipboard") ? L"New copies appear here."
                                                        : L"Enable history to capture new copies.");
    std::vector<std::pair<int, std::wstring>> actions;
    if (!moduleRegistry.Enabled(L"clipboard"))
        actions.emplace_back(8790, L"Enable history");
    else {
        actions = {{8750, L"Ctrl C · Copy"}, {8751, L"Ctrl P · Pin"}, {8752, L"Del · Remove"}};
        if (b.right - b.left > 600)
            actions.insert(actions.begin(), {8755, L"Paste"});
    }
    float actionWidth = std::min(128.0f, (b.right - b.left) / actionColumns);
    for (size_t i = 0; i < actions.size(); ++i) {
        float x = b.left + static_cast<float>(i % actionColumns) * actionWidth;
        float top = b.bottom - actionHeight + static_cast<float>(i / actionColumns) * 40;
        D2D1_RECT_F action{x, top, x + actionWidth - 4, top + 32};
        int id = actions[i].first;
        AddHit(id, action, actions[i].second);
        Fill(Tactile(id, action), dock::Mix(theme::Surface, theme::CardHover, HoverAmount(id)), shape::Chip);
        Text(actions[i].second, action, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(id, action);
    }
}
