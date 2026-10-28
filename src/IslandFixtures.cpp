#include "Island.h"

void Island::SaveRender(const std::wstring &path) {
    privacy::Write audit(L"diagnostics", L"file-write", path, L"fixture-render");
    ReleaseGraphics();
    ComPtr<IWICImagingFactory> images;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images)));
    ComPtr<IWICBitmap> bitmap;
    UINT imageWidth = static_cast<UINT>(std::lround((renderMode ? width : envelopeWidth) * dpi / 96)),
         imageHeight = static_cast<UINT>(std::lround((renderMode ? height : envelopeHeight) * dpi / 96));
    Check(images->CreateBitmap(imageWidth, imageHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap));
    Check(factory->CreateWicBitmapRenderTarget(
        bitmap.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi,
                                     dpi),
        &target));
    Check(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush));
    Draw();
    ComPtr<IWICStream> stream;
    Check(images->CreateStream(&stream));
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(images->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(imageWidth, imageHeight));
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    Check(frame->SetPixelFormat(&pixelFormat));
    Check(frame->WriteSource(bitmap.Get(), nullptr));
    Check(frame->Commit());
    Check(encoder->Commit());
    audit.Complete(true);
    ReleaseGraphics();
}

void Island::AssertScene() {
    RebuildLayout();
    auto l = Layout();
    if (SurfaceWidth() > envelopeWidth + 0.1f || SurfaceHeight() > envelopeHeight - PanelY() + 0.1f)
        winrt::throw_hresult(E_UNEXPECTED);
    if (expanded && !miniMode && l.emergency) {
        bool actions = false;
        for (const auto &h : hits)
            if (h.id == 1901 && VisibleHit(h))
                actions = true;
        if (!actions)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    if (flyout == Flyout::Audio && l.emergency && IndexForKey(StableKey(FlyoutBack)) < 0)
        winrt::throw_hresult(E_UNEXPECTED);
    for (const auto &h : hits) {
        if (h.key.empty() || h.child <= 0)
            winrt::throw_hresult(E_UNEXPECTED);
        if (h.clip.left < 0 || h.clip.top < 0 || h.clip.right > LayoutWidth() + 0.1f ||
            h.clip.bottom > LayoutHeight() + 0.1f)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    for (size_t i = 0; i < hits.size(); ++i)
        for (size_t j = i + 1; j < hits.size(); ++j) {
            if (hits[i].key == hits[j].key)
                winrt::throw_hresult(E_UNEXPECTED);
        }
    if (tab == Tab::Apps && !filteredApps.empty() && flyout == Flyout::None && !Layout().emergency) {
        auto first = std::find_if(hits.begin(), hits.end(), [](const Hit &h) { return h.id == 300; });
        if (first == hits.end() || first->rect.bottom > Body().bottom + 0.1f)
            winrt::throw_hresult(E_UNEXPECTED);
    }
    if (flyout == Flyout::None && !miniMode && expanded && !Layout().emergency) {
        for (Tab primary : PrimaryTabs) {
            int i = static_cast<int>(primary);
            auto rect = TabRect(i);
            int index = HitAt((rect.left + rect.right) / 2 * textScale, (rect.top + rect.bottom) / 2 * textScale);
            if (index < 0 || hits[index].id != 100 + i)
                winrt::throw_hresult(E_UNEXPECTED);
        }
    }
}

void Island::RenderSuite(const std::wstring &directory) {
    fixtureGeometry = true;
    reducedMotion = true;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    systemAppearance = false;
    lightAppearance = true;
    ConfigureAppearance();
    hardwareState.cpu = L"Fixture processor";
    hardwareState.gpu = L"Fixture graphics";
    hardwareState.gpuValid = true;
    hardwareState.gpuPercent = 12;
    hardwareState.vramGiB = 8;
    hardwareState.totalGiB = 32;
    hardwareState.processes = 229;
    hardwareState.top = {
        {L"Browser", 1400, 101}, {L"Browser", 820, 102}, {L"Music", 410, 103}, {L"Terminal", 300, 104}};
    system.cpuValid = system.memoryValid = system.diskValid = system.networkValid = true;
    system.totalCpu = 37;
    system.memoryLoad = 61;
    system.availableGiB = 12.5;
    system.diskRoot = L"C:\\";
    system.diskTotalGiB = 512;
    system.diskUsedGiB = 256;
    system.volume = 0.42f;
    system.wifi = L"Fixture network";
    system.wifiConnected = true;
    system.bluetooth = L"Fixture headphones";
    system.audioName = L"Fixture audio output";
    controlState.darkKnown = true;
    controlState.brightnessSupported = true;
    controlState.brightness = 64;
    controlState.radiosAvailable = true;
    controlState.outputs = {{L"fixture:speakers", L"Fixture speakers", true},
                            {L"fixture:headphones", L"Fixture headphones", false}};
    wallpaperDisplays = {{L"fixture:left", L"Display 1", L"", {0, 0, 1920, 1080}},
                         {L"fixture:right", L"Display 2", L"", {1920, 0, 3840, 1080}}};
    wallpaperTarget = wallpaperDisplays.front().id;
    hardwareState.volumes = {{L"C:", 512, 256, true}, {L"D:", 1024, 420, true}, {L"E:", 0, 0, false}};
    hardwareState.uptimeSeconds = 183600;
    hardwareState.batteryPresent = true;
    hardwareState.batterySeconds = 7200;
    for (size_t i = 0; i < hardwareState.top.size(); ++i) {
        hardwareState.top[i].cpu = 32.0 / (i + 1);
        hardwareState.top[i].cpuValid = true;
    }
    hardwareState.memoryTop = hardwareState.top;
    for (uint64_t i = 1; i <= 60; ++i)
        hardwareState.history.Push(i * 1000, {30 + 20 * std::sin(i * .3), 60 + 4 * std::cos(i * .2),
                                              1000000.0 * (1 + std::sin(i * .2)), 100000, true, true, true});
    wallpapers = DesktopCatalog::LoadWallpapers(DesktopCatalog::BundledWallpaperRoot());
    if (wallpapers.size() != 18)
        winrt::throw_hresult(E_UNEXPECTED);
    auto bundled = wallpapers;
    weather = {true, 18, 21, 12, 2, L"Fixture city", L"Offline weather fixture"};
    weatherCity = L"Fixture city";
    subscriptionIcs = L"BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:20261023T180000\nSUMMARY:Evening walk\nEND:VEVENT\n"
                      L"BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261024\nSUMMARY:Weekend plans\nEND:VEVENT\n"
                      L"BEGIN:VEVENT\nDTSTART:20261026T090000\nSUMMARY:Weekly planning\nRRULE:FREQ=WEEKLY;COUNT=8\n"
                      L"END:VEVENT\nEND:VCALENDAR";
    RebuildCalendar();
    for (int i = 0; i < 5; ++i)
        tasks.push_back({i == 0 ? L"A long task title with mixed scripts · 日本語 · emoji ✨ that must trim safely"
                                : L"Task " + std::to_wstring(i + 1),
                         i == 1, nextTask++});
    for (int i = 0; i < 108; ++i)
        apps.emplace_back(L"Application " + std::to_wstring(i + 1), L"fixture:" + std::to_wstring(i));
    appPreferences.favorites = {L"fixture:0", L"fixture:4", L"fixture:7"};
    appPreferences.recent = {L"fixture:6", L"fixture:5", L"fixture:2"};
    FilterApps();
    tab = Tab::Apps;
    expanded = true;
    appQuery = L"Application 108";
    FilterApps();
    if (dockHeight.goal != SurfaceHeight())
        winrt::throw_hresult(E_UNEXPECTED);
    appQuery.clear();
    FilterApps();
    if (dockHeight.goal != SurfaceHeight())
        winrt::throw_hresult(E_UNEXPECTED);
    expanded = false;
    clipboard = L"Current text preview\r\nMultiple lines, mixed scripts 日本語 and emoji ✨.\r\nThis reader has native "
                L"scrolling and selection. No history is collected.";
    clipboardState = L"Current text · select or scroll to read";
    constexpr const wchar_t *names[]{L"controls", L"today", L"stats", L"clipboard", L"apps", L"wallpaper", L"power"};
    size_t captures = 0;
    auto reportPath = std::filesystem::path(directory) / L"verification.txt";
    privacy::Write verification(L"diagnostics", L"file-write", reportPath.wstring(), L"fixture-report");
    std::wofstream report(reportPath, std::ios::trunc);
    auto capture = [&](const std::wstring &name) {
        report << "render " << name << std::endl;
        width = SurfaceWidth();
        height = SurfaceHeight();
        dockWidth.Snap(width);
        dockHeight.Snap(height);
        dockReveal.Snap(1);
        expanded = true;
        transitioning = false;
        paneBlend.Snap(1);
        SaveRender(directory + L"\\" + name + L".png");
        AssertScene();
        ++captures;
    };
    for (int i = 0; i < 7; ++i) {
        tab = static_cast<Tab>(i);
        scroll = 0;
        capture(names[i]);
    }
    tab = Tab::Wallpaper;
    selectedWallpaper = wallpapers.size() - 1;
    wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));
    capture(L"wallpaper-last");
    note = L"Could not apply wallpaper. Choose another image.";
    capture(L"wallpaper-error");
    note.clear();
    auto sixtyFour = bundled;
    while (sixtyFour.size() < 64) {
        auto entry = bundled[sixtyFour.size() % bundled.size()];
        entry.path = L"fixture:" + std::to_wstring(sixtyFour.size());
        sixtyFour.push_back(entry);
    }
    ReplaceWallpapers(sixtyFour);
    selectedWallpaper = 63;
    wallpaperFocus.Snap(63);
    capture(L"wallpaper-64");
    ReplaceWallpapers({bundled.front()});
    if (selectedWallpaper != 0 || wallpaperFocus.value != 0)
        winrt::throw_hresult(E_UNEXPECTED);
    capture(L"wallpaper-one");
    ReplaceWallpapers({});
    if (selectedWallpaper != 0 || wallpaperFocus.value != 0)
        winrt::throw_hresult(E_UNEXPECTED);
    note = L"No pictures found. Choose another folder.";
    capture(L"wallpaper-empty");
    note.clear();
    ReplaceWallpapers(bundled);
    connections.wifiAvailable = connections.wifiOn = connections.bluetoothAvailable = connections.bluetoothOn = true;
    WifiNetwork home;
    home.name = L"Fixture secured network with a long visible name";
    home.secure = home.connectable = true;
    home.quality = 92;
    connections.networks = {home};
    flyout = Flyout::Wifi;
    selectedWifi = 0;
    capture(L"wifi-password");
    connections.networks[0].connected = true;
    connections.networks[0].profile = L"Fixture";
    capture(L"wifi");
    connections.wifiMessage = L"Credentials rejected. Try again or choose More settings.";
    capture(L"wifi-error");
    connections.wifiMessage.clear();
    BluetoothEntry device;
    device.name = L"Fixture headphones";
    device.paired = device.connected = true;
    device.native.Address.ullLong = 1;
    connections.devices = {device};
    flyout = Flyout::Bluetooth;
    selectedBluetooth = 0;
    capture(L"bluetooth");
    connections.bluetoothBusy = true;
    connections.bluetoothMessage = L"Searching for devices…";
    capture(L"bluetooth-busy");
    connections.bluetoothBusy = false;
    connections.bluetoothMessage.clear();
    flyout = Flyout::None;
    miniMode = true;
    tab = Tab::Controls;
    currentMedia.available = currentMedia.canPrevious = currentMedia.canPlay = currentMedia.canNext = true;
    currentMedia.playing = true;
    currentMedia.title = L"Fixture music";
    currentMedia.artist = L"Fixture artist";
    currentMedia.position = 42;
    currentMedia.duration = 180;
    capture(L"media-mini");
    miniMode = false;
    fixedAccent = false;
    SetAccent(0x83B8DE);
    capture(L"theme-blue");
    SetAccent(0xD898BC);
    capture(L"theme-pink");
    fixedAccent = true;
    ConfigureAppearance();
    capture(L"theme-light");
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"theme-dark");
    ConfigureAppearance(true);
    capture(L"theme-high-contrast");
    lightAppearance = true;
    ConfigureAppearance();
    tab = Tab::Controls;
    system.volume = 0;
    capture(L"controls-volume-zero");
    system.volume = .01f;
    capture(L"controls-volume-one");
    system.volume = 1;
    capture(L"controls-volume-full");
    system.muted = true;
    capture(L"controls-muted");
    system.muted = false;
    fixtureAudioAvailable = false;
    system.volume = 0;
    system.audioName = L"No audio device";
    capture(L"controls-no-audio");
    fixtureAudioAvailable = true;
    system.audioName = L"Fixture audio output";
    system.volume = .42f;
    // Pairwise edge states use inert data, never the user's clipboard or devices.
    auto savedConnections = connections;
    flyout = Flyout::Wifi;
    selectedWifi = -1;
    capture(L"wifi-unselected");
    connections.networks.clear();
    connections.wifiOn = false;
    connections.wifiMessage = L"Wi-Fi is switched off. Use Actions or More settings.";
    capture(L"wifi-radio-off");
    connections.wifiAvailable = false;
    connections.wifiMessage = L"No adapter available. Use More settings.";
    capture(L"wifi-no-adapter");
    connections.wifiAvailable = true;
    connections.wifiMessage = L"Windows denied access. Choose More settings to review permissions.";
    capture(L"wifi-permission-error");
    connections = savedConnections;
    flyout = Flyout::Bluetooth;
    selectedBluetooth = 0;
    connections.devices[0].paired = connections.devices[0].connected = false;
    capture(L"bluetooth-unpaired");
    connections.bluetoothMessage = L"Pairing canceled. Try again or choose More settings.";
    capture(L"bluetooth-error");
    connections.devices.clear();
    selectedBluetooth = -1;
    connections.bluetoothMessage = L"No results. Try Refresh.";
    capture(L"bluetooth-empty");
    connections.bluetoothOn = false;
    connections.bluetoothMessage = L"Bluetooth is switched off.";
    capture(L"bluetooth-radio-off");
    connections.bluetoothAvailable = false;
    connections.bluetoothMessage = L"No adapter available. Use More settings.";
    capture(L"bluetooth-no-adapter");
    connections = savedConnections;
    flyout = Flyout::None;
    auto savedTasks = tasks;
    tasks.clear();
    tab = Tab::Today;
    capture(L"today-empty");
    tasks = savedTasks;
    auto fixtureWeather = weather;
    auto fixtureCalendar = calendarEvents;
    weather = {};
    weatherCity.clear();
    calendarEvents.clear();
    capture(L"today-offline-empty");
    weatherCity = L"Fixture city";
    weather.message = L"Loading weather…";
    capture(L"today-loading");
    weather.message = L"Weather unavailable. Reopen Today to retry.";
    capture(L"today-error");
    weather = fixtureWeather;
    calendarEvents = fixtureCalendar;
    while (tasks.size() < 20)
        tasks.push_back({L"Scrolling task " + std::to_wstring(tasks.size() + 1), false, nextTask++});
    taskScroll = 10000;
    capture(L"today-20-tasks");
    taskScroll = 0;
    tasks = savedTasks;
    for (float dpiScale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * dpiScale;
        envelopeWidth = std::min(900.0f, 1366 / dpiScale);
        envelopeHeight = std::min(760.0f, 768 / dpiScale);
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
            textScale = text;
            scroll = 0;
            capture(L"today-matrix-" + std::to_wstring(static_cast<int>(dpiScale * 100)) + L"-" +
                    std::to_wstring(static_cast<int>(text * 100)));
        }
    }
    textScale = 1;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"today-dark");
    lightAppearance = true;
    ConfigureAppearance(true);
    capture(L"today-high-contrast");
    ConfigureAppearance();

    tab = Tab::Apps;
    appQuery = L"No matching fixture application";
    FilterApps();
    capture(L"apps-empty");
    appQuery.clear();
    FilterApps();
    tab = Tab::Clipboard;
    auto savedClipboard = clipboard;
    auto savedClipboardState = clipboardState;
    clipboard.clear();
    clipboardState = L"Clipboard empty";
    capture(L"clipboard-empty");
    clipboard = std::wstring(8192, L'x');
    clipboardState = L"Showing first 8,192 UTF-16 units. Scroll or select to read.";
    capture(L"clipboard-long");
    clipboard = savedClipboard;
    clipboardState = savedClipboardState;
    tab = Tab::Wallpaper;
    auto savedWallpapers = wallpapers;
    ReplaceWallpapers({});
    wallpaperLoading = true;
    capture(L"wallpaper-loading");
    wallpaperLoading = false;
    ReplaceWallpapers(savedWallpapers);
    wallpaperApplying = true;
    capture(L"wallpaper-applying");
    wallpaperApplying = false;
    tab = Tab::Controls;
    miniMode = true;
    auto savedMedia = currentMedia;
    currentMedia = MediaState{};
    capture(L"media-unavailable");
    currentMedia = savedMedia;
    currentMedia.duration = 0;
    capture(L"media-live-stream");
    currentMedia.duration = 180;
    currentMedia.playing = false;
    capture(L"media-paused");
    currentMedia.title = L"Long media title with mixed scripts \u65e5\u672c\u8a9e and emoji \U0001F3B5 that remains "
                         L"available in full details";
    capture(L"media-long");
    currentMedia = savedMedia;
    miniMode = false;
    for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * scale;
        envelopeWidth = std::min(900.0f, 1366 / scale);
        envelopeHeight = std::min(760.0f, 768 / scale);
        for (int i = 0; i < 7; ++i) {
            tab = static_cast<Tab>(i);
            scroll = 0;
            note = i == 5 ? L"Fixture error stays below the action footer." : L"";
            capture(std::wstring(L"matrix-") + names[i] + L"-" + std::to_wstring(static_cast<int>(scale * 100)));
        }
        tab = Tab::Today;
        scroll = 10000;
        capture(L"matrix-today-scrolled-" + std::to_wstring(static_cast<int>(scale * 100)));
    }
    dpi = 192;
    envelopeWidth = 683;
    envelopeHeight = 272;
    scroll = 0;
    note.clear();
    tab = Tab::Apps;
    capture(L"browser-apps-200");
    flyout = Flyout::Wifi;
    selectedWifi = 0;
    connections.networks[0].connected = false;
    connections.networks[0].profile.clear();
    capture(L"browser-wifi-200");
    flyout = Flyout::None;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    scroll = 0;
    note.clear();
    tab = Tab::Controls;
    capture(L"controls-final");
    // Input identity must survive order changes without activating the replacement.
    tab = Tab::Apps;
    RebuildLayout();
    auto first = std::find_if(hits.begin(), hits.end(), [](const Hit &h) { return h.id == 300; });
    if (first == hits.end())
        winrt::throw_hresult(E_UNEXPECTED);
    pressedKey = first->key;
    pressedId = 300;
    appQuery = L"Application 108";
    FilterApps();
    RebuildLayout();
    if (!pressedKey.empty())
        winrt::throw_hresult(E_UNEXPECTED);
    appQuery.clear();
    FilterApps();
    for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
        textScale = text;
        dpi = 192;
        envelopeWidth = 683;
        envelopeHeight = 272;
        scroll = 0;
        flyout = Flyout::None;
        note = L"Could not complete this operation. Open Actions and read status to retry.";
        for (int i = 0; i < 7; ++i) {
            tab = static_cast<Tab>(i);
            scroll = 0;
            capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-" + names[i]);
        }
        flyout = Flyout::Wifi;
        selectedWifi = 0;
        scroll = 0;
        capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-wifi");
        flyout = Flyout::None;
        miniMode = true;
        capture(L"text-" + std::to_wstring(static_cast<int>(text * 100)) + L"-mini");
        miniMode = false;
    }
    textScale = 1;
    dpi = 96;
    envelopeWidth = 900;
    envelopeHeight = 760;
    note.clear();
    // Hidden native EDIT verification is separate from software pixels.
    tab = Tab::Today;
    renderMode = false;
    previewMode = true;
    dpi = 96;
    scroll = 0;
    RebuildLayout();
    EnsureTaskEdit();
    wchar_t cue[64]{};
    if (!SendMessageW(taskEdit, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(cue), 64) ||
        std::wstring(cue) != L"Add a task…")
        winrt::throw_hresult(E_UNEXPECTED);
    SetWindowTextW(taskEdit, L"V2 native draft — 日本語");
    SendMessageW(taskEdit, EM_SETSEL, 3, 8);
    RECT suggested{};
    GetWindowRect(window, &suggested);
    SendMessageW(window, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&suggested));
    wchar_t draft[121]{};
    GetWindowTextW(taskEdit, draft, 121);
    DWORD start = 0, end = 0;
    SendMessageW(taskEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    if (std::wstring(draft) != L"V2 native draft — 日本語" || start != 3 || end != 8)
        winrt::throw_hresult(E_UNEXPECTED);
    SendMessageW(window, WM_DPICHANGED, MAKELONG(96, 96), reinterpret_cast<LPARAM>(&suggested));
    DestroyWindow(taskEdit);
    taskEdit = nullptr;
    taskDraft.clear();
    renderMode = true;
    previewMode = false;
    TaskStore store((std::filesystem::path(directory) / L"tasks-fixture.ini").wstring());
    std::vector<std::pair<std::wstring, bool>> saved{{L"Mixed scripts 日本語 ✨", false}, {L"Completed", true}};
    if (!store.Save(saved) || store.Load() != saved)
        winrt::throw_hresult(E_UNEXPECTED);
    TaskStore failure((std::filesystem::path(directory) / L"verification.txt" / L"tasks.ini").wstring());
    if (failure.Save(saved))
        winrt::throw_hresult(E_UNEXPECTED);
    report << "PASS native EDIT draft/caret/selection across synthetic DPI transaction; atomic task round trip and "
              "injected save failure."
           << std::endl;
    auto dockCapture = [&](const std::wstring &name, UINT scale, float text, int variant = 0) {
        if (!desktop::DockShell::RenderFixture((std::filesystem::path(directory) / (name + L".png")).wstring(), scale,
                                               text, variant))
            winrt::throw_hresult(E_UNEXPECTED);
        ++captures;
    };
    for (UINT scale : {96U, 120U, 144U, 168U, 192U})
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f})
            dockCapture(L"dock-matrix-" + std::to_wstring(scale * 100 / 96) + L"-" +
                            std::to_wstring(static_cast<int>(text * 100)),
                        scale, text);
    for (UINT scale : {96U, 120U, 144U, 168U, 192U})
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f})
            dockCapture(L"preview-matrix-" + std::to_wstring(scale * 100 / 96) + L"-" +
                            std::to_wstring(static_cast<int>(text * 100)),
                        scale, text, 3);
    dockCapture(L"preview", 96, 1, 3);
    dockCapture(L"preview-unavailable", 96, 1, 4);
    dockCapture(L"dock", 96, 1);
    dockCapture(L"dock-empty", 96, 1, 1);
    dockCapture(L"dock-overflow", 96, 1, 2);
    lightAppearance = false;
    ConfigureAppearance();
    dockCapture(L"dock-dark", 96, 1);
    dockCapture(L"preview-dark", 96, 1, 3);
    lightAppearance = true;
    ConfigureAppearance(true);
    dockCapture(L"dock-high-contrast", 96, 1, 7);
    dockCapture(L"preview-high-contrast", 96, 1, 3);
    ConfigureAppearance();
    auto previousWindows = runningWindows;
    for (int i = 0; i < 5; ++i) {
        desktop::WindowEntry entry;
        entry.window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(100 + i));
        entry.title = L"Fixture running document " + std::to_wstring(i + 1);
        entry.identity = L"fixture-running:" + std::to_wstring(i);
        entry.active = i == 0;
        runningWindows.push_back(entry);
    }
    FilterRunning();
    tab = Tab::Apps;
    scroll = 0;
    capture(L"apps-running");
    lightAppearance = false;
    ConfigureAppearance();
    capture(L"apps-running-dark");
    lightAppearance = true;
    ConfigureAppearance(true);
    capture(L"apps-running-high-contrast");
    ConfigureAppearance();
    for (float dpiScale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        dpi = 96 * dpiScale;
        envelopeWidth = std::min(900.0f, 1366 / dpiScale);
        envelopeHeight = std::min(760.0f, 768 / dpiScale);
        for (float text : {1.25f, 1.5f, 2.0f, 2.25f}) {
            textScale = text;
            capture(L"apps-running-matrix-" + std::to_wstring(static_cast<int>(dpiScale * 100)) + L"-" +
                    std::to_wstring(static_cast<int>(text * 100)));
        }
    }
    runningWindows = std::move(previousWindows);
    FilterRunning();
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    scroll = 0;
    flyout = Flyout::None;
    tab = Tab::Controls;
    controlState.brightnessSupported = false;
    capture(L"v32-controls-unsupported");
    controlState.radiosAvailable = false;
    capture(L"v32-controls-no-radios");
    controlState.radiosAvailable = true;
    controlState.brightnessSupported = true;
    controlState.dark = true;
    capture(L"v32-controls-dark");
    controlState.dark = false;
    controlState.radiosOff = true;
    capture(L"v32-airplane-off");
    controlState.radiosOff = false;
    note = L"Windows denied radio control. Open Radio settings.";
    capture(L"v32-airplane-denied");
    note.clear();
    flyout = Flyout::Audio;
    capture(L"v32-outputs");
    auto savedOutputs = controlState.outputs;
    controlState.outputs.clear();
    capture(L"v32-outputs-empty");
    controlState.outputs = savedOutputs;
    flyout = Flyout::None;
    tab = Tab::Stats;
    statsMemory = true;
    capture(L"v32-stats-memory");
    statsMemory = false;
    auto savedHardware = hardwareState;
    hardwareState.history = {};
    hardwareState.top.clear();
    hardwareState.volumes.clear();
    capture(L"v32-stats-starting");
    hardwareState = savedHardware;
    tab = Tab::Wallpaper;
    wallpaperSource = 2;
    capture(L"v32-wallpaper-current");
    wallpaperSource = 0;
    wallpaperTarget = wallpaperDisplays.back().id;
    wallpaperFit = DWPOS_FIT;
    capture(L"v32-wallpaper-display-fit");
    for (int mode = 0; mode < 2; ++mode) {
        if (mode)
            ConfigureAppearance(true);
        else {
            lightAppearance = false;
            ConfigureAppearance();
        }
        for (int view = 0; view < 4; ++view) {
            tab = view == 0 || view == 3 ? Tab::Controls : view == 1 ? Tab::Stats : Tab::Wallpaper;
            flyout = view == 3 ? Flyout::Audio : Flyout::None;
            scroll = 0;
            capture(L"v32-theme-" + std::to_wstring(mode) + L"-" + std::to_wstring(view));
        }
    }
    lightAppearance = true;
    ConfigureAppearance();
    for (int scale : {100, 125, 150, 175, 200})
        for (int text : {125, 150, 200, 225}) {
            dpi = 96 * scale / 100.0f;
            textScale = text / 100.0f;
            envelopeWidth = 1366 * 96 / dpi;
            envelopeHeight = 768 * 96 / dpi;
            for (int view = 0; view < 4; ++view) {
                tab = view == 0 || view == 3 ? Tab::Controls : view == 1 ? Tab::Stats : Tab::Wallpaper;
                flyout = view == 3 ? Flyout::Audio : Flyout::None;
                scroll = 0;
                capture(L"v32-matrix-" + std::to_wstring(view) + L"-" + std::to_wstring(scale) + L"-" +
                        std::to_wstring(text));
            }
        }
    flyout = Flyout::None;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    for (int scale : {100, 125, 150, 175, 200})
        for (int text : {125, 150, 200, 225}) {
            UINT actualDpi = static_cast<UINT>(96 * scale / 100);
            float factor = text / 100.0f;
            auto suffix = std::to_wstring(scale) + L"-" + std::to_wstring(text);
            dockCapture(L"dock-magnified-" + suffix, actualDpi, factor, 5);
            dockCapture(L"dock-overflow-" + suffix, actualDpi, factor, 2);
            dockCapture(L"dock-reduced-" + suffix, actualDpi, factor, 6);
            ConfigureAppearance(true);
            dockCapture(L"dock-contrast-" + suffix, actualDpi, factor, 7);
            ConfigureAppearance();
            dpi = static_cast<float>(actualDpi);
            textScale = factor;
            envelopeWidth = 1366 * 96 / dpi;
            envelopeHeight = 768 * 96 / dpi;
            flyout = Flyout::Palette;
            paletteQuery.clear();
            BuildPalette();
            capture(L"palette-matrix-" + suffix);
            paletteQuery = L"No such fixture command";
            BuildPalette();
            capture(L"palette-empty-" + suffix);
            flyout = Flyout::DockStatus;
            capture(L"status-matrix-" + suffix);
            flyout = Flyout::Recent;
            recentDocuments = {{L"Fixture report.pdf", L"fixture:report", L"", L""},
                               {L"日本語 document", L"fixture:document", L"", L""}};
            capture(L"recent-matrix-" + suffix);
            recentDocuments.clear();
            recentMessage = L"No public recent documents. Custom Jump List tasks are unavailable.";
            capture(L"recent-empty-" + suffix);
            flyout = Flyout::Features;
            scroll = 0;
            for (int page = 0; page < 4; ++page) {
                featuresPage = page;
                if (page == 2) {
                    privacySnapshot = {true, 0, 3, {}, {{L"dock", L"window-event-hook", L"running-window-events"}}};
                    privacy::Event request;
                    request.owner = L"today";
                    request.capability = L"network";
                    request.target = L"fixture.invalid";
                    request.reason = L"calendar-subscription";
                    request.result = privacy::Result::Blocked;
                    privacySnapshot.events = {request};
                }
                capture(L"features-" + std::to_wstring(page) + L"-" + suffix);
            }
            moduleRegistry.ApplyPreset(modules::Preset::Taskbar);
            featuresPage = 0;
            capture(L"features-enabled-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
            BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().moduleAction != L"windows.display")
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"features-palette-" + suffix);
            flyout = Flyout::Module;
            moduleView = L"windows";
            modulePage = 0;
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"module-view-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = L"ms-settings:privacy-webcam";
            BuildPalette();
            if (paletteItems.size() != 1 || !paletteItems.front().moduleAction.starts_with(L"windows.page-result-"))
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-link-palette-" + suffix);
            flyout = Flyout::Module;
            moduleQuery = paletteQuery;
            modulePanel = *moduleRegistry.View(moduleView, moduleQuery);
            capture(L"settings-link-island-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = L"ms-settings:defaultapps?registeredAUMID=Example!App";
            BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().label.find(L"Example%21App") == std::wstring::npos)
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-app-parameter-" + suffix);
            paletteQuery =
                L"ms-settings:sound-properties?endpointId={0.0.0.00000000}.{aaaaaaaa-0000-1111-2222-bbbbbbbbbbbb}";
            BuildPalette();
            if (paletteItems.size() != 1)
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"settings-device-parameter-" + suffix);
            moduleQuery.clear();
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_COMMAND_RESTART);
            BuildPalette();
            if (paletteItems.size() != 1 || paletteItems.front().action != RestartNow)
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"system-commands-" + suffix);
            paletteQuery = modules::String(IDS_DISPLAY_SETTINGS);
            moduleRegistry.ApplyPreset(modules::Preset::Minimal);
            BuildPalette();
            if (!paletteItems.empty())
                winrt::throw_hresult(E_UNEXPECTED);
            flyout = Flyout::Module;
            for (const auto *id : {L"utilities", L"clipboard", L"snippets", L"keep-awake", L"files", L"shelf",
                                   L"recycle-bin", L"drives"}) {
                moduleRegistry.Grant(id);
                if (!moduleRegistry.Enable(id))
                    winrt::throw_hresult(E_UNEXPECTED);
            }
            moduleRegistry.Invoke(L"utilities.calculate", window, L"2*(3+4)");
            moduleRegistry.TakeView();
            moduleView = L"utilities";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"utilities-" + suffix);
            moduleView = L"clipboard";
            moduleRegistry.Invoke(L"clipboard.open", window);
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"clipboard-history-" + suffix);
            for (const auto &action : moduleRegistry.Actions())
                if (action.id.starts_with(L"clipboard.preview-") &&
                    action.text.find(modules::String(IDS_CLIP_IMAGE)) != std::wstring::npos) {
                    moduleRegistry.Invoke(action.id, window);
                    break;
                }
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"clipboard-image-" + suffix);
            moduleRegistry.Invoke(L"snippets.add", window, L"Fixture signature | Regards,\\nDako");
            moduleRegistry.TakeView();
            moduleView = L"snippets";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"snippets-" + suffix);
            moduleView = L"keep-awake";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"keep-awake-" + suffix);
            moduleRegistry.Invoke(L"keep-awake.rule-apps", window, L"editor.exe; player.exe");
            modulePanel = *moduleRegistry.View(moduleView, L"");
            modulePage = 7 / ModuleSlots();
            capture(L"keep-awake-rules-" + suffix);
            modulePage = 10 / ModuleSlots();
            capture(L"keep-awake-rule-apps-" + suffix);
            modulePage = 0;
            moduleView = L"recycle-bin";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"recycle-bin-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_RECYCLE_EMPTY);
            BuildPalette();
            if (paletteItems.empty() || paletteItems.front().moduleAction != L"recycle-bin.empty")
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"recycle-bin-palette-" + suffix);
            flyout = Flyout::Module;
            moduleView = L"drives";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"removable-drives-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_DRIVE_EJECT);
            BuildPalette();
            if (paletteItems.empty() || !paletteItems.front().moduleAction.starts_with(L"drives.eject-"))
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"drive-eject-palette-" + suffix);
            paletteQuery.clear();
            flyout = Flyout::Module;
            moduleView = L"shelf";
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"shelf-" + suffix);
            for (const auto &action : moduleRegistry.Actions())
                if (action.id.starts_with(L"shelf.preview-")) {
                    moduleRegistry.Invoke(action.id, window);
                    break;
                }
            moduleRegistry.TakeView();
            modulePanel = *moduleRegistry.View(moduleView, L"");
            capture(L"shelf-item-" + suffix);
            moduleView = L"files";
            modulePanel = *moduleRegistry.View(moduleView, L"report");
            capture(L"indexed-files-" + suffix);
            flyout = Flyout::Palette;
            paletteQuery = modules::String(IDS_FILES_KEYWORD) + L" report";
            BuildPalette();
            if (paletteItems.empty() || !paletteItems.front().moduleAction.starts_with(L"files.result-"))
                winrt::throw_hresult(E_UNEXPECTED);
            capture(L"indexed-palette-" + suffix);
            paletteQuery.clear();
            moduleRegistry.ApplyPreset(modules::Preset::Minimal);
            modulePanel = {};
        }
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    flyout = Flyout::Features;
    for (int appearance = 0; appearance < 2; ++appearance) {
        lightAppearance = false;
        ConfigureAppearance(appearance == 1);
        for (int page = 0; page < 4; ++page) {
            featuresPage = page;
            capture(L"features-theme-" + std::to_wstring(appearance) + L"-" + std::to_wstring(page));
        }
    }
    ConfigureAppearance();
    flyout = Flyout::None;
    dpi = 96;
    textScale = 1;
    envelopeWidth = 900;
    envelopeHeight = 760;
    auto v4SavedTasks = tasks;
    tasks = {{L"Review today's plan", true, nextTask++}};
    currentMedia.available = currentMedia.playing = currentMedia.canPlay = currentMedia.canPrevious =
        currentMedia.canNext = true;
    currentMedia.title = L"lofi beats to study and relax";
    currentMedia.artist = L"lofi radio";
    currentMedia.position = 49;
    currentMedia.duration = 3735;
    auto artwork = std::make_shared<std::vector<BYTE>>(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            auto offset = static_cast<size_t>((y * 64 + x) * 4);
            (*artwork)[offset] = static_cast<BYTE>(120 + x);
            (*artwork)[offset + 1] = static_cast<BYTE>(64 + y / 2);
            (*artwork)[offset + 2] = static_cast<BYTE>(80 + y);
            (*artwork)[offset + 3] = 255;
        }
    currentMedia.artwork = artwork;
    system.wifi = L"Home 5G";
    controlState.dark = true;
    focusSeconds = focusDuration = 300;
    weather.available = true;
    weather.temperature = 32;
    weather.code = 1;
    lightAppearance = false;
    highContrast = false;
    ConfigureAppearance();
    flyout = Flyout::None;
    miniMode = false;
    scroll = 0;
    for (Tab page : PrimaryTabs) {
        tab = page;
        SetExpanded(true);
        capture(L"v4-" + TabTitle());
    }
    tab = Tab::Controls;
    miniMode = true;
    SetExpanded(true);
    capture(L"v4-peek");
    miniMode = false;
    for (int edgeIndex = 0; edgeIndex < 2; ++edgeIndex) {
        edge = edgeIndex ? anchor::Edge::Bottom : anchor::Edge::Top;
        for (UINT scale : {100U, 125U, 150U, 200U}) {
            auto name = L"v4-dock-" + std::to_wstring(edgeIndex) + L"-" + std::to_wstring(scale);
            if (!desktop::DockShell::RenderFixture(directory + L"\\" + name + L".png", scale * 96 / 100, 1,
                                                   20 + edgeIndex))
                winrt::throw_hresult(E_UNEXPECTED);
            ++captures;
        }
    }
    edge = anchor::Edge::Bottom;
    tasks = std::move(v4SavedTasks);
    report << "PASS captures=" << captures
           << "; geometry, stable identity, catalog replacement, transformed scene and 5 DPI tiers. Software fixtures; "
              "no desktop/hardware sign-off."
           << std::endl;
    tab = Tab::Controls;
    SetExpanded(false);
    SaveRender(directory + L"\\compact.png");
    verification.Complete(static_cast<bool>(report));
}
