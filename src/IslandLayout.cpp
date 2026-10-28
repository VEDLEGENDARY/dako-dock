#include "Island.h"

layout::Result Island::Layout() const {
    layout::Input input;
    input.availableWidth = std::max(100.0f, (envelopeWidth - 40) / textScale);
    input.availableHeight = (envelopeHeight - 24) / textScale;
    input.view = miniMode ? layout::View::Media : static_cast<layout::View>(tab);
    if (flyout != Flyout::None)
        input.view = flyout == Flyout::Features                              ? layout::View::Features
                     : flyout == Flyout::DockStatus                          ? layout::View::Status
                     : flyout == Flyout::Module                              ? layout::View::Module
                     : flyout == Flyout::Palette || flyout == Flyout::Recent ? layout::View::Palette
                                                                             : layout::View::Wireless;
    input.items = moduleRegistry.Descriptors().size();
    input.active = privacySnapshot.active.size();
    input.events = std::min(size_t{8}, privacySnapshot.events.size());
    input.page = featuresPage;
    input.offlineIntro = moduleRegistry.FirstRun() && privacy::Policy().Offline();
    input.image = modulePanel.image != nullptr;
    input.bottom = edge == anchor::Edge::Bottom;
    return layout::Solve(input);
}

D2D1_RECT_F Island::R(layout::Rect r) { return {r.left, r.top, r.right, r.bottom}; }

D2D1_RECT_F Island::Body() const { return R(Layout().body); }

D2D1_RECT_F Island::Footer() const {
    auto footer = R(Layout().footer);
    if (footer.bottom - footer.top >= 4) {
        footer.top += 2;
        footer.bottom -= 2;
    }
    return footer;
}

void Island::RebuildLayout() {
    moduleRegistry.Visible(expanded && flyout == Flyout::Module ? moduleView : L"");
    int previous = focused;
    bool hadFocus = !focusedKey.empty();
    bool saved = layoutOnly;
    layoutOnly = true;
    PanelContent();
    layoutOnly = saved;
#ifndef NDEBUG
    if (!miniMode && flyout == Flyout::None && expanded &&
        !layout::ValidCards(Layout(), static_cast<layout::View>(tab)))
        Check(E_UNEXPECTED);
#endif
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
    if (pressMotion.size() > 256) {
        for (auto it = pressMotion.begin(); it != pressMotion.end();)
            if (IndexForKey(it->first) < 0)
                it = pressMotion.erase(it);
            else
                ++it;
    }
    for (auto it = taskMotion.begin(); it != taskMotion.end();)
        if (std::none_of(tasks.begin(), tasks.end(), [&](const Task &task) { return task.identity == it->first; }))
            it = taskMotion.erase(it);
        else
            ++it;
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
        KillTimer(window, ToastTimer);
        toastVisible = !note.empty();
        toastBlend.goal = toastVisible ? 1.0f : 0.0f;
        if (renderMode || reducedMotion || highContrast)
            toastBlend.Snap(toastBlend.goal);
        else
            ArmMotion();
        if (toastVisible && !renderMode && expanded)
            SetTimer(window, ToastTimer, 3800, nullptr);
        LONG child = CHILDID_SELF;
        for (const auto &hit : hits)
            if (hit.id == 1800 || hit.id == 2040)
                child = hit.child;
        NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, window, OBJID_CLIENT, child);
    }
}

void Island::RefreshGeometry() {
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

float Island::LayoutWidth() const { return Layout().panel.Width(); }

float Island::LayoutHeight() const { return Layout().panel.Height(); }

float Island::SurfaceWidth() const { return LayoutWidth() * textScale; }

float Island::SurfaceHeight() const { return LayoutHeight() * textScale; }

float Island::ContentWidth() const { return LayoutWidth(); }

float Island::PanelX() const { return renderMode ? 0 : (envelopeWidth - SurfaceWidth()) / 2; }

float Island::HandleY() const { return edge == anchor::Edge::Bottom ? envelopeHeight - theme::DockHeight : 0; }

float Island::PanelY() const {
    return renderMode                     ? 0
           : DockEnabled()                ? (edge == anchor::Edge::Top ? 0.0f : std::max(0.0f, envelopeHeight - height))
           : browserContext               ? std::min(browserClearance, std::max(12.0f, envelopeHeight - 272.0f))
           : edge == anchor::Edge::Bottom ? std::max(0.0f, envelopeHeight - height)
                                          : 12.0f;
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
            std::min(theme::EnvelopeHeight,
                     std::max(80.0f, (edge == anchor::Edge::Bottom ? barBounds.top - info.rcMonitor.top
                                                                   : info.rcMonitor.bottom - barBounds.bottom) /
                                             scale -
                                         8));
    const int w = static_cast<int>(std::lround((renderMode ? width : envelopeWidth) * scale));
    const int h = static_cast<int>(std::lround((renderMode ? height : envelopeHeight) * scale));
    RECT bounds{};
    GetWindowRect(window, &bounds);
    int left = info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - w) / 2;
    if (anchored && dockAnchor.x)
        left = std::clamp(dockAnchor.x - w / 2, info.rcMonitor.left,
                          std::max(info.rcMonitor.left, info.rcMonitor.right - w));
    int top = anchored ? (edge == anchor::Edge::Bottom ? barBounds.top - h + static_cast<int>(4 * scale)
                                                       : barBounds.bottom - static_cast<int>(4 * scale))
                       : static_cast<int>(std::lround(anchor::EdgeTop(edge, static_cast<float>(info.rcMonitor.top),
                                                                      static_cast<float>(info.rcMonitor.bottom),
                                                                      static_cast<float>(h))));
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
            auto polygon = AttachedPixels(scale);
            HRGN body = CreatePolygonRgn(polygon.data(), static_cast<int>(polygon.size()), WINDING);
            CombineRgn(shape, shape, body, RGN_OR);
            DeleteObject(body);
        }
        if (!SetWindowRgn(window, shape, TRUE))
            DeleteObject(shape);
    }
    hotZone.Place(window, !expanded && !fullscreenContext, static_cast<UINT>(dpi), edge);
    PositionBackdrop();
    PositionShadow();
    PositionInputs();
}

void Island::PositionInputs() {
    if (layoutOnly)
        return;
    bool visible = expanded && !capturingPane && (reducedMotion || renderMode || contentBlend.value > .98f);
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
            LabelInput(appSearch, flyout == Flyout::Palette ? modules::String(IDS_PALETTE_NAME).c_str()
                                                            : L"Search installed apps");
        }
    }
    position(taskEdit, 400);
    position(appSearch, 401);
    position(wifiPassword, 402);
    position(clipboardEdit, 403);
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

D2D1_RECT_F Island::TabRect(int index) const {
    auto l = Layout();
    const auto &tabs = visibleTabs.empty() ? std::vector<Tab>(PrimaryTabs.begin(), PrimaryTabs.end()) : visibleTabs;
    auto found = std::find(tabs.begin(), tabs.end(), static_cast<Tab>(index));
    size_t slot = found == tabs.end() ? 0 : static_cast<size_t>(found - tabs.begin());
    float slotWidth = (l.header.Width() - 4) / static_cast<float>(tabs.size());
    float left = l.header.left + 2 + slotWidth * static_cast<float>(slot);
    return {left, l.header.top + 4, left + slotWidth, l.header.bottom - 4};
}
