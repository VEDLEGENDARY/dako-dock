#include "Island.h"

std::array<POINT, 35> Island::NotchPixels(float scale) const {
    auto curve = dock::Notch(renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2, HandleY(), theme::DockWidth,
                             theme::DockHeight);
    curve = anchor::ForEdge(edge, curve, HandleY(), theme::DockHeight);
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
    auto outline = dock::Attached((envelopeWidth - width) / 2, PanelY(), width, height, theme::Radius);
    outline = anchor::ForEdge(edge, outline, PanelY(), height);
    std::array<POINT, 69> points{};
    for (size_t i = 0; i < points.size(); ++i)
        points[i] = {static_cast<LONG>(std::lround(outline[i].x * scale)),
                     static_cast<LONG>(std::lround(outline[i].y * scale))};
    return points;
}

void Island::IslandSurface() {
    auto outline = anchor::ForEdge(
        edge, dock::Attached((envelopeWidth - width) / 2, PanelY(), width, height, theme::Radius), PanelY(), height);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
        return;
    sink->BeginFigure({outline[0].x, outline[0].y}, D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < outline.size(); ++i)
        sink->AddLine({outline[i].x, outline[i].y});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    brush->SetColor(Color(theme::Surface));
    target->FillGeometry(path.Get(), brush.Get());
    brush->SetColor(Color(theme::Border));
    target->DrawGeometry(path.Get(), brush.Get(), highContrast ? 2.0f : 1.0f);
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

D2D1_COLOR_F Island::Color(unsigned rgb, float alpha) { return D2D1::ColorF(rgb, alpha); }

void Island::Fill(D2D1_RECT_F rect, unsigned color, float radius) {
    if (layoutOnly)
        return;
    if (rect.bottom - rect.top >= 48 && rect.right - rect.left >= 60)
        radius = std::min(radius, 10.0f);
    brush->SetColor(Color(color));
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
    if (radius >= 10 && rect.bottom - rect.top >= 32 && rect.right - rect.left >= 60) {
        brush->SetColor(Color(theme::Border, highContrast ? 1.0f : 0.65f));
        D2D1_RECT_F outlineEdge{rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f};
        target->DrawRoundedRectangle(D2D1::RoundedRect(outlineEdge, radius, radius), brush.Get(), 1);
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
         dock::Mix(dock::Mix(theme::Card, theme::Accent, state->second.value), theme::CardHover, HoverAmount(id)),
         layout::CardRadius);
    Text(icon, {rect.left + 12, rect.top + 12, rect.left + 40, rect.bottom - 12}, type::Icon,
         active ? theme::AccentInk : theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(title, {rect.left + 50, rect.top + 7, rect.right - 26, rect.top + 30}, type::Control,
         active    ? theme::AccentInk
         : enabled ? theme::Text
                   : theme::Muted);
    Text(subtitle, {rect.left + 50, rect.top + 30, rect.right - 26, rect.bottom - 6}, type::Secondary,
         active ? theme::AccentInk : theme::Muted);
    Text(L"\xE76C", {rect.right - 24, rect.top + 15, rect.right - 8, rect.bottom - 15}, type::Icon,
         active ? theme::AccentInk : theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
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
    constexpr const wchar_t *labels[]{L"Controls", L"Today", L"Stats", L"Clipboard", L"Apps", L"Wallpaper", L"Power"};
    for (Tab primary : EnabledTabs()) {
        int index = static_cast<int>(primary);
        auto rect = TabRect(index);
        if (primary == tab)
            Fill(rect, theme::AccentSoft, 14);
        AddHit(100 + index, rect, labels[index]);
        Text(labels[index], rect, type::Caption, primary == tab ? theme::Accent : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(100 + index, rect, 14);
    }
    AddHit(2002, {0, 0, 1, 1}, L"Battery status is available through Today", false);
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
