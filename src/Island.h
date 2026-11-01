#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <shcore.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wlanapi.h>
#include <bluetoothapis.h>
#include <psapi.h>
#include <shellapi.h>
#include <oleacc.h>
#include <wincodec.h>
#include <powrprof.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "DockMotion.h"
#include "SurfaceMotion.h"
#include "Anchor.h"
#include "IslandState.h"
#include "SettingsSchema.h"
#include "SettingsCore.h"
#include "BundledFont.h"
#include "UiTypography.h"
#include "GeometryCache.h"
#include "AmbientShadow.h"
#include "DepthCache.h"
#include "NativeChrome.h"
#include "SpeedTest.h"
#include "LayoutEngine.h"
#include "MotionController.h"
#include "CompositionSurface.h"
#include "FrameClock.h"
#include "AsyncWait.h"
#include <map>
#include <set>
#include <cstdint>
#include "DesktopShell.h"
#include "DockShell.h"
#include "WindowList.h"

using Microsoft::WRL::ComPtr;
using std::chrono::duration;
using std::chrono::steady_clock;
using namespace winrt::Windows::Media::Control;
constexpr UINT StateChanged = WM_APP + 1;
constexpr UINT InvokeControl = WM_APP + 2;
constexpr UINT FocusControl = WM_APP + 3;
constexpr UINT AudioDeviceChanged = WM_APP + 5;
constexpr UINT RevealDock = WM_APP + 7;
constexpr UINT MotionFrame = WM_APP + 31;
constexpr UINT_PTR TransitionTimer = 1, StatsTimer = 2, MediaTimer = 3, InteractionTimer = 4, FocusTimer = 5,
                   ConnectivityTimer = 6, ToastTimer = 7, ScrollFadeTimer = 8;
constexpr wchar_t WindowClass[] = L"Dako.Native.Island";

inline void Check(HRESULT result) {
    if (FAILED(result))
        winrt::throw_hresult(result);
}
inline std::wstring Number(double value, int digits = 0) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.*f", digits, value);
    wchar_t decimal[8]{};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, decimal, 8) && decimal[0] != L'.') {
        std::wstring result = buffer;
        auto at = result.find(L'.');
        if (at != std::wstring::npos)
            result.replace(at, 1, decimal);
        return result;
    }
    return buffer;
}
inline std::wstring Wide(const char *value, int length) {
    int size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    std::wstring result(size, L' ');
    if (size)
        MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), size);
    return result;
}

#include "EffectsJournal.h"
#include "NativeServices.h"
#include "SettingsStore.h"
#include "WeatherService.h"
#include "CalendarService.h"
#include "NotchHotZone.h"
#include "PromptText.h"
#include "DesktopCatalog.h"
#include "OneShotDeadline.h"
#include "SlideshowPlan.h"
#include "HardwareStats.h"
#include "ControlsService.h"
#include "Connectivity.h"
#include "ParityServices.h"
#include "ModuleRegistry.h"
#include "ClipboardTask.h"
#include "CommandCatalog.h"
#include "ModuleStrings.h"

enum class Tab { Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power };
inline constexpr std::array PrimaryTabs{Tab::Controls, Tab::Today,     Tab::Stats, Tab::Clipboard,
                                        Tab::Apps,     Tab::Wallpaper, Tab::Power};
enum class Flyout { None, Wifi, Bluetooth, Audio, Palette, DockStatus, Recent, Features, Module };
enum Action {
    Expand = 1,
    Wifi = 20,
    Bluetooth,
    Audio,
    Night,
    Dark,
    Volume,
    Mute,
    Previous,
    Play,
    Next,
    AddTask = 60,
    ClipboardRefresh,
    ClipboardClear,
    WallpaperSettings,
    PowerSettings,
    Lock,
    Quit,
    Minimize,
    AppsPrevious,
    AppsNext,
    WallpaperFolder,
    WallpaperPrevious,
    WallpaperNext,
    WallpaperApply,
    WallpaperCollection,
    SleepNow = 80,
    RestartNow,
    ShutdownNow,
    FlyoutBack = 600,
    FlyoutRefresh,
    FlyoutToggle,
    FlyoutSettings,
    FlyoutConnect,
    FlyoutDisconnect,
    FlyoutPair,
    FlyoutRemove,
    FlyoutPrevious,
    FlyoutNext,
    FocusStart = 700,
    FocusReset,
    FocusShort,
    FocusMedium,
    FocusLong,
    FocusOne,
    FocusTen,
    FocusTwenty,
    WeatherCity = 720,
    CalendarFeed,
    CalendarAdd,
    TodayClocks,
    CalendarPrevious,
    CalendarNext,
    TodayOptions,
    CalendarClear,
    RunningPrevious = 740,
    RunningNext,
    Brightness = 750,
    Airplane,
    ProjectDisplay,
    ScreenSnip,
    AirplaneSettings,
    OutputSettings,
    OutputPrevious,
    OutputNext,
    WallpaperCurrent,
    WallpaperTarget,
    WallpaperFit,
    StatsSort
};
struct Hit {
    int id;
    D2D1_RECT_F rect;
    std::wstring name;
    bool enabled = true;
    std::wstring key;
    LONG child = 0;
    float skew = 0, radius = layout::ControlRadius;
    D2D1_RECT_F clip{};
    HWND input = nullptr;
};
inline bool ReadingControl(int id) { return id == 1800 || (id >= 2000 && id < 2300) || (id >= 6000 && id < 8000); }
struct Task {
    std::wstring title;
    bool done = false;
    unsigned identity = 0;
};
struct ModalGuard {
    bool &flag;
    explicit ModalGuard(bool &value) : flag(value) { flag = true; }
    ~ModalGuard() { flag = false; }
};

class Island {
  public:
    HWND window = nullptr;
    bool dockMode = true, desktopRequested = false, desktopPromptDone = false, paletteHotkey = false;
    desktop::DockOptions dockOptions;
    POINT dockAnchor{};
    void ConfirmDesktopChoice();
    bool PrepareSystemChange();
    void DockPopover();
    void Palette();
    size_t PaletteSlots() const;
    size_t ModuleSlots() const;
    void SetSystemVolume(float value);
    void ToggleSystemMute();
    void OpenPalette();
    void BuildPalette();
    void ExecutePalette(size_t slot);
    modules::Registry moduleRegistry;
    ::clipboard::Task clipboardReadTask;
    uint64_t clipboardReadToken = 0;
    int featuresPage = 0;
    privacy::Snapshot privacySnapshot;
    size_t privacyPage = 0;
    std::map<std::wstring, modules::Cost> moduleCosts;
    void Features();
    void OpenFeatures();
    void FeatureAction(int id);
    void TransferSettings(bool importing);
    bool ConfirmModule(const std::wstring &id);
    void InvokeModuleAction(const std::wstring &id);
    void OpenModule(const std::wstring &id);
    void ModuleView();
    std::wstring moduleView, moduleQuery;
    modules::Panel modulePanel;
    size_t modulePage = 0;
    struct PaletteItem {
        std::wstring key, label, payload;
        int action = 0;
        HWND target = nullptr;
        std::wstring moduleAction;
    };
    command::Catalog commandCatalog;
    void ExecuteCommand(const PaletteItem &item);
    std::vector<PaletteItem> paletteItems;
    std::wstring paletteQuery;
    size_t palettePage = 0;
    std::unique_ptr<parity::RecentDocuments> recentService;
    std::vector<parity::Document> recentDocuments;
    std::wstring recentMessage;
    size_t recentPage = 0;
    void RecentView();
    void OpenRecent(const std::wstring &appId, HMONITOR monitor);

    std::wstring layoutOverride;
    HMONITOR panelMonitor = nullptr;
    std::unique_ptr<desktop::DockShell> docks;
    std::unique_ptr<desktop::WindowList> windowList;
    std::vector<desktop::WindowEntry> runningWindows;
    std::vector<size_t> runningIndices;
    size_t runningPage = 0;
    UINT shellHookMessage = RegisterWindowMessageW(L"SHELLHOOK");
    bool shellHookRegistered = false;
    std::array<bool, 9> numberHotkeys{};
    bool DockEnabled() const;
    void StartDesktop();
    void StopDesktop();
    void ToggleDesktopMode();
    void UpdateDocks();
    void OpenFromDock(int action, HMONITOR monitor);
    surface::Request surfaceRequest;
    surface::HoverPolicy surfaceHover;
    desktop::DockGroup surfaceGroup;
    std::unique_ptr<OneShotDeadline> surfaceDeadline;
    static constexpr UINT SurfaceDue = WM_APP + 100;
    HMONITOR hoverMonitor = nullptr;
    desktop::DockGroup hoverGroup;
    std::array<HTHUMBNAIL, 3> surfaceThumbnails{};
    std::array<D2D1_RECT_F, 3> thumbnailSlots{};
    void RequestSurface(surface::Request request, desktop::DockGroup group, HMONITOR monitor);
    void PresentSurface(surface::Request request, desktop::DockGroup group, HMONITOR monitor);
    void SurfacePointer(bool inside);
    void SurfaceDeadline(WPARAM token);
    void UnifiedContent();
    bool UnifiedAction(int id);
    float UnifiedHeight() const;
    void CapturePane();
    void UpdateSurfaceThumbnails();
    void ClearSurfaceThumbnails();
    ComPtr<ID2D1Effect> surfaceShadow;
    void LaunchPin(const std::wstring &key);
    void ReorderPin(size_t from, size_t to);
    void PinToDock(const std::wstring &key, bool pinned);
    void FilterRunning();
    float AppsListTop() const;
    NotchHotZone hotZone;
    std::unique_ptr<WeatherService> weatherService;
    std::unique_ptr<CalendarService> calendarService;
    WeatherState weather;
    std::wstring weatherCity, calendarUrl, subscriptionIcs, calendarMessage;
    SettingsStore::Section localEvents;
    std::vector<calendar::Event> calendarEvents;
    float taskScroll = 0;
    int monthOffset = 0;
    int todayDateKey = 0, lastHeaderMinute = -1;
    void RefreshToday();
    void RebuildCalendar();
    void TodayAction(int id);
    void TodayMenu();
    void ScrollTasks(float amount);
    D2D1_RECT_F TaskListRect() const;

    std::unique_ptr<controls::Service> nativeControls;
    controls::Snapshot controlState;
    bool draggingBrightness = false, statsMemory = true;
    size_t outputPage = 0;
    int wallpaperSource = 0;
    std::wstring wallpaperFolder, wallpaperTarget;
    std::vector<WallpaperDisplay> wallpaperDisplays;
    DESKTOP_WALLPAPER_POSITION wallpaperFit = DWPOS_FILL;
    bool wallpaperTargetChosen = false, wallpaperFitChosen = false;
    void RefreshControls();
    void ControlAction(int id);
    void OutputDevices();
    D2D1_RECT_F BrightnessRect() const;
    void SetBrightnessAt(int x, bool commit);
    void Sparkline(D2D1_RECT_F rect, int metric, unsigned color);
    size_t WallpaperSlots() const;
    void WallpaperSource(int source);
    void WallpaperAction(int id);
    bool fixtureAudioAvailable = true;
    bool fixtureGeometry = false, previewMode = false, layoutOnly = false, buildingBody = false, motionOverride = false,
         opaqueMaterial = true, highContrast = false, lightAppearance = true, systemAppearance = false;
    bool backdropAvailable = false, systemReduced = false;
    HRESULT backdropResult = E_PENDING;
    DWORD systemTransparency = 1;
    ComPtr<ID2D1RoundedRectangleGeometry> wallpaperMask;
    ComPtr<ID2D1StrokeStyle> ringStroke;
    mutable graphics::GeometryCache geometryCache;
    graphics::AmbientShadow shadow;
    graphics::DepthCache depth;
    nativechrome::Host chromeHost;
    enum class ChromeKind { None, Confirm, Input, Menu };
    struct MenuEntry {
        std::wstring label;
        int command = 0;
        bool enabled = true, checked = false;
        std::vector<MenuEntry> children;
    };
    struct Chrome {
        ChromeKind kind = ChromeKind::None;
        std::wstring title, detail, value, primary, restoreFocus;
        std::vector<MenuEntry> items;
        std::vector<std::pair<std::wstring, std::vector<MenuEntry>>> history;
        D2D1_POINT_2F anchor{};
        float scroll = 0, height = 160, detailHeight = 32;
        size_t offset = 0, slots = 5;
        int result = IDCANCEL, accept = IDOK, cancel = IDCANCEL;
        bool closing = false, danger = false, savedModal = false;
        dock::Spring blend{0};
    } chrome;
    HWND chromeEdit = nullptr;
    static constexpr int ChromeInput = 404, ChromeCancel = 9600, ChromeAccept = 9601;
    static constexpr int ChromeRow = 9700, ChromeBack = 9790, ChromePrevious = 9791, ChromeNext = 9792;
    bool ChromeActive() const { return chrome.kind != ChromeKind::None; }
    void BeginChrome(ChromeKind kind, const std::wstring &title, const std::wstring &detail,
                     const std::wstring &initial = L"", size_t limit = 2048);
    void DrawChrome();
    void FinishChrome(int result);
    void CloseChrome();
    int WaitChrome();
    bool ChromeAction(int id);
    bool ChromeKey(WPARAM key);
    void ScrollChrome(float amount);
    int MessageBoxW(HWND owner, const wchar_t *text, const wchar_t *title, UINT flags);
    std::optional<std::wstring> PromptText(HWND owner, const std::wstring &title, const std::wstring &help,
                                           const std::wstring &initial, size_t limit = 2048);
    static std::vector<MenuEntry> ReadMenu(HMENU menu);
    int TrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved, HWND owner, const RECT *exclude);
    int OpenMenu(std::vector<MenuEntry> entries, D2D1_POINT_2F anchor, const std::wstring &title = L"Menu");
    HWND backdropWindow = nullptr;
    RECT backdropPlacement{};
    HMONITOR refreshMonitor = nullptr;
    std::array<int, 9> regionSignature{-1, -1, -1, -1, -1, -1, -1, -1, -1};
    float envelopeWidth = theme::EnvelopeWidth, scroll = 0, pointerX = -10000, textScale = 1;
    float observedScroll = 0;
    dock::Spring scrollThumb{0};
    ComPtr<ID2D1LinearGradientBrush> scrollFade;
    void ScrollDecoration();
    void EmptyState(D2D1_RECT_F rect, const wchar_t *glyph, const std::wstring &title, const std::wstring &hint,
                    int readout = 1980);
    dock::Scheduler scheduler;
    FrameClock frameClock;
    bool frameTickReady = false;
    UINT motionInterval = 16;
    dock::Spring wallpaperParallax{0}, volumeMotion{0};
    dock::Spring focusBlend{1};
    std::wstring paintedFocus;
    std::map<std::wstring, dock::Spring> pressMotion;
    std::map<unsigned, dock::Spring> taskMotion;
    float paneClock = 1, loadingClock = 0;
    bool paintClosing = false, tabMotionReady = false;
    bool VisibleLoading() const;
    void Shimmer(D2D1_RECT_F rect);
    ComPtr<ID2D1LinearGradientBrush> shimmerBrush;
    struct PaintGroup {
        Island &app;
        D2D1::Matrix3x2F saved;
        bool active = false;
        PaintGroup(Island &island, int index);
        ~PaintGroup();
    };
    std::array<int, 3> percentValues{-1, -1, -1};
    std::array<std::wstring, 3> percentText;
    std::wstring diagnosticsPath;
    std::vector<double> frameIntervals, paintTimes;
    std::function<void()> beforePresent;
    dock::Spring tabLeft{20}, tabRight{100}, railPointer{-10000}, railStrength{0}, paletteBlend{1}, togglePosition{0};
    std::array<dock::Spring, 4> rings{dock::Spring(0), dock::Spring(0), dock::Spring(0), dock::Spring(0)};
    std::map<std::wstring, dock::Feedback> feedback;
    std::map<std::wstring, dock::Spring> rowPositions;
    std::wstring hoveredKey, focusedKey, pressedKey, pendingWallpaper, appliedWallpaper, taskDraft, lastStatus;
    std::map<std::wstring, LONG> childIds;
    LONG nextChild = 1;
    unsigned nextTask = 1;
    int pressedId = 0;
    POINT moduleDragStart{};
    DWORD draftStart = 0, draftEnd = 0;
    std::vector<layout::AppPage> appPages;
    std::vector<size_t> railApps;
    std::wstring clipboardState = L"Not read";
    size_t clipboardOffset = 0;
    unsigned previousAccent = theme::Signal, targetAccent = theme::Signal;
    std::wstring wifiIdentity, bluetoothIdentity;
    float bodyDirection = 1;
    layout::Result Layout() const;
    static D2D1_RECT_F R(layout::Rect r);
    D2D1_RECT_F Body() const;
    D2D1_RECT_F Footer() const;
    bool CanInvoke(const Hit &hit) const;
    // Input follows the presented surface; spring settlement must never gate clicks.
    bool Actionable() const;
    int IndexForKey(const std::wstring &key) const;
    int IndexForChild(LONG child) const;
    std::wstring StableKey(int id) const;
    static std::wstring WifiKey(const WifiNetwork &n);
    static std::wstring BluetoothKey(const BluetoothEntry &d);
    size_t AppIndex(size_t slot) const;
    bool VisibleHit(const Hit &h) const;
    void ReplaceWallpapers(std::vector<WallpaperEntry> next);
    void RequestVisibleIcons();
    void RebuildLayout();
    void RefreshGeometry();
    void SnapMotion();

    float dpi = 96, width = theme::DockWidth, height = theme::DockHeight;
    bool expanded = false, transitioning = false, tracking = false, draggingVolume = false;
    Tab tab = Tab::Today;
    Flyout flyout = Flyout::None;
    surface::SurfaceMotion surfaceMotion;
    dock::Spring &dockReveal = surfaceMotion.reveal, &dockWidth = surfaceMotion.width,
                 &dockHeight = surfaceMotion.height, &paneBlend = surfaceMotion.blend,
                 &contentBlend = surfaceMotion.content;
    dock::Spring wallpaperFocus{0};
    island::Machine interaction;
    bool applyingInteraction = false, interactionTicking = false;
    v4::Values options;
    anchor::Edge edge = anchor::Edge::Bottom;
    unsigned accentColor = 0xCFAE60;
    std::vector<Tab> visibleTabs;
    std::unique_ptr<v4::SettingsWatch> settingsWatch;
    std::unique_ptr<SpeedTest> speedTest;
    void SettingsUpdated(WPARAM preview);
    void OpenSettings();
    void LoadV4Settings();
    void ApplyV4Values();
    void UpdateInteractionMedia();
    void ApplyInteraction();
    void InteractionPointer(bool inside);
    void UpdateInteractionTimer();
    std::map<int, dock::Spring> hoverMotion, tileState;
    dock::Spring composerFocus{0};
    steady_clock::time_point lastMotion = steady_clock::now(), lastInteraction = steady_clock::now();
    float envelopeHeight = theme::EnvelopeHeight;
    ComPtr<ID2D1Bitmap> previousPane;
    float previousPaneWidth = 0, previousPaneHeight = 0;
    bool capturingPane = false, modalOpen = false, keyboardOpen = false, launchPreview = false;
    std::unique_ptr<Connectivity> connectivity;
    ConnectionState connections;
    int selectedWifi = -1, selectedBluetooth = -1;
    size_t connectionPage = 0;
    HWND wifiPassword = nullptr;
    int focusSeconds = 300, focusDuration = 300;
    bool focusRunning = false;
    bool statsTicking = false, mediaTicking = false, focusTicking = false, connectionsTicking = false;
    ULONGLONG focusDeadline = 0;
    int hovered = -1, focused = -1;
    std::vector<Hit> hits;
    std::vector<Task> tasks;
    TaskStore taskStore;
    std::unique_ptr<DesktopCatalog> catalog;
    std::unique_ptr<DesktopCatalog> slideshowCatalog;
    std::unique_ptr<OneShotDeadline> slideshowDeadline;
    SlideshowPlan slideshow;
    std::vector<WallpaperEntry> slideshowPictures;
    size_t slideshowIndex = 0;
    void ConfigureSlideshow();
    void SlideshowReady(LPARAM generation);
    void RunSlideshow(WPARAM generation);
    void SlideshowFinished(WPARAM result, LPARAM detail);
    bool appsRequested = false, wallpapersRequested = false, appsLoading = false, wallpaperLoading = false;
    std::vector<AppEntry> apps;
    std::vector<WallpaperEntry> wallpapers;
    std::vector<ComPtr<ID2D1Bitmap>> thumbnails;
    std::map<ID2D1Bitmap *, ComPtr<ID2D1BitmapBrush>> imageBrushes;
    void Photo(ID2D1Bitmap *bitmap, D2D1_RECT_F rect, float opacity = 1, float parallax = 0);
    std::vector<size_t> filteredApps;
    std::wstring appQuery, selectedAppKey;
    size_t appPage = 0, selectedWallpaper = 0;
    std::wstring clipboard = L"Open this tab to read the current clipboard.", note;
    std::unique_ptr<MediaService> media;
    SystemState system;
    MediaState currentMedia;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> textFactory;
    std::shared_ptr<type::Library> typography;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1HwndRenderTarget> hostTarget;
    CompositionSurface composition;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::array<ComPtr<IDWriteTextFormat>, type::Count> formats;
    struct TextEntry {
        std::wstring text;
        float width = 0, height = 0;
        int role = 0;
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
        ComPtr<IDWriteTextLayout> layout;
    };
    std::deque<TextEntry> textLayouts;
    float startWidth = 300, startHeight = 100, endWidth = 300, endHeight = 100;
    steady_clock::time_point transitionStart;
    bool toggleHotkey = false, quitHotkey = false;
    bool reducedMotion = false;
    bool renderMode = false;
    HWND taskEdit = nullptr;
    HWND appSearch = nullptr;
    HFONT editFont = nullptr;
    HBRUSH editBackground = nullptr;
    ComPtr<IAccessible> accessible;
    ComPtr<IAccPropServices> accessibleProperties;
    desktop::Shell shell;
    bool taskbarHotkey = false;
    HWND contextWindow = nullptr;
    bool browserContext = false, fullscreenContext = false;
    bool YieldResting() const;
    bool wallpaperApplying = false, miniMode = false;
    std::unique_ptr<HardwareStats> hardware;
    HardwareSnapshot hardwareState;
    AppPreferences appPreferences;
    std::wstring AppKey(const AppEntry &app) const;
    bool Favorite(const AppEntry &app) const;
    int AppGroup(const AppEntry &app) const;
    void SampleStats();
    static unsigned SystemColor(int index);
    static double Luminance(unsigned color);
    static double Contrast(unsigned a, unsigned b);
    std::wstring preferencesPath;
    bool fixedAccent = true;
    void LoadSettings();
    bool SaveSettings();
    void ReadTextScale();
    void ConfigureAppearance(bool forceHighContrast = false);
    void ApplyAccent(unsigned rgb);
    void SetAccent(unsigned rgb);
    void UpdateContext();

    ~Island();
    void PreviewTrace(const char *stage);
    void Initialize();
    void ReleaseGraphics();
    float LayoutWidth() const;
    float LayoutHeight() const;
    float SurfaceWidth() const;
    float SurfaceHeight() const;
    float ContentWidth() const;
    float PanelX() const;
    shape::Rect DockPill() const;
    shape::Rect DockExtension(float w, float h) const;
    ID2D1PathGeometry *SurfacePath() const;
    float HandleY() const;
    float PanelY() const;
    float browserClearance = 112;
    std::vector<POINT> NotchPixels(float scale) const;
    void PaintNotch(HDC dc);
    void DrawNotch();
    std::vector<POINT> AttachedPixels(float scale) const;
    void IslandSurface();
    static LRESULT CALLBACK BackdropProcedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam);
    void PositionBackdrop();
    void PositionShadow();
    void ConfigureMaterial();
    void Graphics();
    void Place();
    void PositionInputs();
    void ArmMotion();
    void SetExpanded(bool value, bool deliberate = true);
    void Transition();
    void InteractionTick();
    void UpdateTimers();
    void SetTab(Tab value);
    D2D1_COLOR_F Color(unsigned rgb, float alpha = 1);
    void Fill(D2D1_RECT_F rect, unsigned color, float radius = layout::Metrics::ControlRadius);
    void Card(D2D1_RECT_F rect, int level = 1, float hover = 0, float radius = layout::CardRadius);
    void Text(const std::wstring &text, D2D1_RECT_F rect, int format = type::Body, unsigned color = theme::Text,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    void TwoLine(D2D1_RECT_F rect, const std::wstring &title, const std::wstring &subtitle,
                 unsigned color = theme::Text, int role = type::Title);
    void ListRow(int id, D2D1_RECT_F rect, const std::wstring &title, const std::wstring &subtitle = L"",
                 bool selected = false, bool enabled = true);
    void Line(float x1, float y1, float x2, float y2, unsigned color, float thickness = 1);
    void AddHit(int id, D2D1_RECT_F rect, std::wstring name, bool enabled = true);
    void FocusRing(int id, D2D1_RECT_F rect, float radius = layout::ControlRadius);
    bool Hover(int id) const;
    float HoverAmount(int id) const;
    void SetHover(int next);
    D2D1_RECT_F Tactile(int id, D2D1_RECT_F rect);
    void Button(int id, D2D1_RECT_F rect, const std::wstring &title, const std::wstring &subtitle, const wchar_t *icon,
                bool active = false, bool enabled = true);
    void Paragraph(const std::wstring &text, D2D1_RECT_F rect, unsigned color = theme::Muted, int format = type::Body);
    float RowY(const std::wstring &key, float goal);
    std::wstring Clock(double seconds);
    void MediaCard(bool compact);
    D2D1_RECT_F TabRect(int index) const;
    void CompactHeader(bool wireless = false);
    std::wstring TabTitle() const;
    void Tabs();
    const std::vector<Tab> &EnabledTabs();
    D2D1_RECT_F VolumeRect() const;
    D2D1_RECT_F VolumeTrack() const;
    ComPtr<ID2D1Bitmap> mediaBitmap;
    std::shared_ptr<std::vector<BYTE>> mediaPixels;
    bool HasAudio() const;
    void Controls();
    D2D1_RECT_F TaskCard() const;
    D2D1_RECT_F TaskInputRect() const;
    void Today();
    void DrawRing(float x, float y, float radius, float value, unsigned color, float thickness);
    void TextButton(int id, D2D1_RECT_F rect, const std::wstring &text, bool primary = false, bool enabled = true);
    void Stats();
    void ReadClipboard();
    HWND clipboardEdit = nullptr;
    D2D1_RECT_F ClipboardRect() const;
    void EnsureClipboardEdit();
    void ClipboardView();
    void ClipboardAction(int id);
    std::wstring clipboardQuery;
    int clipboardFilter = 0;
    uint64_t clipboardSelected = 0;
    HWND clipboardPasteTarget = nullptr;
    std::vector<clipboard::Item> clipboardRecords;
    std::set<uint64_t> clipboardSelection;

    void RetargetLayout();
    void FilterApps(bool reset = true);
    bool CompactApps() const;
    bool ShowAppRail() const;
    void RepageApps();
    size_t AppSlots() const;
    D2D1_RECT_F SearchRect() const;
    void Apps();
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> appIcons;
    void DrawAppIcon(const AppEntry &app, D2D1_RECT_F rect);
    void SmallButton(int id, D2D1_RECT_F rect, const wchar_t *icon, const std::wstring &label, bool enabled = true);
    void Wallpaper();
    void Power();
    void OpenFlyout(Flyout next);
    void EnsurePassword();
    size_t ConnectionSlots() const;
    void ConnectionView();

    void RefreshConnectionsView();
    void PanelContent(bool header = true);
    void PageContent(bool header);
    dock::Spring toastBlend{0};
    bool toastVisible = false;
    void DrawToast();
    void Draw();

    bool PresentedContains(float x, float y) const;
    int HitAt(float x, float y, bool accessibleHit = false);
    void SetVolumeAt(int x);

    void Open(const wchar_t *path);
    void LabelInput(HWND input, const wchar_t *name);
    void EnsureTaskEdit();
    void EnsureEditResources();
    void EnsureAppSearch();
    bool SaveTasks();
    void OpenApp(const AppEntry &app);
    void ChooseWallpaperFolder();
    void PowerAction(int id);
    void Invoke(int id);
    void FocusAt(int index);
    void Key(WPARAM key);
    void InspectDetails();
    void OpenCompactMenu(bool views);
    void ContextMenu();
    // A deterministic render target for visual QA of our own UI, without capturing the desktop.
    void SaveRender(const std::wstring &path);
    void AssertScene();
    void RenderSuite(const std::wstring &directory);
};

IAccessible *CreateIslandAccessible(HWND window);
void VerifyAccessibility(Island &app);
LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);
