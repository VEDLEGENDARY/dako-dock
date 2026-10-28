#include "Island.h"

unsigned Island::SystemColor(int index) {
    auto c = GetSysColor(index);
    return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
}

double Island::Luminance(unsigned color) {
    auto channel = [](unsigned v) {
        double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((color >> 16) & 255) + 0.7152 * channel((color >> 8) & 255) + 0.0722 * channel(color & 255);
}

double Island::Contrast(unsigned a, unsigned b) {
    double x = Luminance(a), y = Luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

void Island::LoadSettings() {
    SettingsStore store;
    preferencesPath = store.Path();
    if (!previewMode && !store.Migrate())
        note = L"Could not migrate settings; original files are preserved.";
    auto values = store.Get(L"Dako");
    auto number = [&](const wchar_t *key, int fallback) {
        auto found = values.find(key);
        if (found == values.end())
            return fallback;
        try {
            return std::stoi(found->second);
        } catch (...) {
            return fallback;
        }
    };
    auto today = store.Get(L"Today");
    weatherCity = today[L"WeatherCity"];
    calendarUrl = today[L"CalendarUrl"];
    localEvents = store.Get(L"CalendarEvents");
    RebuildCalendar();
    dockMode = desktop::NormalizeLayout(values[L"layoutMode"]) == L"dock";
    desktopPromptDone = number(L"DesktopPromptDone", 0) != 0;
    desktopRequested = desktopRequested || number(L"DesktopMode", 0) != 0;
    dockOptions.statusMask = static_cast<unsigned>(number(L"DockStatusMask", 31)) & 31;
    dockOptions.showDesktop = number(L"DockShowDesktop", 0) != 0;
    dockOptions.showTaskView = number(L"DockShowTaskView", 0) != 0;
    if (!layoutOverride.empty())
        dockMode = layoutOverride == L"dock";
    LoadV4Settings();
    motionOverride = options.Bool(L"ReducedMotion");
    opaqueMaterial = number(L"Opaque", 1) != 0;
    int appearance = options.Text(L"Theme") == L"Paper" ? 1 : options.Text(L"Theme") == L"System" ? 0 : 2;
    systemAppearance = appearance == 0;
    lightAppearance = appearance == 1;
    browserClearance = static_cast<float>(std::clamp(number(L"BrowserClearance", 112), 64, 240));
    fixedAccent = number(L"FixedAccent", 1) != 0;
    shell.SetMaximizeNew(number(L"MaximizeNew", 0) != 0);
}

bool Island::SaveSettings() {
    if (renderMode || previewMode)
        return true;
    SettingsStore store(preferencesPath);
    auto values = store.Get(L"Dako");
    SettingsStore::Section changed{{L"layoutMode", dockMode ? L"dock" : L"notch"},
                                   {L"DesktopPromptDone", desktopPromptDone ? L"1" : L"0"},
                                   {L"DesktopMode", desktopRequested ? L"1" : L"0"},
                                   {L"DockStatusMask", std::to_wstring(dockOptions.statusMask)},
                                   {L"DockShowDesktop", dockOptions.showDesktop ? L"1" : L"0"},
                                   {L"DockShowTaskView", dockOptions.showTaskView ? L"1" : L"0"},
                                   {L"ReducedMotion", motionOverride ? L"1" : L"0"},
                                   {L"Opaque", opaqueMaterial ? L"1" : L"0"},
                                   {L"Appearance", systemAppearance  ? L"0"
                                                   : lightAppearance ? L"1"
                                                                     : L"2"},
                                   {L"BrowserClearance", std::to_wstring(static_cast<int>(browserClearance))},
                                   {L"MaximizeNew", shell.MaximizeNew() ? L"1" : L"0"},
                                   {L"FixedAccent", fixedAccent ? L"1" : L"0"}};
    for (auto &entry : changed)
        values[entry.first] = entry.second;
    auto shared = options.data;
    shared[L"ReducedMotion"] = motionOverride ? L"1" : L"0";
    shared[L"Theme"] = systemAppearance ? L"System" : lightAppearance ? L"Paper" : L"Midnight";
    shared[L"ReplaceTaskbar"] = desktopRequested ? L"1" : L"0";
    options = v4::Values(shared);
    return store.UpdateSections({{L"Dako", std::move(values)}, {L"V4", std::move(shared)}});
}

void Island::ReadTextScale() {
    if (renderMode)
        return;
    DWORD percent = 100, bytes = sizeof(percent);
    RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Accessibility)", L"TextScaleFactor", RRF_RT_REG_DWORD,
                 nullptr, &percent, &bytes);
    float next = std::clamp(static_cast<float>(percent) / 100, 1.0f, 2.25f) * options.Int(L"Scale") / 100.0f;
    if (next == textScale)
        return;
    textScale = next;
    ReleaseGraphics();
    if (editFont) {
        DeleteObject(editFont);
        editFont = nullptr;
    }
    EnsureEditResources();
    for (HWND input : {taskEdit, appSearch, wifiPassword, clipboardEdit})
        if (input)
            SendMessageW(input, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
}

void Island::ConfigureAppearance(bool forceHighContrast) {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    highContrast = forceHighContrast || (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (systemAppearance && !renderMode) {
        DWORD light = 0, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        lightAppearance = light != 0;
    }
    const auto &colors = lightAppearance ? palette::Paper : palette::Midnight;
    theme::Surface = colors.surface;
    theme::Card = colors.card;
    theme::CardHover = colors.hover;
    theme::Inset = lightAppearance ? 0xE6E3DB : 0x161617;
    theme::Border = colors.hairline;
    theme::Text = colors.text;
    theme::Muted = colors.muted;
    if (highContrast) {
        theme::Surface = SystemColor(COLOR_WINDOW);
        theme::Card = theme::Inset = SystemColor(COLOR_BTNFACE);
        theme::CardHover = SystemColor(COLOR_BTNFACE);
        theme::Text = SystemColor(COLOR_WINDOWTEXT);
        theme::Muted = SystemColor(COLOR_BTNTEXT);
        theme::Border = theme::Text;
    }
    ApplyAccent(fixedAccent ? accentColor : targetAccent);
    ConfigureMaterial();
    if (editBackground) {
        DeleteObject(editBackground);
        editBackground = nullptr;
    }
    EnsureEditResources();
    UpdateDocks();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::ApplyAccent(unsigned rgb) {
    if (fixedAccent)
        rgb = accentColor;
    if (highContrast) {
        rgb = SystemColor(COLOR_HIGHLIGHT);
        theme::Accent = theme::AccentSoft = theme::AccentHover = rgb;
        theme::SelectedText = SystemColor(COLOR_HIGHLIGHTTEXT);
        if (Contrast(theme::SelectedText, rgb) < 4.5)
            theme::SelectedText = Contrast(0, rgb) > Contrast(0xFFFFFF, rgb) ? 0 : 0xFFFFFF;
        theme::AccentInk = theme::SelectedText;
        theme::Focus = SystemColor(COLOR_WINDOWTEXT);
        theme::Secondary = theme::Tertiary = theme::Focus;
        return;
    }
    theme::Accent = rgb;
    theme::AccentHover = dock::Mix(rgb, lightAppearance ? theme::Ink : theme::Paper, 0.10f);
    theme::AccentInk = Contrast(rgb, theme::Ink) > Contrast(rgb, theme::Paper) ? theme::Ink : theme::Paper;
    theme::AccentSoft = dock::Mix(theme::Surface, rgb, 0.12f);
    unsigned readable = rgb;
    for (int i = 0; i < 32 && Contrast(readable, theme::AccentSoft) < 4.5; ++i)
        readable = dock::Mix(readable, lightAppearance ? 0x000000 : 0xFFFFFF, 0.08f);
    theme::SelectedText = theme::Focus = readable;
    theme::Secondary = lightAppearance ? 0x526E62 : 0xB8D890;
    theme::Tertiary = lightAppearance ? 0x546C82 : 0xCEADE6;
}

void Island::SetAccent(unsigned rgb) {
    if (fixedAccent)
        rgb = accentColor;
    if (rgb == targetAccent)
        return;
    previousAccent = theme::Accent;
    targetAccent = rgb;
    paletteBlend.Snap(0);
    paletteBlend.goal = 1;
    if (reducedMotion || renderMode) {
        paletteBlend.Snap(1);
        ApplyAccent(rgb);
    } else
        ArmMotion();
    InvalidateRect(window, nullptr, FALSE);
}

void Island::PreviewTrace(const char *stage) {
    if (!previewMode)
        return;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    auto path = std::filesystem::path(executable).parent_path() / L"preview-startup.txt";
    privacy::Write audit(L"diagnostics", L"file-write", path.wstring(), L"preview-startup-trace");
    std::ofstream trace(path, std::ios::app);
    trace << GetTickCount64() << " " << stage << "\n";
    trace.flush();
    audit.Complete(static_cast<bool>(trace));
}

void Island::Initialize() {
    PreviewTrace("initialize");
    if (previewMode)
        SetPropW(window, L"Dako.IsolatedPreview", reinterpret_cast<HANDLE>(1));
    dpi = static_cast<float>(GetDpiForWindow(window));
    BOOL animation = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
    systemReduced = !animation;
    reducedMotion = systemReduced;
    if (!renderMode)
        LoadSettings();
    if (!renderMode && !fixtureGeometry) {
        privacy::Policy().SetOffline(SettingsStore(preferencesPath).Get(L"Privacy")[L"Offline"] != L"0");
        privacy::Policy().Observe(window);
    }
    modules::RegisterBuiltins(moduleRegistry);
    moduleRegistry.Owner(window);
    moduleRegistry.Mode(renderMode || fixtureGeometry, previewMode);
    if (!renderMode && !fixtureGeometry)
        commandCatalog.Restore(SettingsStore(preferencesPath).Get(L"CommandRanking"));
    if (!renderMode && !fixtureGeometry && !moduleRegistry.Load(SettingsStore(preferencesPath)))
        note = modules::String(IDS_START_FAILED);
    reducedMotion = systemReduced || motionOverride;
    if (options.Bool(L"SafeMode")) {
        desktopRequested = false;
        moduleRegistry.ApplyPreset(modules::Preset::Minimal);
    }
    ReadTextScale();
    ConfigureAppearance();
    CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibleProperties));
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown **>(textFactory.GetAddressOf())));
    bundledFont = std::make_unique<BundledFont>(textFactory.Get());
    const float sizes[]{12, 12, 14, 16, 28, 38, 18, 13, 32, 11};
    ComPtr<IDWriteFontCollection> fontCollection;
    UINT32 iconFamily = 0;
    BOOL hasFluent = FALSE;
    if (SUCCEEDED(textFactory->GetSystemFontCollection(&fontCollection)))
        fontCollection->FindFamilyName(L"Segoe Fluent Icons", &iconFamily, &hasFluent);
    const wchar_t *iconFont = hasFluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    for (size_t i = 0; i < formats.size(); ++i) {
        const wchar_t *family = i == type::Icon                                                   ? iconFont
                                : i == type::Editorial || i == type::Display || i == type::Metric ? L"Rubik"
                                : i == type::Mono                                                 ? L"Consolas"
                                                                                                  : L"Rubik";
        Check(textFactory->CreateTextFormat(
            family, i == type::Icon || i == type::Mono ? nullptr : bundledFont->Collection(),
            i == 3 || i == 7 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, sizes[i], locale, &formats[i]));
        formats[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        textFactory->CreateEllipsisTrimmingSign(formats[i].Get(), &ellipsis);
        formats[i]->SetTrimming(&trim, ellipsis.Get());
    }
    int corner = 1;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    PreviewTrace("native-services");
    if (!renderMode && !fixtureGeometry)
        system.Initialize(window);
    if (!renderMode && !fixtureGeometry) {
        media = std::make_unique<MediaService>(window);
        media->SetVisible(false);
    }
    PreviewTrace("native-services-ready");
    if (!renderMode) {
        catalog = std::make_unique<DesktopCatalog>(window);
        appPreferences.Load();
        for (const auto &task : taskStore.Load())
            tasks.push_back({task.first, task.second, nextTask++});
        if (!previewMode) {
            auto shortcut = options.Text(L"Hotkey");
            UINT shortcutKey = shortcut == L"Space" ? VK_SPACE : static_cast<UINT>(shortcut[0]);
            toggleHotkey = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, shortcutKey) != FALSE;
            quitHotkey = RegisterHotKey(window, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != FALSE;
            paletteHotkey = RegisterHotKey(window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K') != FALSE;
            taskbarHotkey = RegisterHotKey(window, 4, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T') != FALSE;
        }
    }
    if (!renderMode && !fixtureGeometry && !previewMode)
        settingsWatch = std::make_unique<v4::SettingsWatch>(window);
    Place();
    if (!renderMode && !previewMode && !toggleHotkey)
        note = L"Global shortcut unavailable. Use the tray menu.";
    RebuildLayout();
    ConfigureSlideshow();
    PreviewTrace("initialized");
    if (!renderMode && !previewMode && !DockEnabled()) {
        hotZone.Initialize(window);
        hotZone.Place(window, true, static_cast<UINT>(dpi));
    }
}

void Island::ReleaseGraphics() {
    frameClock.Stop();
    frameTickReady = false;
    if (backdropWindow)
        ShowWindow(backdropWindow, SW_HIDE);
    previousPane.Reset();
    wallpaperMask.Reset();
    appIcons.clear();
    thumbnails.clear();
    brush.Reset();
    target.Reset();
    hostTarget.Reset();
    composition.Reset();
    if (scheduler.active && !renderMode && !reducedMotion)
        SetTimer(window, TransitionTimer, motionInterval, nullptr);
    if (!renderMode)
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_NOREDIRECTIONBITMAP);
}

void Island::EnsureEditResources() {
    if (!editFont)
        editFont = CreateFontW(-static_cast<int>(std::lround(13 * textScale * dpi / 96)), 0, 0, 0, FW_NORMAL, FALSE,
                               FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    if (!editBackground)
        editBackground = CreateSolidBrush(
            RGB((theme::CardHover >> 16) & 255, (theme::CardHover >> 8) & 255, theme::CardHover & 255));
}

Island::~Island() {
    speedTest.reset();
    settingsWatch.reset();
    clipboardReadTask.Stop();
    if (clipboardReadToken)
        privacy::Policy().Unwatch(L"clipboard-transfer", static_cast<uintptr_t>(clipboardReadToken));
    moduleRegistry.Stop();
    StopDesktop();
    frameClock.Stop();
    if (backdropWindow)
        DestroyWindow(backdropWindow);
    if (editFont)
        DeleteObject(editFont);
    if (editBackground)
        DeleteObject(editBackground);
}
