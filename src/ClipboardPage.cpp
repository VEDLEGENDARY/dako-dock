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
    return {b.left + 12, b.top + 4, b.right - 260, b.top + 34};
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
    Fill({b.left, b.top, b.right, b.top + 38}, theme::Card, 14);
    auto search = ClipboardRect();
    AddHit(403, search, L"Search clipboard history");
    if (renderMode)
        Text(L"Search history…", search, type::Caption, theme::Muted);
    EnsureClipboardEdit();
    const wchar_t *filters[]{L"All", L"Text", L"Images", L"Links"};
    float chip = (b.right - b.left >= 600 ? 56.0f : b.right - b.left >= 480 ? 48.0f : 36.0f);
    for (int i = 0; i < 4; ++i)
        TextButton(
            8700 + i,
            {b.right - 36 - 4 * chip + i * chip, b.top + 4, b.right - 36 - 4 * chip + (i + 1) * chip - 3, b.top + 34},
            i == 2 && chip < 56 ? L"Img" : filters[i], clipboardFilter == i);
    SmallButton(8791, {b.right - 32, b.top + 4, b.right, b.top + 34}, L"\xE713", L"Clipboard settings");
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
    float y = b.top + 48;
    int previousGroup = -1;
    for (size_t i = clipboardOffset; i < clipboardRecords.size() && y + 56 < b.bottom - 38; ++i) {
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
                 {b.left + 8, y, split - 8, y + 20}, type::Caption, theme::Muted);
            y += 24;
        }
        D2D1_RECT_F row{b.left, y, split - 12, y + 32};
        if (item.id == clipboardSelected || clipboardSelection.contains(item.id))
            Fill(row, theme::AccentSoft, 9);
        AddHit(8600 + static_cast<int>(i), row, (item.pinned ? L"Pinned. " : L"") + item.text);
        Text(item.kind == clipboard::Kind::Image ? L"▧"
             : item.pinned                       ? L"⌖"
                                                 : L"≡",
             {row.left + 8, y, row.left + 32, y + 32}, type::Caption, theme::Accent);
        Text(item.kind == clipboard::Kind::Image ? L"Image" : item.text, {row.left + 38, y, row.right - 46, y + 32},
             type::Caption);
        Text(age < 60000     ? L"now"
             : age < 3600000 ? std::to_wstring(age / 60000) + L"m"
                             : std::to_wstring(age / 3600000) + L"h",
             {row.right - 44, y, row.right - 6, y + 32}, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        FocusRing(8600 + static_cast<int>(i), row, 9);
        y += 38;
    }
    auto item = std::find_if(clipboardRecords.begin(), clipboardRecords.end(),
                             [&](const auto &entry) { return entry.id == clipboardSelected; });
    if (wide) {
        D2D1_RECT_F preview{split, b.top + 48, b.right, b.bottom - 40};
        Fill(preview, theme::Card, 16);
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
                Paragraph(item->text, {preview.left + 16, preview.top + 16, preview.right - 16, preview.bottom - 68});
            auto tick = GetTickCount64() + clipboard::MaxRetentionMs;
            auto age = item->captured && tick >= item->captured ? tick - item->captured : 0;
            std::wstring kind = item->kind == clipboard::Kind::Image   ? L"Image"
                                : item->kind == clipboard::Kind::Files ? L"Files"
                                                                       : L"Text";
            std::wstring size =
                item->kind == clipboard::Kind::Image
                    ? item->text + L" px"
                    : std::to_wstring(item->text.size()) + (item->text.size() == 4096 ? L"+ chars" : L" chars");
            Text(L"Type   " + kind + L" · Size   " + size,
                 {preview.left + 16, preview.bottom - 56, preview.right - 16, preview.bottom - 32}, type::Caption,
                 theme::Muted);
            Text(L"Copied   " +
                     (age < 60000 ? std::wstring(L"Just now") : std::to_wstring(age / 60000) + L" minutes ago"),
                 {preview.left + 16, preview.bottom - 32, preview.right - 16, preview.bottom - 8}, type::Caption,
                 theme::Muted);
        }
    }
    if (clipboardRecords.empty())
        Paragraph(moduleRegistry.Enabled(L"clipboard")
                      ? L"No matching history. New copies appear here while history is enabled."
                      : L"Clipboard history is optional. Enable it to capture new copies.",
                  {b.left + 12, b.top + 64, b.right - 12, b.top + 138});
    float bottom = b.bottom - 32;
    if (!moduleRegistry.Enabled(L"clipboard"))
        TextButton(8790, {b.left, bottom, b.left + 170, b.bottom}, L"Enable history");
    else {
        TextButton(8750, {b.left, bottom, b.left + 90, b.bottom}, L"Copy Ctrl C");
        TextButton(8751, {b.left + 98, bottom, b.left + 180, b.bottom}, L"Pin Ctrl P");
        TextButton(8752, {b.left + 188, bottom, b.left + 280, b.bottom}, L"Remove Del");
        if (b.right - b.left > 600)
            TextButton(8755, {b.left + 288, bottom, b.left + 390, b.bottom}, L"Paste Enter");
    }
    TextButton(8753, {b.right - 92, bottom, b.right - 50, b.bottom}, L"↑");
    TextButton(8754, {b.right - 44, bottom, b.right, b.bottom}, L"↓");
}
