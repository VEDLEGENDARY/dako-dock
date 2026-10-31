#include "Island.h"

void Island::BeginChrome(ChromeKind kind, const std::wstring &title, const std::wstring &detail,
                         const std::wstring &initial, size_t limit) {
    if (ChromeActive())
        return;
    if (miniMode) {
        miniMode = false;
        RetargetLayout();
    }
    if (!expanded)
        SetExpanded(true);
    chrome = Chrome{};
    chrome.kind = kind;
    chrome.title = title;
    chrome.detail = detail;
    chrome.value = initial;
    chrome.primary = kind == ChromeKind::Input ? L"Save" : L"Continue";
    chrome.restoreFocus = focusedKey;
    chrome.savedModal = modalOpen;
    modalOpen = true;
    float sheetWidth = std::min(560.0f, Body().right - Body().left);
    ComPtr<IDWriteTextLayout> paragraph;
    if (!detail.empty() && SUCCEEDED(textFactory->CreateTextLayout(
                               detail.c_str(), static_cast<UINT32>(detail.size()), formats[type::Caption].Get(),
                               std::max(16.0f, sheetWidth - 32), 100000, &paragraph))) {
        paragraph->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        paragraph->GetMetrics(&metrics);
        chrome.detailHeight = std::max(32.0f, std::ceil(metrics.height));
    }
    chrome.height = std::max(kind == ChromeKind::Input ? 224.0f : 160.0f, 48 + chrome.detailHeight + 12 +
                                                                              (kind == ChromeKind::Input ? 56 : 0) +
                                                                              48 + (sheetWidth < 160 ? 36 : 0));
    if (kind == ChromeKind::Input && !renderMode) {
        if (!chromeEdit) {
            chromeEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1, window,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(ChromeInput)), nullptr, nullptr);
            EnsureEditResources();
            SendMessageW(chromeEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
        }
        LabelInput(chromeEdit, title.c_str());
        SendMessageW(chromeEdit, EM_SETLIMITTEXT, std::min(size_t{32768}, limit), 0);
        SetWindowTextW(chromeEdit, initial.c_str());
        SendMessageW(chromeEdit, EM_SETSEL, 0, -1);
    }
    chrome.blend.Snap(reducedMotion || renderMode || highContrast ? 1.0f : 0.0f);
    chrome.blend.goal = 1;
    focusedKey.clear();
    RebuildLayout();
    FocusAt(IndexForKey(StableKey(kind == ChromeKind::Input ? ChromeInput : ChromeCancel)));
    if (!renderMode) {
        SetForegroundWindow(window);
        PositionInputs();
    }
    ArmMotion();
    NotifyWinEvent(EVENT_SYSTEM_DIALOGSTART, window, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
    InvalidateRect(window, nullptr, FALSE);
}

void Island::FinishChrome(int result) {
    if (!ChromeActive() || chrome.closing)
        return;
    chrome.result = result;
    if (chrome.kind == ChromeKind::Input && result == chrome.accept && chromeEdit) {
        int length = GetWindowTextLengthW(chromeEdit);
        chrome.value.resize(static_cast<size_t>(length) + 1);
        GetWindowTextW(chromeEdit, chrome.value.data(), length + 1);
        chrome.value.resize(length);
    }
    chrome.closing = true;
    chrome.blend.goal = 0;
    if (reducedMotion || renderMode || highContrast) {
        chrome.blend.Snap(0);
        CloseChrome();
    } else
        ArmMotion();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::CloseChrome() {
    if (!ChromeActive())
        return;
    auto restore = chrome.restoreFocus;
    modalOpen = chrome.savedModal;
    chrome.kind = ChromeKind::None;
    chrome.closing = false;
    chrome.blend.Snap(0);
    if (chromeEdit) {
        ShowWindow(chromeEdit, SW_HIDE);
        SetWindowTextW(chromeEdit, L"");
    }
    if (IsWindow(window) && expanded) {
        focusedKey = restore;
        RebuildLayout();
        int index = IndexForKey(restore);
        if (index >= 0 && hits[index].enabled)
            FocusAt(index);
        else if (!renderMode)
            SetFocus(window);
        PositionInputs();
    }
    NotifyWinEvent(EVENT_SYSTEM_DIALOGEND, window, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
}

int Island::WaitChrome() {
    MSG message{};
    while (ChromeActive() && IsWindow(window)) {
        BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            chrome.result = chrome.cancel;
            CloseChrome();
            if (status == 0)
                PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (message.hwnd == chromeEdit && message.message == WM_KEYDOWN &&
            (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE || message.wParam == VK_TAB))
            Key(message.wParam);
        else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return chrome.result;
}

int Island::MessageBoxW(HWND owner, const wchar_t *text, const wchar_t *title, UINT flags) {
    (void)owner;
    bool yesNo = (flags & MB_TYPEMASK) == MB_YESNO;
    if (renderMode || ChromeActive())
        return yesNo ? IDNO : IDCANCEL;
    BeginChrome(ChromeKind::Confirm, title ? title : L"Dako", text ? text : L"");
    chrome.accept = yesNo ? IDYES : IDOK;
    chrome.cancel = yesNo ? IDNO : IDCANCEL;
    chrome.primary = yesNo ? L"Continue" : L"OK";
    if (chrome.detail.starts_with(L"Restart Windows")) {
        chrome.title = chrome.primary = L"Restart";
        chrome.danger = true;
    } else if (chrome.detail.starts_with(L"Shut down Windows")) {
        chrome.title = chrome.primary = L"Shut down";
        chrome.danger = true;
    } else if (chrome.detail.starts_with(L"Put this PC to sleep"))
        chrome.title = chrome.primary = L"Sleep";
    chrome.result = chrome.cancel;
    RebuildLayout();
    return WaitChrome();
}

std::optional<std::wstring> Island::PromptText(HWND owner, const std::wstring &title, const std::wstring &help,
                                               const std::wstring &initial, size_t limit) {
    (void)owner;
    if (renderMode || ChromeActive())
        return std::nullopt;
    BeginChrome(ChromeKind::Input, title, help, initial, limit);
    if (WaitChrome() != IDOK) {
        chrome.value.clear();
        return std::nullopt;
    }
    return std::move(chrome.value);
}

std::vector<Island::MenuEntry> Island::ReadMenu(HMENU menu) {
    std::vector<MenuEntry> entries;
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t label[1024]{};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU;
        info.dwTypeData = label;
        info.cch = static_cast<UINT>(std::size(label));
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info) || (info.fType & MFT_SEPARATOR))
            continue;
        std::wstring clean;
        for (const wchar_t *p = label; *p; ++p) {
            if (*p == L'&' && p[1] != L'&')
                continue;
            if (*p == L'&' && p[1] == L'&')
                ++p;
            clean += *p;
        }
        MenuEntry entry{clean,
                        static_cast<int>(info.wID),
                        (info.fState & (MFS_DISABLED | MFS_GRAYED)) == 0,
                        (info.fState & MFS_CHECKED) != 0,
                        {}};
        if (info.hSubMenu)
            entry.children = ReadMenu(info.hSubMenu);
        entries.push_back(std::move(entry));
    }
    return entries;
}

int Island::OpenMenu(std::vector<MenuEntry> entries, D2D1_POINT_2F anchorPoint, const std::wstring &title) {
    if (ChromeActive() || entries.empty() || renderMode)
        return 0;
    bool resting = !expanded;
    BeginChrome(ChromeKind::Menu, title, L"");
    chrome.cancel = chrome.result = 0;
    chrome.items = std::move(entries);
    chrome.anchor = anchorPoint;
    RebuildLayout();
    for (size_t i = 0; i < chrome.slots && i < chrome.items.size(); ++i)
        if (chrome.items[i].enabled) {
            FocusAt(IndexForKey(StableKey(ChromeRow + static_cast<int>(i))));
            break;
        }
    int result = WaitChrome();
    if (resting && expanded)
        SetExpanded(false);
    return result;
}

int Island::TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND owner, const RECT *exclude) {
    (void)flags;
    (void)reserved;
    (void)owner;
    (void)exclude;
    POINT point{x, y};
    ScreenToClient(window, &point);
    float scale = dpi / 96 * textScale;
    D2D1_POINT_2F anchorPoint{(point.x - PanelX() * dpi / 96) / scale, (point.y - PanelY() * dpi / 96) / scale};
    return OpenMenu(ReadMenu(menu), anchorPoint);
}

bool Island::ChromeAction(int id) {
    if (!ChromeActive())
        return false;
    if (chrome.closing)
        return true;
    if (id == ChromeCancel)
        FinishChrome(chrome.cancel);
    else if (id == ChromeAccept)
        FinishChrome(chrome.accept);
    else if (id == ChromeInput)
        FocusAt(IndexForKey(StableKey(ChromeInput)));
    else if (id == ChromeBack && !chrome.history.empty()) {
        chrome.title = std::move(chrome.history.back().first);
        chrome.items = std::move(chrome.history.back().second);
        chrome.history.pop_back();
        chrome.offset = 0;
    } else if (id == ChromePrevious)
        chrome.offset = chrome.offset >= chrome.slots ? chrome.offset - chrome.slots : 0;
    else if (id == ChromeNext)
        chrome.offset =
            std::min(chrome.offset + chrome.slots, chrome.items.empty() ? size_t{0} : chrome.items.size() - 1);
    else if (id >= ChromeRow && id < ChromeRow + static_cast<int>(chrome.slots)) {
        size_t index = chrome.offset + id - ChromeRow;
        if (index < chrome.items.size() && chrome.items[index].enabled) {
            auto entry = chrome.items[index];
            if (!entry.children.empty()) {
                chrome.history.emplace_back(chrome.title, std::move(chrome.items));
                chrome.title = entry.label;
                chrome.items = std::move(entry.children);
                chrome.offset = 0;
            } else
                FinishChrome(entry.command);
        }
    }
    RebuildLayout();
    PositionInputs();
    InvalidateRect(window, nullptr, FALSE);
    return true;
}

bool Island::ChromeKey(WPARAM key) {
    if (!ChromeActive())
        return false;
    if (key == VK_ESCAPE) {
        FinishChrome(chrome.cancel);
        return true;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        if (GetFocus() == chromeEdit && key == VK_SPACE)
            return false;
        int id = GetFocus() == chromeEdit                                  ? ChromeAccept
                 : focused >= 0 && focused < static_cast<int>(hits.size()) ? hits[focused].id
                                                                           : ChromeCancel;
        ChromeAction(id);
        return true;
    }
    if (key == VK_LEFT && chrome.kind == ChromeKind::Menu && !chrome.history.empty()) {
        ChromeAction(ChromeBack);
        return true;
    }
    if (key == VK_RIGHT && chrome.kind == ChromeKind::Menu && focused >= 0) {
        ChromeAction(hits[focused].id);
        return true;
    }
    if (chrome.kind == ChromeKind::Menu && !chrome.items.empty() &&
        (key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END)) {
        int direction = key == VK_UP || key == VK_END ? -1 : 1;
        int slot = focused >= 0 ? hits[focused].id - ChromeRow : -1;
        int index = slot >= 0 && slot < static_cast<int>(chrome.slots) ? static_cast<int>(chrome.offset) + slot
                                                                       : static_cast<int>(chrome.offset) - direction;
        if (key == VK_HOME)
            index = -1;
        if (key == VK_END)
            index = static_cast<int>(chrome.items.size());
        for (size_t i = 0; i < chrome.items.size(); ++i) {
            index = (index + direction + static_cast<int>(chrome.items.size())) % static_cast<int>(chrome.items.size());
            if (!chrome.items[index].enabled)
                continue;
            size_t selected = static_cast<size_t>(index);
            if (selected < chrome.offset)
                chrome.offset = selected;
            else if (selected >= chrome.offset + chrome.slots)
                chrome.offset = selected - chrome.slots + 1;
            RebuildLayout();
            FocusAt(IndexForKey(StableKey(ChromeRow + static_cast<int>(selected - chrome.offset))));
            break;
        }
        return true;
    }
    if (key == VK_TAB || key == VK_UP || key == VK_DOWN) {
        int direction = key == VK_UP || (key == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? -1 : 1;
        int index = focused;
        for (size_t i = 0; i < hits.size(); ++i) {
            index = (index + direction + static_cast<int>(hits.size())) % static_cast<int>(hits.size());
            if (hits[index].enabled) {
                FocusAt(index);
                break;
            }
        }
    }
    if (chrome.kind != ChromeKind::Menu && (key == VK_LEFT || key == VK_RIGHT))
        FocusAt(IndexForKey(StableKey(key == VK_LEFT ? ChromeCancel : ChromeAccept)));
    return true;
}

void Island::ScrollChrome(float amount) {
    if (!ChromeActive())
        return;
    if (chrome.kind == ChromeKind::Menu) {
        int next = static_cast<int>(chrome.offset) + (amount > 0 ? 1 : -1);
        chrome.offset = static_cast<size_t>(
            std::clamp(next, 0, std::max(0, static_cast<int>(chrome.items.size()) - static_cast<int>(chrome.slots))));
    } else {
        auto body = Body();
        chrome.scroll =
            std::clamp(chrome.scroll + amount, 0.0f, std::max(0.0f, chrome.height - (body.bottom - body.top)));
    }
    RebuildLayout();
    PositionInputs();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::DrawChrome() {
    auto body = Body();
    bool menu = chrome.kind == ChromeKind::Menu;
    float sheetWidth = std::min(menu ? 320.0f : 560.0f, body.right - body.left);
    float header = menu && body.bottom - body.top >= 108 ? 36.0f : 0;
    float footer = menu && body.bottom - body.top >= 144 ? 36.0f : 0;
    float rowHeight = std::min(36.0f, body.bottom - body.top);
    if (menu) {
        chrome.slots =
            static_cast<size_t>(std::max(1.0f, std::floor((body.bottom - body.top - header - footer) / rowHeight)));
        chrome.offset = std::min(chrome.offset, chrome.items.empty() ? size_t{0} : chrome.items.size() - 1);
        chrome.height = header + footer +
                        rowHeight * static_cast<float>(std::min(chrome.slots, chrome.items.size() - chrome.offset));
    }
    float left = menu ? std::clamp(chrome.anchor.x, body.left, body.right - sheetWidth)
                      : (body.left + body.right - sheetWidth) / 2;
    float top = edge == anchor::Edge::Bottom ? body.top : body.bottom - std::min(chrome.height, body.bottom - body.top);
    if (menu)
        top = std::clamp(chrome.anchor.y + (edge == anchor::Edge::Bottom ? -chrome.height : 0), body.top,
                         std::max(body.top, body.bottom - chrome.height));
    D2D1_RECT_F rect{left, top - chrome.scroll, left + sheetWidth, top - chrome.scroll + chrome.height};
    float opacity = highContrast ? 1.0f : std::clamp(chrome.blend.value, 0.0f, 1.0f);
    if (!layoutOnly) {
        brush->SetColor(Color(highContrast ? theme::Surface : palette::Tokens::Black, highContrast ? 1.0f
                                                                                      : menu       ? .12f
                                                                                                   : .4f));
        target->FillRectangle(body, brush.Get());
        target->PushAxisAlignedClip(body, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        auto layer = D2D1::LayerParameters();
        layer.opacity = opacity;
        if (opacity < 1)
            target->PushLayer(layer, nullptr);
    }
    D2D1::Matrix3x2F saved;
    if (!layoutOnly) {
        target->GetTransform(&saved);
        target->SetTransform(D2D1::Matrix3x2F::Translation(
                                 0, reducedMotion || highContrast
                                        ? 0
                                        : anchor::Grow(edge) * 8 * (1 - std::clamp(chrome.blend.value, 0.0f, 1.0f))) *
                             saved);
        if (!highContrast)
            depth.Paint(target.Get(), rect, dpi * textScale, shape::Surface, edge);
    }
    Card(rect, 2, 0, shape::Surface);
    if (menu) {
        if (header) {
            Text(chrome.title,
                 {rect.left + (chrome.history.empty() ? 12.0f : 44.0f), rect.top, rect.right - 40, rect.top + header},
                 type::Label);
            if (!chrome.history.empty())
                SmallButton(ChromeBack, {rect.left + 4, rect.top + 2, rect.left + 36, rect.top + 34}, L"\xE76B",
                            L"Back");
            SmallButton(ChromeCancel, {rect.right - 36, rect.top + 2, rect.right - 4, rect.top + 34}, L"\xE711",
                        L"Close menu");
        }
        for (size_t slot = 0; slot < chrome.slots && chrome.offset + slot < chrome.items.size(); ++slot) {
            const auto &entry = chrome.items[chrome.offset + slot];
            float y = rect.top + header + static_cast<float>(slot) * rowHeight;
            D2D1_RECT_F row{rect.left + 4, y, rect.right - 4, y + rowHeight};
            int id = ChromeRow + static_cast<int>(slot);
            AddHit(id, row, entry.label, entry.enabled);
            if (Hover(id) || focusedKey == StableKey(id))
                Fill(row, theme::AccentSoft, layout::ControlRadius);
            size_t tabStop = entry.label.find(L'\t');
            Text(entry.label.substr(0, tabStop), {row.left + 32, y, row.right - 28, y + rowHeight}, type::Body,
                 entry.enabled ? theme::Text : theme::Muted);
            if (entry.checked)
                Text(L"\xE73E", {row.left + 8, y, row.left + 24, y + rowHeight}, type::InlineIcon, theme::Text);
            if (!entry.children.empty())
                Text(L"\xE76C", {row.right - 24, y, row.right - 8, y + rowHeight}, type::InlineIcon, theme::Muted);
            FocusRing(id, row);
        }
        if (footer) {
            float y = rect.bottom - footer + 2;
            SmallButton(ChromePrevious, {rect.left + 4, y, rect.left + 36, y + 32}, L"\xE70E", L"Previous menu items",
                        chrome.offset > 0);
            SmallButton(ChromeNext, {rect.right - 36, y, rect.right - 4, y + 32}, L"\xE70D", L"Next menu items",
                        chrome.offset + chrome.slots < chrome.items.size());
        }
    } else {
        Text(chrome.title, {rect.left + 16, rect.top + 16, rect.right - 16, rect.top + 40}, type::Title);
        Paragraph(chrome.detail, {rect.left + 16, rect.top + 48, rect.right - 16, rect.top + 48 + chrome.detailHeight},
                  theme::Muted, type::Caption);
        AddHit(1800, rect, chrome.title + L". " + chrome.detail, false);
        if (chrome.kind == ChromeKind::Input) {
            float y = rect.top + 60 + chrome.detailHeight;
            D2D1_RECT_F well{rect.left + 16, y, rect.right - 16, y + 40};
            Fill(well, theme::Inset, layout::ControlRadius);
            float pad = std::max(0.0f, std::min(8.0f, (well.right - well.left - 32) / 2));
            D2D1_RECT_F input{well.left + pad, well.top + 4, well.right - pad, well.bottom - 4};
            AddHit(ChromeInput, input, chrome.title);
            if (renderMode)
                Text(chrome.value, input, type::Body);
            FocusRing(ChromeInput, well);
        }
        bool stacked = sheetWidth < 160;
        float y = rect.bottom - 48, mid = (rect.left + rect.right) / 2;
        D2D1_RECT_F cancel{rect.left + 16, stacked ? y - 36 : y, stacked ? rect.right - 16 : mid - 6,
                           stacked ? y - 4 : y + 32};
        D2D1_RECT_F accept{stacked ? rect.left + 16 : mid + 6, y, rect.right - 16, y + 32};
        TextButton(ChromeCancel, cancel, L"Cancel");
        TextButton(ChromeAccept, accept, chrome.primary, !chrome.danger);
        if (chrome.danger)
            Text(chrome.primary, accept, type::Label, highContrast ? theme::Text : theme::Danger,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    for (auto &hit : hits) {
        hit.clip = body;
        if (chrome.closing)
            hit.enabled = false;
    }
    if (!layoutOnly) {
        target->SetTransform(saved);
        if (opacity < 1)
            target->PopLayer();
        target->PopAxisAlignedClip();
    }
}
