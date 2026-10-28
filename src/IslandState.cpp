#include "Island.h"
#include "ClipboardPaste.h"

void Island::SnapMotion() {
    if (!expanded) {
        dockReveal.goal = contentBlend.goal = 0;
        dockWidth.goal = theme::DockWidth;
        dockHeight.goal = theme::DockHeight;
    } else
        contentBlend.goal = 1;
    collapsePending = false;
    revealClock = paneClock = 1;
    for (auto *v : {&dockReveal, &dockWidth, &dockHeight, &paneBlend, &wallpaperFocus, &tabLeft, &tabRight,
                    &railPointer, &railStrength, &paletteBlend, &contentBlend, &togglePosition, &wallpaperParallax,
                    &volumeMotion, &composerFocus, &toastBlend, &focusBlend})
        v->Snap(v->goal);
    for (auto &v : rings)
        v.Snap(v.goal);
    for (auto &[key, v] : hoverMotion) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : tileState) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : pressMotion) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : taskMotion) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : feedback) {
        (void)key;
        v.Snap(v.goal);
    }
    for (auto &[key, v] : rowPositions) {
        (void)key;
        v.Snap(v.goal);
    }
    previousPane.Reset();
    frameClock.Stop();
    frameTickReady = false;
    scheduler.Stop();
    KillTimer(window, TransitionTimer);
    transitioning = false;
    width = dockWidth.value;
    height = dockHeight.value;
    Place();
    if (!expanded && !renderMode)
        ReleaseGraphics();
}

bool Island::YieldResting() const { return DockEnabled() || fullscreenContext; }

void Island::UpdateContext() {
    if (renderMode)
        return;
    if (DockEnabled()) {
        browserContext = false;
        fullscreenContext = false;
        if (docks)
            docks->RefreshFullscreen();
        return;
    }
    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!foreground || processId == GetCurrentProcessId() || GetWindow(foreground, GW_OWNER) == window ||
        desktop::IsTaskbar(foreground))
        return;
    bool wasFullscreen = fullscreenContext;
    RECT activeBounds{};
    MONITORINFO activeMonitor{sizeof(activeMonitor)};
    GetWindowRect(foreground, &activeBounds);
    GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &activeMonitor);
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    fullscreenContext = SUCCEEDED(SHQueryUserNotificationState(&state)) &&
                        desktop::HideForNotificationState(state, dockOptions.fullscreenPolicy);
    if (foreground == contextWindow) {
        if (wasFullscreen != fullscreenContext) {
            if (expanded)
                RefreshGeometry();
            else
                Place();
        }
        return;
    }
    bool wasBrowser = browserContext;
    contextWindow = foreground;
    browserContext = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process) {
        wchar_t path[32768]{};
        DWORD length = 32768;
        if (QueryFullProcessImageNameW(process, 0, path, &length))
            browserContext = dock::BrowserExecutable(std::wstring_view(path, length));
        CloseHandle(process);
    }
    wchar_t name[64]{};
    GetClassNameW(foreground, name, 64);
    if (!wcscmp(name, L"MozillaWindowClass"))
        browserContext = true;
    if (wasBrowser != browserContext || wasFullscreen != fullscreenContext) {
        if (expanded)
            RefreshGeometry();
        else
            Place();
    }
}

void Island::ArmMotion() {
    if (renderMode || reducedMotion) {
        SnapMotion();
        return;
    }
    bool started = scheduler.Start();
    if (started)
        lastMotion = steady_clock::now();
    if (composition.FrameReady() && frameClock.Start(window, MotionFrame, composition.FrameReady()))
        KillTimer(window, TransitionTimer);
    else if (started)
        SetTimer(window, TransitionTimer, motionInterval, nullptr);
}

void Island::SetExpanded(bool value, bool deliberate) {
    if (value && !expanded && GetForegroundWindow() != window)
        clipboardPasteTarget = ::clipboard::CapturePasteTarget();
    if (value && !expanded)
        UpdateContext();
    if (!renderMode && !fixtureGeometry) {
        if (value && tab == Tab::Today && flyout == Flyout::None)
            RefreshToday();
    }
    if (!applyingInteraction && !renderMode && !fixtureGeometry) {
        if (value)
            interaction.OnExpand();
        else
            interaction.OnDismiss(deliberate);
    }
    bool opening = value && !expanded, closing = !value && expanded;
    if (opening) {
        revealClock = 0;
        contentBlend.Snap(0);
    }
    if (closing) {
        collapseClock = 0;
        collapsePending = !reducedMotion && !renderMode;
        contentBlend.goal = 0;
    }
    if (value)
        collapsePending = false;
    expanded = value;
    if (docks)
        docks->PanelVisible(value);
    if (value && flyout == Flyout::Palette && paletteItems.empty())
        BuildPalette();
    if (value && flyout == Flyout::Module) {
        moduleRegistry.Poll();
        if (auto panel = moduleRegistry.View(moduleView, moduleQuery))
            modulePanel = std::move(*panel);
    }
    hovered = -1;
    if (!value) {
        focusedKey.clear();
        pressedKey.clear();
        focused = -1;
    }
    if (value) {
        if (tab == Tab::Controls)
            RefreshControls();
        system.RefreshVolume();
        if (!renderMode && !fixtureGeometry && tab == Tab::Controls && flyout == Flyout::None) {
            if (!connectivity)
                connectivity = std::make_unique<Connectivity>(window);
            connectivity->RefreshWifi(false);
            connectivity->RefreshBluetooth(false);
        }
        if (!renderMode && !fixtureGeometry && !media && tab == Tab::Controls && flyout == Flyout::None)
            media = std::make_unique<MediaService>(window);
    }
    if (media)
        media->SetVisible(value && tab == Tab::Controls && flyout == Flyout::None);
    if (!value) {
        clipboardReadTask.Cancel();
        paletteItems.clear();
        commandCatalog.Clear();
        paletteQuery.clear();
        if (appSearch && flyout == Flyout::Palette)
            SetWindowTextW(appSearch, L"");
        moduleRegistry.Query(L"");
        modulePanel = {};
        moduleQuery.clear();
        clipboard.clear();
        if (clipboardEdit)
            SetWindowTextW(clipboardEdit, L"");
        clipboardState = L"Preview cleared";
        keyboardOpen = false;
        launchPreview = false;
        if (wifiPassword)
            SetWindowTextW(wifiPassword, L"");
        if (connectivity)
            connectivity->CancelDiscovery();
    }
    dockReveal.goal = value || collapsePending ? 1.0f : 0.0f;
    dockWidth.goal = value || collapsePending ? SurfaceWidth() : theme::DockWidth;
    dockHeight.goal = value || collapsePending ? SurfaceHeight() : theme::DockHeight;
    if (value && (reducedMotion || renderMode || !opening))
        contentBlend.goal = 1;
    transitioning = !reducedMotion && !renderMode;
    if (transitioning)
        ArmMotion();
    else {
        dockReveal.Snap(dockReveal.goal);
        dockWidth.Snap(dockWidth.goal);
        dockHeight.Snap(dockHeight.goal);
        contentBlend.Snap(value ? 1.0f : 0.0f);
        width = dockWidth.value;
        height = dockHeight.value;
        Place();
        if (!value && !renderMode)
            ReleaseGraphics();
    }
    RebuildLayout();
    UpdateTimers();
    UpdateInteractionTimer();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::Transition() {
    if (!scheduler.active)
        return;
    auto now = steady_clock::now();
    float dt = duration<float>(now - lastMotion).count();
    lastMotion = now;
    if (!diagnosticsPath.empty() && frameIntervals.size() < 8192)
        frameIntervals.push_back(dt * 1000);
    if (dt > 0.5f) {
        SnapMotion();
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    bool moving = false;
    float speed = options.Int(L"Animation") / 100.0f;
    auto advance = [&](dock::Spring &spring, dock::motion::Spec spec, float elapsed) {
        while (elapsed > 0) {
            float step = std::min(elapsed, .05f);
            spring.Step(step, spec, speed);
            elapsed -= step;
        }
        moving |= spring.Moving();
    };
    float previousReveal = revealClock;
    if (expanded)
        revealClock += dt;
    if (collapsePending) {
        collapseClock += dt;
        advance(contentBlend, dock::motion::Collapse, dt);
        if (collapseClock >= .12f / speed || contentBlend.value <= .001f) {
            collapsePending = false;
            dockReveal.goal = 0;
            dockWidth.goal = theme::DockWidth;
            dockHeight.goal = theme::DockHeight;
        }
        moving |= collapsePending;
    }
    advance(dockReveal, dock::motion::Settle, dt);
    advance(dockHeight, highContrast ? dock::motion::Settle : dock::motion::Height, dt);
    float widthElapsed =
        expanded ? std::max(0.0f, revealClock - .04f / speed) - std::max(0.0f, previousReveal - .04f / speed) : dt;
    advance(dockWidth, dock::motion::Width, widthElapsed);
    if (expanded && dockReveal.value > .6f)
        contentBlend.goal = 1;
    if (!collapsePending)
        advance(contentBlend, dock::motion::Fade, dt);
    advance(paneBlend, dock::motion::Settle, dt);
    advance(wallpaperFocus, dock::motion::Carousel, dt);
    advance(tabLeft, bodyDirection > 0 ? dock::motion::TabTrail : dock::motion::Snappy, dt);
    advance(tabRight, bodyDirection > 0 ? dock::motion::Snappy : dock::motion::TabTrail, dt);
    for (auto *spring : {&railPointer, &railStrength, &togglePosition, &composerFocus})
        advance(*spring, dock::motion::Snappy, dt);
    for (auto *spring : {&paletteBlend, &wallpaperParallax, &volumeMotion})
        advance(*spring, dock::motion::Soft, dt);
    advance(toastBlend, dock::motion::Fade, dt);
    advance(focusBlend, dock::motion::Focus, dt);
    for (auto &spring : rings)
        advance(spring, dock::motion::Soft, dt);
    for (auto &[id, spring] : hoverMotion) {
        (void)id;
        advance(spring, dock::motion::Snappy, dt);
    }
    for (auto &[id, spring] : tileState) {
        (void)id;
        advance(spring, dock::motion::Snappy, dt);
    }
    for (auto &[key, spring] : pressMotion) {
        (void)key;
        advance(spring, dock::motion::Press, dt);
    }
    for (auto &[key, spring] : taskMotion) {
        (void)key;
        advance(spring, dock::motion::Task, dt);
    }
    for (auto &[key, spring] : feedback) {
        (void)key;
        spring.Step(dt, speed);
        moving |= spring.Moving();
    }
    for (auto &[key, spring] : rowPositions) {
        (void)key;
        advance(spring, dock::motion::Settle, dt);
    }
    if (paneClock < .37f && !highContrast) {
        paneClock = std::min(.37f, paneClock + dt * speed);
        moving |= paneClock < .37f;
    }
    if (VisibleLoading() && !highContrast) {
        loadingClock = std::fmod(loadingClock + dt, 1.2f);
        moving = true;
    } else
        loadingClock = 0;
    for (auto it = feedback.begin(); it != feedback.end();)
        if (!it->second.Moving())
            it = feedback.erase(it);
        else
            ++it;
    width = std::clamp(dockWidth.value, 1.0f, envelopeWidth);
    height = std::clamp(dockHeight.value, 1.0f, envelopeHeight);
    transitioning = collapsePending || dockReveal.Moving() || dockWidth.Moving() || dockHeight.Moving();
    ApplyAccent(dock::Mix(previousAccent, targetAccent, paletteBlend.value));
    if (paneBlend.value == 1)
        previousPane.Reset();
    if (!moving) {
        frameClock.Stop();
        frameTickReady = false;
        scheduler.Stop();
        KillTimer(window, TransitionTimer);
        for (auto it = hoverMotion.begin(); it != hoverMotion.end();)
            if (it->second.value == 0)
                it = hoverMotion.erase(it);
            else
                ++it;
    }
    if (dockReveal.value == 0 && !expanded)
        SnapMotion();
    Place();
    RebuildLayout();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::UpdateInteractionTimer() {
    if (renderMode || fixtureGeometry)
        return;
    bool needed = interaction.NeedsTick();
    if (needed && !interactionTicking) {
        lastInteraction = steady_clock::now();
        SetTimer(window, InteractionTimer, 16, nullptr);
    } else if (!needed && interactionTicking)
        KillTimer(window, InteractionTimer);
    interactionTicking = needed;
}

void Island::ApplyInteraction() {
    bool nextOpen = interaction.Current() != island::Mode::Dock;
    bool nextMini = interaction.Current() == island::Mode::Peek;
    if (nextOpen != expanded || (nextOpen && miniMode != nextMini)) {
        applyingInteraction = true;
        miniMode = nextMini;
        if (nextMini) {
            tab = Tab::Controls;
            flyout = Flyout::None;
        }
        SetExpanded(nextOpen, false);
        applyingInteraction = false;
        RetargetLayout();
    }
    UpdateInteractionTimer();
}

void Island::UpdateInteractionMedia() {
    auto sources = v4::Split(options.Text(L"MediaSources"));
    bool allowed = sources.empty() || std::find(sources.begin(), sources.end(), currentMedia.source) != sources.end();
    interaction.OnMedia(allowed &&
                        island::MediaAllowsPeek(currentMedia.available, currentMedia.playing, currentMedia.paused,
                                                !currentMedia.title.empty(), options.Bool(L"PeekPlayingOnly")));
    ApplyInteraction();
}

void Island::InteractionPointer(bool inside) {
    if (renderMode || fixtureGeometry || previewMode)
        return;
    if (!interaction.NeedsTick())
        lastInteraction = steady_clock::now();
    interaction.OnPointer(inside);
    InteractionTick();
}

void Island::InteractionTick() {
    if (renderMode || fixtureGeometry)
        return;
    auto now = steady_clock::now();
    float elapsed = duration<float>(now - lastInteraction).count();
    lastInteraction = now;
    POINT point{};
    GetCursorPos(&point);
    bool inside = false;
    if (docks) {
        HWND under = GetAncestor(WindowFromPoint(point), GA_ROOT);
        inside = expanded ? docks->Contains(under) || docks->Contains(GetWindow(under, GW_OWNER))
                          : docks->DwellContains(point);
    }
    POINT local = point;
    ScreenToClient(window, &local);
    float x = local.x * 96.0f / dpi, y = local.y * 96.0f / dpi;
    inside |= expanded ? PresentedContains(x, y)
                       : !DockEnabled() && std::abs(x - envelopeWidth / 2) < theme::DockWidth / 2 + 18 &&
                             y >= HandleY() && y < HandleY() + theme::DockHeight + 6;
    HWND foreground = GetForegroundWindow();
    bool editing = foreground == window && (GetFocus() == taskEdit || GetFocus() == appSearch ||
                                            GetFocus() == wifiPassword || GetFocus() == clipboardEdit);
    bool busy = previewMode || modalOpen || draggingVolume || draggingBrightness || launchPreview || editing ||
                (keyboardOpen && foreground == window) ||
                (expanded && flyout == Flyout::Module && moduleView == L"shelf");
    interaction.OnPointer(inside);
    interaction.OnBusy(busy);
    interaction.Step(elapsed);
    ApplyInteraction();
}

void Island::UpdateTimers() {
    if (renderMode || fixtureGeometry)
        return;
    auto timer = [&](UINT_PTR id, bool enabled, UINT period, bool &active) {
        if (enabled == active)
            return;
        active = enabled;
        if (enabled)
            SetTimer(window, id, period, nullptr);
        else
            KillTimer(window, id);
    };
    bool stats = expanded && tab == Tab::Stats && flyout == Flyout::None;
    if (stats && !statsTicking)
        SampleStats();
    timer(StatsTimer, stats, 1000, statsTicking);
    timer(MediaTimer, currentMedia.playing && expanded && tab == Tab::Controls && flyout == Flyout::None, 1000,
          mediaTicking);
    timer(FocusTimer, focusRunning || (expanded && !miniMode && tab == Tab::Today && flyout == Flyout::None), 1000,
          focusTicking);
    timer(ConnectivityTimer, expanded && (flyout == Flyout::Wifi || flyout == Flyout::Bluetooth), 2500,
          connectionsTicking);
}

void Island::SetTab(Tab value) {
    bool fromPalette = flyout == Flyout::Palette;
    if (expanded && !miniMode && flyout == Flyout::None && tab == value)
        return;
    modulePanel = {};
    moduleView.clear();
    moduleQuery.clear();
    miniMode = false;
    previousPane.Reset();
    paneBlend.Snap(reducedMotion || renderMode || highContrast ? 1.0f : 0.0f);
    paneBlend.goal = 1;
    paneClock = reducedMotion || renderMode || highContrast ? 1.0f : 0.0f;
    if (connectivity)
        connectivity->CancelDiscovery();
    flyout = Flyout::None;
    bodyDirection = static_cast<int>(value) >= static_cast<int>(tab) ? 1.0f : -1.0f;
    if (!tabMotionReady) {
        auto previous = TabRect(static_cast<int>(tab));
        tabLeft.Snap(previous.left);
        tabRight.Snap(previous.right);
        tabMotionReady = true;
    }
    tab = value;
    auto selected = TabRect(static_cast<int>(tab));
    tabLeft.goal = selected.left;
    tabRight.goal = selected.right;
    if (tab == Tab::Stats)
        for (auto &ring : rings)
            ring.Snap(0);
    note.clear();
    scroll = 0;
    focusedKey.clear();
    focused = hovered = -1;
    if (tab == Tab::Apps)
        FilterRunning();

    if (!renderMode && !fixtureGeometry && (tab == Tab::Apps || tab == Tab::Wallpaper)) {
        if (!catalog)
            catalog = std::make_unique<DesktopCatalog>(window);
        if (tab == Tab::Apps && (!appsRequested || (!appsLoading && apps.empty()))) {
            appsRequested = true;
            appsLoading = true;
            catalog->DiscoverApps();
        }
        if (tab == Tab::Wallpaper) {
            catalog->DiscoverDisplays();
            if (!wallpapersRequested)
                WallpaperSource(wallpaperSource);
        }
    }
    SetExpanded(true);
    if (fromPalette && appSearch) {
        SetWindowTextW(appSearch, appQuery.c_str());
        SendMessageW(appSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search installed apps…"));
    }
    PositionInputs();
}
