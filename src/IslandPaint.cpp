#include "Island.h"

std::vector<POINT> Island::NotchPixels(float scale) const {
    float x = renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2;
    auto convert = [&](auto curve) {
        curve = anchor::ForEdge(edge, curve, HandleY(), theme::DockHeight);
        std::vector<POINT> points;
        for (auto p : curve)
            points.push_back({static_cast<LONG>(std::lround(p.x * scale)),
                              static_cast<LONG>(std::lround(p.y * scale))});
        return points;
    };
    return dpi > 144 ? convert(dock::Notch<24>(x, HandleY(), theme::DockWidth, theme::DockHeight))
                     : convert(dock::Notch(x, HandleY(), theme::DockWidth, theme::DockHeight));
}

void Island::PaintNotch(HDC dc) {
    if (YieldResting())
        return;
    auto points = NotchPixels(dpi / 96);
    HBRUSH fill = CreateSolidBrush(RGB((theme::Surface >> 16) & 255, (theme::Surface >> 8) & 255,
                                     theme::Surface & 255));
    auto oldBrush = SelectObject(dc, fill);
    auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, points.data(), static_cast<int>(points.size()));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(fill);
}

void Island::DrawNotch() {
    float x = renderMode ? 0 : (envelopeWidth - theme::DockWidth) / 2;
    auto *path = geometryCache.Surface(factory.Get(), true, x, HandleY(), theme::DockWidth, theme::DockHeight,
                                       0, edge);
    if (path) {
        brush->SetColor(Color(theme::Surface));
        target->FillGeometry(path, brush.Get());
    }
}

std::vector<POINT> Island::AttachedPixels(float scale) const {
    float x = (envelopeWidth - width) / 2;
    auto convert = [&](auto curve) {
        curve = anchor::ForEdge(edge, curve, PanelY(), height);
        std::vector<POINT> points;
        for (auto p : curve)
            points.push_back({static_cast<LONG>(std::lround(p.x * scale)),
                              static_cast<LONG>(std::lround(p.y * scale))});
        return points;
    };
    return dpi > 144 ? convert(dock::Attached<24>(x, PanelY(), width, height, theme::Radius))
                     : convert(dock::Attached(x, PanelY(), width, height, theme::Radius));
}

void Island::IslandSurface() {
    auto *path = geometryCache.Surface(factory.Get(), false, 0, 0, width, height, theme::Radius, edge);
    if (!path)
        return;
    float pixel = 96 / dpi, x = std::round((envelopeWidth - width) / 2 / pixel) * pixel;
    float y = std::round(PanelY() / pixel) * pixel;
    D2D1::Matrix3x2F saved;
    target->GetTransform(&saved);
    target->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * saved);
    brush->SetColor(Color(theme::Surface, backdropAvailable && !opaqueMaterial && !highContrast ? .94f : 1.0f));
    target->FillGeometry(path, brush.Get());
    target->SetTransform(D2D1::Matrix3x2F::Scale((width - pixel) / width, (height - pixel) / height) *
                         D2D1::Matrix3x2F::Translation(x + pixel / 2, y + pixel / 2) * saved);
    brush->SetColor(Color(highContrast ? theme::Border : theme::Edge,
                          highContrast ? 1.0f : palette::Tokens::HairlineOuter));
    target->DrawGeometry(path, brush.Get(), highContrast ? 2.0f : pixel);
    target->SetTransform(saved);
    if (!highContrast) {
        float freeY = edge == anchor::Edge::Bottom ? y + pixel * 1.5f : y + height - pixel * 1.5f;
        brush->SetColor(Color(theme::Edge, palette::Tokens::HairlineInner));
        target->DrawLine({x + theme::Radius, freeY}, {x + width - theme::Radius, freeY}, brush.Get(), pixel);
    }
}

void Island::PositionShadow() {
    if (renderMode) {
        shadow.Hide();
        return;
    }
    RECT host{};
    GetWindowRect(window, &host);
    float scale = dpi / 96, w = SurfaceWidth(), h = SurfaceHeight();
    float top = edge == anchor::Edge::Bottom ? PanelY() + height - h : PanelY();
    RECT panel{host.left + static_cast<LONG>(std::lround((envelopeWidth - w) / 2 * scale)),
                host.top + static_cast<LONG>(std::lround(top * scale)),
                host.left + static_cast<LONG>(std::lround((envelopeWidth + w) / 2 * scale)),
                host.top + static_cast<LONG>(std::lround((top + h) * scale))};
    MONITORINFO monitor{sizeof(monitor)};
    bool fits = GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
                monitor.rcMonitor.right - monitor.rcMonitor.left >= (w + 96) * scale &&
                monitor.rcMonitor.bottom - monitor.rcMonitor.top >= (h + 96) * scale;
    shadow.Position(backdropWindow && IsWindowVisible(backdropWindow) ? backdropWindow : window, panel, dpi,
                     theme::Radius, edge, dockReveal.value,
                     fits && !highContrast && !opaqueMaterial && !fullscreenContext && dockReveal.value > 0);
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
    int shoulder = static_cast<int>(std::lround(24 * scale));
    int x = host.left + static_cast<int>(std::lround((envelopeWidth - width) * scale / 2)) - shoulder,
        y = host.top + static_cast<int>(std::lround(PanelY() * scale)),
        w = static_cast<int>(std::lround(width * scale)) + shoulder * 2,
        h = static_cast<int>(std::lround(height * scale));
    RECT next{x, y, x + w, y + h};
    if (EqualRect(&next, &backdropPlacement) && IsWindowVisible(backdropWindow))
        return;
    backdropPlacement = next;
    auto points = AttachedPixels(scale);
    int originX = x - host.left, originY = y - host.top;
    for (auto &point : points) {
        point.x -= originX;
        point.y -= originY;
    }
    HRGN region = CreatePolygonRgn(points.data(), static_cast<int>(points.size()), WINDING);
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
    brush->SetColor(Color(color));
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
    if (highContrast && rect.bottom - rect.top >= layout::Metrics::MinHit) {
        brush->SetColor(Color(theme::Border));
        target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get(), 2);
    }
}

void Island::Card(D2D1_RECT_F rect, int level) {
    Fill(rect, level == 1 ? theme::Card : theme::CardHover, layout::Metrics::CardRadius);
    if (layoutOnly || highContrast)
        return;
    float pixel = 96 / dpi / textScale;
    float y = (std::floor(rect.top / pixel) + .5f) * pixel;
    brush->SetColor(Color(theme::Edge, level == 1 ? palette::Tokens::HairlineInner : palette::Tokens::HairlineOuter));
    target->DrawLine({rect.left + layout::Metrics::CardRadius, y},
                     {rect.right - layout::Metrics::CardRadius, y}, brush.Get(), pixel);
}

void Island::Text(const std::wstring &text, D2D1_RECT_F rect, int format, unsigned color, DWRITE_TEXT_ALIGNMENT align) {
    if (layoutOnly || rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    float w = rect.right - rect.left, h = rect.bottom - rect.top;
    auto found = std::find_if(textLayouts.begin(), textLayouts.end(), [&](const TextEntry &entry) {
        return entry.text == text && entry.width == w && entry.height == h && entry.role == format &&
               entry.align == align;
    });
    if (found == textLayouts.end()) {
        TextEntry entry{text, w, h, format, align, nullptr};
        if (FAILED(textFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                                 formats[format].Get(), w, h, &entry.layout)))
            return;
        entry.layout->SetTextAlignment(align);
        bool glyph = format == type::Icon || format >= type::LargeIcon;
        if (typography && typography->numbers && !glyph)
            entry.layout->SetTypography(typography->numbers.Get(), {0, static_cast<UINT32>(text.size())});
        if (format == type::Overline) {
            ComPtr<IDWriteTextLayout1> modern;
            if (SUCCEEDED(entry.layout.As(&modern)))
                modern->SetCharacterSpacing(type::Sizes[format] * type::Tracking, 0, 0,
                                             {0, static_cast<UINT32>(text.size())});
        }
        if (textLayouts.size() >= 96)
            textLayouts.pop_front();
        textLayouts.push_back(std::move(entry));
        found = std::prev(textLayouts.end());
    } else if (std::next(found) != textLayouts.end()) {
        TextEntry entry = std::move(*found);
        textLayouts.erase(found);
        textLayouts.push_back(std::move(entry));
        found = std::prev(textLayouts.end());
    }
    brush->SetColor(Color(color));
    target->DrawTextLayout({rect.left, rect.top}, found->layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Island::TwoLine(D2D1_RECT_F rect, const std::wstring &title, const std::wstring &subtitle, unsigned color) {
    float titleHeight = type::Sizes[type::Title] * type::Leading;
    float captionHeight = subtitle.empty() ? 0 : type::Sizes[type::Caption] * type::Leading;
    float top = (rect.top + rect.bottom - titleHeight - captionHeight) / 2;
    Text(title, {rect.left, top, rect.right, top + titleHeight}, type::Title, color);
    if (!subtitle.empty())
        Text(subtitle, {rect.left, top + titleHeight, rect.right, top + titleHeight + captionHeight},
             type::Caption, theme::Muted);
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
         layout::ControlRadius);
    if (visual.right - visual.left < 128) {
        Text(icon, {visual.left, visual.top + 8, visual.right, visual.top + 40}, type::TileIcon,
             active ? theme::Accent : theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(title, {visual.left + 4, visual.top + 44, visual.right - 4, visual.top + 64}, type::Label,
             enabled ? theme::Text : theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(subtitle, {visual.left + 4, visual.top + 64, visual.right - 4, visual.bottom - 4},
             type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(id, rect);
        return;
    }
    D2D1_RECT_F circle{visual.left + 12, visual.top + 12, visual.left + 52, visual.top + 52};
    Fill(circle, dock::Mix(theme::CardHover, theme::Accent, state->second.value), 20);
    Text(icon, circle, type::TileIcon,
         active ? theme::AccentInk : dock::Mix(theme::Muted, theme::Text, HoverAmount(id)),
         DWRITE_TEXT_ALIGNMENT_CENTER);
    TwoLine({visual.left + 64, visual.top + 8, visual.right - 12, visual.top + 56}, title, subtitle,
            enabled ? theme::Text : theme::Muted);
    if (id == Wifi || id == Bluetooth || id == Audio)
        Text(L"\xE76C", {visual.right - 28, visual.bottom - 28, visual.right - 12, visual.bottom - 12},
             type::InlineIcon, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (id == Dark) {
        D2D1_RECT_F toggle{visual.left + 16, visual.bottom - 28, visual.left + 44, visual.bottom - 12};
        Fill(toggle, dock::Mix(theme::Inset, theme::Accent, state->second.value), 8);
        if (!layoutOnly) {
            brush->SetColor(Color(active ? theme::AccentInk : theme::Muted));
            target->FillEllipse(D2D1::Ellipse({toggle.left + 8 + 12 * state->second.value,
                                               (toggle.top + toggle.bottom) / 2}, 6, 6), brush.Get());
        }
    }
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
    thickness = std::clamp(radius * .25f, 6.0f, 10.0f);
    brush->SetColor(Color(theme::Inset));
    target->DrawEllipse(D2D1::Ellipse({x, y}, radius, radius), brush.Get(), thickness);
    value = std::clamp(value, 0.0f, 1.0f);
    if (value < .0025f)
        return;
    brush->SetColor(Color(color));
    if (value > .9975f) {
        target->DrawEllipse(D2D1::Ellipse({x, y}, radius, radius), brush.Get(), thickness);
        return;
    }
    auto *path = geometryCache.Arc(factory.Get(), radius, value);
    if (!path)
        return;
    if (!ringStroke)
        Check(factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                     D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                         nullptr, 0, &ringStroke));
    D2D1::Matrix3x2F saved;
    target->GetTransform(&saved);
    target->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * saved);
    target->DrawGeometry(path, brush.Get(), thickness, ringStroke.Get());
    target->SetTransform(saved);
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

void Island::PageContent(bool header) {
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
        TextButton(1901, {f.left, f.top, f.left + 86, f.bottom}, L"Menu");
        Text(L"\xE70D", {f.left + 62, f.top, f.left + 78, f.bottom}, type::InlineIcon, theme::Muted);
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
        if (f.right - f.left >= 560) {
            constexpr const wchar_t *fits[]{L"Center", L"Tile", L"Stretch", L"Fit", L"Fill", L"Span"};
            TextButton(WallpaperFit, {f.left + 192, f.top, f.left + 272, f.bottom},
                       fits[static_cast<int>(wallpaperFit)]);
            TextButton(WallpaperTarget, {f.left + 284, f.top, f.left + 396, f.bottom},
                       wallpaperTarget.empty() ? L"All displays" : L"Display");
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
}

void Island::DrawToast() {
    if (note.empty() || (!renderMode && !toastVisible && toastBlend.value <= 0))
        return;
    auto body = Body(), rect = R(Layout().status);
    rect.right = std::min(rect.right, rect.left + 520);
    for (const auto &hit : hits)
        if (hit.input && layout::Intersects({rect.left, rect.top, rect.right, rect.bottom},
                                           {hit.rect.left, hit.rect.top, hit.rect.right, hit.rect.bottom})) {
            rect.top = body.top;
            rect.bottom = rect.top + std::min(32.0f, body.bottom - body.top);
            break;
        }
    AddHit(1800, rect, note, false);
    if (layoutOnly)
        return;
    auto layer = D2D1::LayerParameters();
    layer.opacity = renderMode || highContrast || reducedMotion ? 1 : std::clamp(toastBlend.value, 0.0f, 1.0f);
    target->PushLayer(layer, nullptr);
    D2D1::Matrix3x2F saved;
    target->GetTransform(&saved);
    float offset = renderMode || highContrast || reducedMotion ? 0 :
                   -anchor::Grow(edge) * 8 * (1 - std::clamp(toastBlend.value, 0.0f, 1.0f));
    target->SetTransform(D2D1::Matrix3x2F::Translation(0, offset) * saved);
    Card(rect, 2);
    Text(note, {rect.left + 12, rect.top, rect.right - 12, rect.bottom}, type::Label, theme::Text);
    target->SetTransform(saved);
    target->PopLayer();
}

void Island::PanelContent(bool header) {
    PageContent(header);
    if (expanded && !miniMode)
        DrawToast();
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
