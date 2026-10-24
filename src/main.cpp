#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
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
#include "DesktopShell.h"

using Microsoft::WRL::ComPtr;
using std::chrono::steady_clock;
using std::chrono::duration;
using namespace winrt::Windows::Media::Control;
constexpr UINT StateChanged = WM_APP + 1;
constexpr UINT InvokeControl = WM_APP + 2;
constexpr UINT FocusControl = WM_APP + 3;
constexpr UINT AudioDeviceChanged = WM_APP + 5;
constexpr UINT RevealDock = WM_APP + 7;
constexpr UINT_PTR TransitionTimer = 1, StatsTimer = 2, MediaTimer = 3, ProximityTimer=4, FocusTimer=5, ConnectivityTimer=6;
constexpr wchar_t WindowClass[] = L"Dako.Native.Island";

void Check(HRESULT result) { if (FAILED(result)) winrt::throw_hresult(result); }
std::wstring Number(double value, int digits = 0) {
    wchar_t buffer[64]; swprintf_s(buffer, L"%.*f", digits, value); return buffer;
}
std::wstring Wide(const char* value, int length) {
    int size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    std::wstring result(size, L' ');
    if (size) MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), size);
    return result;
}

struct MediaState {
    bool available = false, playing = false;
    std::wstring title = L"Nothing playing", artist = L"Play music in a supported app";
    double position = 0, duration = 0;
};

// All asynchronous media work lives in one MTA, never on the window thread.
class MediaService {
    HWND window_;
    std::mutex mutex_, stateMutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    MediaState state_;
    GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    GlobalSystemMediaTransportControlsSession session_{nullptr};
    winrt::event_token currentToken_{}, propertiesToken_{}, playbackToken_{}, timelineToken_{};
    std::atomic<bool> visible_{true};
    std::thread worker_;

    void Queue(std::function<void()> job) {
        { std::scoped_lock lock(mutex_); if (stopping_) return; jobs_.push_back(std::move(job)); }
        ready_.notify_one();
    }
    void DetachSession() {
        if (!session_) return;
        session_.MediaPropertiesChanged(propertiesToken_);
        session_.PlaybackInfoChanged(playbackToken_);
        session_.TimelinePropertiesChanged(timelineToken_);
        session_ = nullptr;
    }
    void Refresh(bool readProperties=true) {
        MediaState next;
        if(!readProperties){std::scoped_lock lock(stateMutex_);next=state_;}
        try {
            if (session_) {
                if(readProperties){auto metadata=session_.TryGetMediaPropertiesAsync().get();
                    next.title=metadata.Title().empty()?L"Untitled media":std::wstring(metadata.Title());
                    next.artist=metadata.Artist().empty()?std::wstring(session_.SourceAppUserModelId()):std::wstring(metadata.Artist());}
                auto playback = session_.GetPlaybackInfo();
                auto timeline = session_.GetTimelineProperties();
                next.available = true;
                next.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                next.position = duration<double>(timeline.Position()).count();
                next.duration = duration<double>(timeline.EndTime() - timeline.StartTime()).count();
            }else next=MediaState{};
        } catch (const winrt::hresult_error&) { next.artist = L"Media session unavailable"; }
        { std::scoped_lock lock(stateMutex_); state_ = std::move(next); }
        PostMessageW(window_, StateChanged, 0, 0);
    }
    void AttachSession() {
        DetachSession();
        session_ = manager_.GetCurrentSession();
        if (session_) {
            propertiesToken_ = session_.MediaPropertiesChanged([this](auto&&, auto&&) { if(visible_)Queue([this] { Refresh(); }); });
            playbackToken_ = session_.PlaybackInfoChanged([this](auto&&, auto&&) { if(visible_)Queue([this] { Refresh(false); }); });
            timelineToken_ = session_.TimelinePropertiesChanged([this](auto&&, auto&&) { if(visible_)Queue([this] { Refresh(false); }); });
        }
        if(visible_)Refresh();
    }
    void Run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            currentToken_ = manager_.CurrentSessionChanged([this](auto&&, auto&&) { Queue([this] { AttachSession(); }); });
            AttachSession();
        } catch (const winrt::hresult_error&) {
            { std::scoped_lock lock(stateMutex_); state_.artist = L"Windows media access unavailable"; }
            PostMessageW(window_, StateChanged, 0, 0);
        }
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) break;
                job = std::move(jobs_.front()); jobs_.pop_front();
            }
            try { job(); } catch (const winrt::hresult_error&) { Refresh(); }
        }
        DetachSession();
        if (manager_) manager_.CurrentSessionChanged(currentToken_);
        manager_ = nullptr;
        winrt::uninit_apartment();
    }
public:
    explicit MediaService(HWND window) : window_(window), worker_([this] { Run(); }) {}
    ~MediaService() {
        { std::scoped_lock lock(mutex_); stopping_ = true; }
        ready_.notify_one(); worker_.join();
    }
    MediaState Snapshot() { std::scoped_lock lock(stateMutex_); return state_; }
    void SetVisible(bool value){bool previous=visible_.exchange(value);if(value&&!previous)Queue([this]{Refresh();});}
    void Transport(int action) {
        Queue([this, action] {
            if (!session_) return;
            if (action == 0) session_.TrySkipPreviousAsync().get();
            else if (action == 1) session_.TryTogglePlayPauseAsync().get();
            else session_.TrySkipNextAsync().get();
            Refresh();
        });
    }
};

class VolumeSink : public IAudioEndpointVolumeCallback {
    std::atomic<ULONG> references_{1};
    HWND window_;
public:
    explicit VolumeSink(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IAudioEndpointVolumeCallback)) {
            *object = static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK;
        }
        *object = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override { PostMessageW(window_, StateChanged, 0, 0); return S_OK; }
};

class AudioDeviceSink final : public IMMNotificationClient {
    std::atomic<ULONG> references_{1};
    HWND window_;
public:
    explicit AudioDeviceSink(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** object) override {
        if (!object) return E_POINTER;
        if (id==IID_IUnknown || id==__uuidof(IMMNotificationClient)) { *object=static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        *object=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR) override {
        if (flow==eRender && role==eConsole) PostMessageW(window_,AudioDeviceChanged,0,0); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override { return S_OK; }
};

struct SystemState {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioEndpointVolume> audio;
    VolumeSink* sink = nullptr;
    AudioDeviceSink* deviceSink = nullptr;
    float volume = 0;
    bool muted = false;
    std::wstring wifi = L"Not connected", bluetooth = L"Not available";
    std::wstring audioName=L"No audio device";
    DWORD memoryLoad = 0;
    double availableGiB = 0, workingMiB = 0, privateMiB = 0, processCpu = 0;
    double totalCpu=0,diskUsedGiB=0,diskTotalGiB=0,diskPercent=0,receiveRate=0,sendRate=0;
    ULONGLONG systemIdle=0,systemKernel=0,systemUser=0,receivedBytes=0,sentBytes=0,networkTime=0;
    ULONGLONG previousCpu = 0, previousTime = 0;

    void Initialize(HWND window) {
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))) {
            deviceSink=new AudioDeviceSink(window); enumerator->RegisterEndpointNotificationCallback(deviceSink);
            BindAudio(window);
        }
        RefreshVolume(); RefreshConnections(); Sample();
    }
    void BindAudio(HWND window) {
        if (audio && sink) audio->UnregisterControlChangeNotify(sink);
        if (sink) { sink->Release(); sink=nullptr; }
        audio.Reset();volume=0;muted=false;audioName=L"No audio device";
        ComPtr<IMMDevice> device;
        if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &audio))) {
            sink = new VolumeSink(window);
            audio->RegisterControlChangeNotify(sink);
            ComPtr<IPropertyStore> properties;if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT name{};if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR&&name.pwszVal)audioName=name.pwszVal;PropVariantClear(&name);}
        }
        RefreshVolume();
    }
    ~SystemState() {
        if (enumerator && deviceSink) enumerator->UnregisterEndpointNotificationCallback(deviceSink);
        if (deviceSink) deviceSink->Release();
        if (audio && sink) audio->UnregisterControlChangeNotify(sink);
        if (sink) sink->Release();
    }
    void RefreshVolume() {
        if (audio) { BOOL value = FALSE; audio->GetMasterVolumeLevelScalar(&volume); audio->GetMute(&value); muted = value != FALSE; }
    }
    void SetVolume(float value) { if (audio) { audio->SetMasterVolumeLevelScalar(std::clamp(value, 0.0f, 1.0f), nullptr); RefreshVolume(); } }
    void ToggleMute() { if (audio) { audio->SetMute(!muted, nullptr); RefreshVolume(); } }
    void RefreshConnections() {
        wifi = L"Not connected";
        HANDLE handle = nullptr; DWORD negotiated = 0;
        if (WlanOpenHandle(2, nullptr, &negotiated, &handle) == ERROR_SUCCESS) {
            PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
            if (WlanEnumInterfaces(handle, nullptr, &interfaces) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
                    auto& item = interfaces->InterfaceInfo[i];
                    if (item.isState != wlan_interface_state_connected) continue;
                    DWORD size = 0; WLAN_OPCODE_VALUE_TYPE type{}; PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
                    if (WlanQueryInterface(handle, &item.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size,
                        reinterpret_cast<void**>(&connection), &type) == ERROR_SUCCESS) {
                        auto& ssid = connection->wlanAssociationAttributes.dot11Ssid;
                        wifi = Wide(reinterpret_cast<const char*>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
                        if (wifi.empty()) wifi = L"Connected";
                        WlanFreeMemory(connection); break;
                    }
                    wifi = L"Connected";
                }
                WlanFreeMemory(interfaces);
            }
            WlanCloseHandle(handle, nullptr);
        }
        BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)}; HANDLE radio = nullptr;
        HBLUETOOTH_RADIO_FIND found = BluetoothFindFirstRadio(&params, &radio);
        bluetooth = found ? L"Radio available" : L"Not available";
        if (found) BluetoothFindRadioClose(found);
        if (radio) CloseHandle(radio);
    }
    void Sample() {
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory)) { memoryLoad = memory.dwMemoryLoad; availableGiB = memory.ullAvailPhys / 1073741824.0; }
        PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            workingMiB = counters.WorkingSetSize / 1048576.0; privateMiB = counters.PrivateUsage / 1048576.0;
        }
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            ULONGLONG cpu = (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                            (static_cast<ULONGLONG>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
            ULONGLONG now = GetTickCount64();
            SYSTEM_INFO info{}; GetSystemInfo(&info);
            if (previousTime && now > previousTime) processCpu = 100.0 * static_cast<double>(cpu - previousCpu) /
                (10000.0 * static_cast<double>(now - previousTime) * info.dwNumberOfProcessors);
            previousCpu = cpu; previousTime = now;
        }
        auto value=[](FILETIME time){return(static_cast<ULONGLONG>(time.dwHighDateTime)<<32)|time.dwLowDateTime;};
        FILETIME idle{},totalKernel{},totalUser{};
        if(GetSystemTimes(&idle,&totalKernel,&totalUser)){
            ULONGLONG k=value(totalKernel),u=value(totalUser),i=value(idle),total=(k-systemKernel)+(u-systemUser);
            if(systemKernel&&total)totalCpu=std::clamp(100.0*(1.0-static_cast<double>(i-systemIdle)/total),0.0,100.0);
            systemIdle=i;systemKernel=k;systemUser=u;
        }
        wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);std::wstring root=std::wstring(windows).substr(0,3);
        ULARGE_INTEGER freeBytes{},totalBytes{},totalFree{};
        if(GetDiskFreeSpaceExW(root.c_str(),&freeBytes,&totalBytes,&totalFree)&&totalBytes.QuadPart){diskTotalGiB=totalBytes.QuadPart/1073741824.0;diskUsedGiB=(totalBytes.QuadPart-totalFree.QuadPart)/1073741824.0;diskPercent=100*diskUsedGiB/diskTotalGiB;}
        PMIB_IF_TABLE2 interfaces=nullptr;
        if(GetIfTable2(&interfaces)==NO_ERROR){ULONGLONG received=0,sent=0;
            for(ULONG i=0;i<interfaces->NumEntries;++i){const auto& row=interfaces->Table[i];if(row.OperStatus==IfOperStatusUp&&row.InterfaceAndOperStatusFlags.HardwareInterface){received+=row.InOctets;sent+=row.OutOctets;}}
            ULONGLONG now=GetTickCount64();if(networkTime&&now>networkTime&&received>=receivedBytes&&sent>=sentBytes){receiveRate=(received-receivedBytes)*1000.0/(now-networkTime);sendRate=(sent-sentBytes)*1000.0/(now-networkTime);}
            receivedBytes=received;sentBytes=sent;networkTime=now;FreeMibTable(interfaces);
        }
    }
};

#include "DesktopCatalog.h"
#include "WallpaperReveal.h"
#include "HardwareStats.h"
#include "Connectivity.h"

enum class Tab { Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power };
enum class Flyout { None, Wifi, Bluetooth };
enum Action { Expand = 1, Wifi = 20, Bluetooth, Audio, Night, Dark, Volume, Mute, Previous, Play, Next,
    AddTask = 60, ClipboardRefresh, ClipboardClear, WallpaperSettings, PowerSettings, Lock, Quit, Minimize,
    AppsPrevious, AppsNext, WallpaperFolder, WallpaperPrevious, WallpaperNext, WallpaperApply, WallpaperCollection,
    SleepNow = 80, RestartNow, ShutdownNow,
    FlyoutBack=600,FlyoutRefresh,FlyoutToggle,FlyoutSettings,FlyoutConnect,FlyoutDisconnect,FlyoutPair,FlyoutRemove,FlyoutPrevious,FlyoutNext,
    FocusStart=700,FocusReset,FocusShort,FocusMedium,FocusLong,FocusOne,FocusTen,FocusTwenty };
struct Hit { int id; D2D1_RECT_F rect; std::wstring name; bool enabled = true; };
struct Task { std::wstring title; bool done = false; };
struct ModalGuard{bool& flag;explicit ModalGuard(bool& value):flag(value){flag=true;}~ModalGuard(){flag=false;}};

class Island {
public:
    HWND window = nullptr;
    float dpi = 96, width = theme::DockWidth, height = theme::DockHeight;
    bool expanded = false, transitioning = false, tracking = false, draggingVolume = false;
    Tab tab = Tab::Controls;
    Flyout flyout=Flyout::None;
    dock::Spring dockReveal{0},dockWidth{theme::DockWidth},dockHeight{theme::DockHeight},paneBlend{1};
    dock::Spring wallpaperFocus{0};
    dock::Proximity proximity;
    std::map<int,dock::Spring> hoverMotion;
    steady_clock::time_point lastMotion=steady_clock::now(),lastProximity=steady_clock::now();
    float envelopeHeight=theme::EnvelopeHeight;
    ComPtr<ID2D1Bitmap> previousPane;
    float previousPaneWidth=0,previousPaneHeight=0;
    bool capturingPane=false,modalOpen=false,keyboardOpen=false,launchPreview=false;
    std::unique_ptr<Connectivity> connectivity;
    ConnectionState connections;
    int selectedWifi=-1,selectedBluetooth=-1;
    size_t connectionPage=0;
    HWND wifiPassword=nullptr;
    int focusSeconds=300,focusDuration=300;
    bool focusRunning=false;
    bool statsTicking=false,mediaTicking=false,focusTicking=false,connectionsTicking=false;
    ULONGLONG focusDeadline=0;
    int hovered = -1, focused = -1;
    std::vector<Hit> hits;
    std::vector<Task> tasks;
    TaskStore taskStore;
    std::unique_ptr<DesktopCatalog> catalog;
    bool appsRequested = false, wallpapersRequested = false;
    bool customWallpaperFolder = false;
    std::vector<AppEntry> apps;
    std::vector<WallpaperEntry> wallpapers;
    std::vector<ComPtr<ID2D1Bitmap>> thumbnails;
    std::vector<size_t> filteredApps;
    std::wstring appQuery;
    size_t appPage = 0, selectedWallpaper = 0;
    std::wstring clipboard = L"Open this tab to read the current clipboard.", note;
    std::unique_ptr<MediaService> media;
    SystemState system;
    MediaState currentMedia;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1HwndRenderTarget> hostTarget;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::array<ComPtr<IDWriteTextFormat>, 8> formats;
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
    desktop::Shell shell;
    bool taskbarHotkey=false;
    HWND contextWindow=nullptr;
    bool browserContext=false;
    bool wallpaperApplying=false,miniMode=false;
    WallpaperReveal wallpaperReveal;
    std::unique_ptr<HardwareStats> hardware;
    HardwareSnapshot hardwareState;
    AppPreferences appPreferences;
    std::wstring AppKey(const AppEntry& app)const{return app.parsingName.empty()?app.name:app.parsingName;}
    bool Favorite(const AppEntry& app)const{return std::find(appPreferences.favorites.begin(),appPreferences.favorites.end(),AppKey(app))!=appPreferences.favorites.end();}
    int AppGroup(const AppEntry& app)const{return Favorite(app)?0:std::find(appPreferences.recent.begin(),appPreferences.recent.end(),AppKey(app))!=appPreferences.recent.end()?1:2;}
    void SampleStats(){system.Sample();if(!renderMode){if(!hardware)hardware=std::make_unique<HardwareStats>(window);hardware->Request();}}
    void SetAccent(unsigned rgb){theme::Gold=rgb;theme::GoldHover=dock::Mix(rgb,0xFFFFFF,0.15f);theme::GoldInk=0x18181A;theme::AccentSoft=dock::Mix(theme::Surface,rgb,0.17f);
        auto hsl=palette::ToHsl(rgb);hsl.h=std::fmod(hsl.h+0.55f,1.0f);theme::Secondary=palette::FromHsl(hsl);hsl.h=std::fmod(hsl.h+0.63f,1.0f);theme::Tertiary=palette::FromHsl(hsl);
        InvalidateRect(window,nullptr,FALSE);
    }
    void UpdateContext(){
        if(renderMode||expanded)return;
        HWND foreground=GetForegroundWindow();DWORD processId=0;GetWindowThreadProcessId(foreground,&processId);
        if(!foreground||processId==GetCurrentProcessId()||GetWindow(foreground,GW_OWNER)==window||foreground==contextWindow||desktop::IsTaskbar(foreground))return;
        contextWindow=foreground;browserContext=false;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
        if(process){wchar_t path[32768]{};DWORD length=32768;if(QueryFullProcessImageNameW(process,0,path,&length))browserContext=dock::BrowserExecutable(std::wstring_view(path,length));CloseHandle(process);}
        wchar_t name[64]{};GetClassNameW(foreground,name,64);if(!wcscmp(name,L"MozillaWindowClass"))browserContext=true;
    }

    ~Island() { if (editFont) DeleteObject(editFont); if (editBackground) DeleteObject(editBackground); }
    void Initialize() {
        dpi = static_cast<float>(GetDpiForWindow(window));
        BOOL animation = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0); reducedMotion = !animation;
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())));
        const float sizes[]{11,12,13,15,20,40,18,13};
        for (size_t i = 0; i < formats.size(); ++i) {
            Check(textFactory->CreateTextFormat(i == 6 ? L"Segoe Fluent Icons" : L"Segoe UI", nullptr,
                i==3||i==5||i==7 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, sizes[i], L"en-us", &formats[i]));
            formats[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis; textFactory->CreateEllipsisTrimmingSign(formats[i].Get(), &ellipsis);
            formats[i]->SetTrimming(&trim, ellipsis.Get());
        }
        int corner = 1; DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        system.Initialize(window);
        if (!renderMode) {
            catalog=std::make_unique<DesktopCatalog>(window);
            appPreferences.Load();
            for (const auto& task : taskStore.Load()) tasks.push_back({task.first,task.second});
            toggleHotkey = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE) != FALSE;
            quitHotkey = RegisterHotKey(window, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != FALSE;
            taskbarHotkey=RegisterHotKey(window,4,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,'T')!=FALSE;
        }
        Place();
        if(!renderMode)SetCoalescableTimer(window,ProximityTimer,100,nullptr,50);
    }
    void ReleaseGraphics() { previousPane.Reset();thumbnails.clear(); brush.Reset(); target.Reset(); hostTarget.Reset(); }
    float LayoutWidth()const {
        if(miniMode)return 344;
        if(flyout!=Flyout::None)return 460;
        switch(tab){case Tab::Today:case Tab::Stats:case Tab::Clipboard:return 800;case Tab::Wallpaper:return 840;case Tab::Apps:return 680;default:return 600;}
    }
    float LayoutHeight()const {
        if(miniMode)return 136;
        if(flyout!=Flyout::None)return std::min(520.0f,envelopeHeight-PanelY()-12);
        float desired=tab==Tab::Power?156.0f:tab==Tab::Today?410.0f:tab==Tab::Stats?330.0f:tab==Tab::Clipboard?430.0f:tab==Tab::Apps?580.0f:tab==Tab::Wallpaper?300.0f:368.0f;
        return std::min(desired,envelopeHeight-PanelY()-12);
    }
    float ContentWidth() const {return LayoutWidth();}
    float PanelX()const{return renderMode?0:(theme::EnvelopeWidth-LayoutWidth())/2;}
    float HandleY()const{return 0;}
    float PanelY()const{return renderMode?0:dock::PanelTop(browserContext);}
    std::array<POINT,35> NotchPixels(float scale)const{
        auto curve=dock::Notch(renderMode?0:(theme::EnvelopeWidth-theme::DockWidth)/2,HandleY(),theme::DockWidth,theme::DockHeight);
        std::array<POINT,35> points{};for(size_t i=0;i<points.size();++i)points[i]={static_cast<LONG>(std::lround(curve[i].x*scale)),static_cast<LONG>(std::lround(curve[i].y*scale))};return points;
    }
    void PaintNotch(HDC dc){
        auto points=NotchPixels(dpi/96);HBRUSH fill=CreateSolidBrush(RGB(13,13,15));auto oldBrush=SelectObject(dc,fill);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        Polygon(dc,points.data(),static_cast<int>(points.size()));SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(fill);
    }
    void DrawNotch(){
        auto points=dock::Notch(renderMode?0:(theme::EnvelopeWidth-theme::DockWidth)/2,HandleY(),theme::DockWidth,theme::DockHeight);
        ComPtr<ID2D1PathGeometry> geometry;Check(factory->CreatePathGeometry(&geometry));ComPtr<ID2D1GeometrySink> sink;Check(geometry->Open(&sink));
        sink->BeginFigure({points[0].x,points[0].y},D2D1_FIGURE_BEGIN_FILLED);for(size_t i=1;i<points.size();++i)sink->AddLine({points[i].x,points[i].y});sink->EndFigure(D2D1_FIGURE_END_CLOSED);Check(sink->Close());
        brush->SetColor(Color(theme::Surface));target->FillGeometry(geometry.Get(),brush.Get());
    }
    std::array<POINT,69> AttachedPixels(float scale)const{auto outline=dock::Attached((theme::EnvelopeWidth-width)/2,0,width,height);std::array<POINT,69> points{};
        for(size_t i=0;i<points.size();++i)points[i]={static_cast<LONG>(std::lround(outline[i].x*scale)),static_cast<LONG>(std::lround(outline[i].y*scale))};return points;}
    void IslandSurface(){
        if(browserContext){Fill({(theme::EnvelopeWidth-width)/2,PanelY(),(theme::EnvelopeWidth+width)/2,PanelY()+height},theme::Surface,20);return;}
        auto outline=dock::Attached((theme::EnvelopeWidth-width)/2,0,width,height);ComPtr<ID2D1PathGeometry> geometry;Check(factory->CreatePathGeometry(&geometry));ComPtr<ID2D1GeometrySink> sink;Check(geometry->Open(&sink));sink->BeginFigure({outline[0].x,outline[0].y},D2D1_FIGURE_BEGIN_FILLED);
        for(size_t i=1;i<outline.size();++i)sink->AddLine({outline[i].x,outline[i].y});sink->EndFigure(D2D1_FIGURE_END_CLOSED);Check(sink->Close());
        D2D1_GRADIENT_STOP stops[]{{0,Color(0x101010)},{1,Color(0x020202)}};ComPtr<ID2D1GradientStopCollection> colors;ComPtr<ID2D1LinearGradientBrush> gradient;
        Check(target->CreateGradientStopCollection(stops,2,&colors));Check(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{0,height}),colors.Get(),&gradient));target->FillGeometry(geometry.Get(),gradient.Get());
    }
    void Graphics() {
        if (target) return;
        RECT client{}; GetClientRect(window, &client);
        Check(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(), dpi, dpi), D2D1::HwndRenderTargetProperties(window,
                D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom))), &hostTarget));
        Check(hostTarget.As(&target));
        Check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1), &brush));
    }
    void Place() {
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        const float scale = dpi / 96.0f;
        envelopeHeight=std::min(theme::EnvelopeHeight,(info.rcMonitor.bottom-info.rcMonitor.top)/scale);
        const int w=static_cast<int>(std::lround((renderMode?width:theme::EnvelopeWidth)*scale));
        const int h=static_cast<int>(std::lround((renderMode?height:envelopeHeight)*scale));
        RECT bounds{};GetWindowRect(window,&bounds);
        int left=info.rcMonitor.left+(info.rcMonitor.right-info.rcMonitor.left-w)/2;
        int top=info.rcMonitor.top;
        if(bounds.left!=left||bounds.top!=top||bounds.right-bounds.left!=w||bounds.bottom-bounds.top!=h)
            SetWindowPos(window,HWND_TOPMOST,left,top,w,h,SWP_NOACTIVATE);
        const int radius=static_cast<int>(theme::Radius*scale);
        auto notch=NotchPixels(scale);
        HRGN shape=renderMode?CreateRoundRectRgn(0,0,w+1,h+1,radius*2,radius*2):
            CreatePolygonRgn(notch.data(),static_cast<int>(notch.size()),WINDING);
        if(!renderMode&&dockReveal.value>0.003f){
            int x=static_cast<int>((theme::EnvelopeWidth-width)*0.5f*scale),y=static_cast<int>(PanelY()*scale);
            auto attached=AttachedPixels(scale);
            HRGN body=browserContext?CreateRoundRectRgn(x,y,x+static_cast<int>(width*scale),y+static_cast<int>(height*scale),radius*2,radius*2):CreatePolygonRgn(attached.data(),static_cast<int>(attached.size()),WINDING);
            CombineRgn(shape,shape,body,RGN_OR);DeleteObject(body);
        }
        if (!SetWindowRgn(window, shape, TRUE)) DeleteObject(shape);
        PositionInputs();
    }
    void PositionInputs(){
        bool visible=expanded&&dockReveal.value>0.99f&&std::abs(width-dockWidth.goal)<1&&std::abs(height-dockHeight.goal)<1&&paneBlend.value>0.99f&&!capturingPane;
        float s=dpi/96.0f;
        auto position=[&](HWND input,float x,float y,float w,float h,bool show){if(!input)return;
            SetWindowPos(input,nullptr,static_cast<int>((PanelX()+x)*s),static_cast<int>((PanelY()+y)*s),static_cast<int>(w*s),static_cast<int>(h*s),SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(input,visible&&show?SW_SHOWNOACTIVATE:SW_HIDE);};
        position(taskEdit,554,LayoutHeight()-49,168,24,flyout==Flyout::None&&tab==Tab::Today);
        position(appSearch,54,67,LayoutWidth()-90,24,flyout==Flyout::None&&tab==Tab::Apps);
        bool needsPassword=flyout==Flyout::Wifi&&selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())&&connections.networks[selectedWifi].secure&&connections.networks[selectedWifi].profile.empty()&&!connections.networks[selectedWifi].connected;
        position(wifiPassword,32,LayoutHeight()-127,LayoutWidth()-64,26,needsPassword);
    }
    void ArmMotion(){if(renderMode)return;lastMotion=steady_clock::now();SetTimer(window,TransitionTimer,16,nullptr);}
    void SetExpanded(bool value,bool deliberate=true) {
        if(value&&!expanded)UpdateContext();
        expanded = value; focused = hovered = -1; note.clear();
        if(value){system.RefreshVolume();if(!renderMode&&!media&&tab==Tab::Controls&&flyout==Flyout::None)media=std::make_unique<MediaService>(window);}
        if(media)media->SetVisible(value&&tab==Tab::Controls&&flyout==Flyout::None);
        if(!value){keyboardOpen=false;launchPreview=false;if(deliberate)proximity.Dismiss();else{proximity.outside=0;proximity.dwell=0;}if(wifiPassword)SetWindowTextW(wifiPassword,L"");if(connectivity)connectivity->CancelDiscovery();}
        dockReveal.goal=value?1.0f:0.0f;dockWidth.goal=value?LayoutWidth():theme::DockWidth;dockHeight.goal=value?LayoutHeight():theme::DockHeight;
        transitioning=!reducedMotion&&!renderMode;
        if(transitioning)ArmMotion();
        else{dockReveal.Snap(dockReveal.goal);dockWidth.Snap(dockWidth.goal);dockHeight.Snap(dockHeight.goal);width=dockWidth.value;height=dockHeight.value;Place();}
        UpdateTimers(); InvalidateRect(window, nullptr, FALSE);
    }
    void Transition() {
        auto now=steady_clock::now();float dt=duration<float>(now-lastMotion).count();lastMotion=now;
        dockReveal.Step(dt);dockWidth.Step(dt);dockHeight.Step(dt);paneBlend.Step(dt,26);wallpaperFocus.Step(dt,20);
        width=dockWidth.value;height=dockHeight.value;
        bool moving=dockReveal.Moving()||dockWidth.Moving()||dockHeight.Moving()||paneBlend.Moving()||wallpaperFocus.Moving();
        for(auto& [id,motion]:hoverMotion){(void)id;motion.Step(dt,30);moving|=motion.Moving();}
        transitioning=dockReveal.Moving()||dockWidth.Moving()||dockHeight.Moving();
        if(paneBlend.value==1)previousPane.Reset();
        if(!moving)KillTimer(window,TransitionTimer);
        if(dockReveal.value==0)ReleaseGraphics();
        Place(); InvalidateRect(window, nullptr, FALSE);
    }
    void ProximityTick(){
        UpdateContext();
        shell.UpdateBrowserCutout(browserContext?contextWindow:nullptr);
        auto now=steady_clock::now();float dt=duration<float>(now-lastProximity).count();lastProximity=now;
        POINT cursor{};GetCursorPos(&cursor);ScreenToClient(window,&cursor);
        float x=cursor.x*96.0f/dpi,y=cursor.y*96.0f/dpi,center=theme::EnvelopeWidth/2;
        bool buttonDown=(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000);
        bool approaching=std::abs(x-center)<theme::DockWidth/2+18&&y>=0&&y<theme::DockHeight+6&&!buttonDown;
        bool withinBody=std::abs(x-center)<width/2&&y>=PanelY()&&y<PanelY()+height;
        bool inside=std::abs(x-center)<std::max(width,theme::DockWidth)/2+18&&y>=0&&y<PanelY()+height+18;
        HWND foreground=GetForegroundWindow();bool ownedModal=foreground&&GetWindow(foreground,GW_OWNER)==window;
        bool editing=foreground==window&&(GetFocus()==taskEdit||GetFocus()==appSearch||GetFocus()==wifiPassword);
        bool protectedInput=modalOpen||ownedModal||editing||draggingVolume||launchPreview||(keyboardOpen&&foreground==window);
        if(expanded&&buttonDown&&!withinBody&&!protectedInput){SetExpanded(false);return;}
        if(!expanded&&dockReveal.value>0.05f&&proximity.cooldown==0&&withinBody&&!buttonDown){SetExpanded(true);return;}
        int action=proximity.Step(dt,expanded,approaching,inside,protectedInput);
        if(action>0)SetExpanded(true);else if(action<0)SetExpanded(false,false);
    }
    void UpdateTimers() {
        if(renderMode)return;
        auto timer=[&](UINT_PTR id,bool enabled,UINT period,bool& active){if(enabled==active)return;active=enabled;if(enabled)SetTimer(window,id,period,nullptr);else KillTimer(window,id);};
        bool stats=expanded&&tab==Tab::Stats&&flyout==Flyout::None;
        if(stats&&!statsTicking)SampleStats();timer(StatsTimer,stats,2000,statsTicking);
        timer(MediaTimer,currentMedia.playing&&expanded&&tab==Tab::Controls&&flyout==Flyout::None,1000,mediaTicking);
        timer(FocusTimer,focusRunning||(expanded&&tab==Tab::Today&&flyout==Flyout::None),1000,focusTicking);
        timer(ConnectivityTimer,expanded&&flyout!=Flyout::None,2500,connectionsTicking);
    }
    void SetTab(Tab value) {
        miniMode=false;
        CapturePane();paneBlend.Snap(reducedMotion||renderMode?1.0f:0.0f);paneBlend.goal=1;
        if(connectivity)connectivity->CancelDiscovery();flyout=Flyout::None;
        tab = value; focused = hovered = -1;
        if (tab == Tab::Clipboard) ReadClipboard();
        if (!renderMode && (tab==Tab::Apps || tab==Tab::Wallpaper)) {
            if (!catalog) catalog=std::make_unique<DesktopCatalog>(window);
            if (tab==Tab::Apps && !appsRequested) { appsRequested=true; catalog->DiscoverApps(); }
            if (tab==Tab::Wallpaper && !wallpapersRequested) { wallpapersRequested=true;selectedWallpaper=2;wallpaperFocus.Snap(2);catalog->DiscoverWallpapers(); }
        }
        SetExpanded(true);
        PositionInputs();
    }
    D2D1_COLOR_F Color(unsigned rgb,float alpha=1){
        switch(rgb){case 0xC9AB61:case 0xD8BD78:case 0xD7BE80:case 0xDBCA9C:case 0xBAA66F:case 0xC6B88D:case 0xD8C28D:case 0xE0CD98:rgb=theme::Gold;break;
        case 0x2A261A:case 0x30291A:case 0x3B301A:case 0x353024:case 0x29262F:rgb=theme::AccentSoft;break;case 0xE5D8AF:rgb=dock::Mix(theme::Gold,0xFFFFFF,0.65f);break;case 0xBC93D8:rgb=theme::Secondary;break;case 0xA6CA7D:rgb=theme::Tertiary;break;default:break;}return D2D1::ColorF(rgb,alpha);
    }
    void Fill(D2D1_RECT_F rect, unsigned color, float radius = 12) {
        brush->SetColor(Color(color)); target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
        if(radius>=10&&rect.bottom-rect.top>=32&&rect.right-rect.left>=60){
            brush->SetColor(Color(0xFFFFFF,0.035f));D2D1_RECT_F edge{rect.left+0.5f,rect.top+0.5f,rect.right-0.5f,rect.bottom-0.5f};
            target->DrawRoundedRectangle(D2D1::RoundedRect(edge,radius,radius),brush.Get(),1);
        }
    }
    void Text(const std::wstring& text, D2D1_RECT_F rect, int format = 2, unsigned color = theme::Text,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        brush->SetColor(Color(color)); formats[format]->SetTextAlignment(align);
        target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), formats[format].Get(), rect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Line(float x1, float y1, float x2, float y2, unsigned color, float thickness = 1) {
        brush->SetColor(Color(color)); target->DrawLine({x1,y1},{x2,y2},brush.Get(),thickness);
    }
    void AddHit(int id, D2D1_RECT_F rect, std::wstring name, bool enabled = true) { hits.push_back({id,rect,std::move(name),enabled}); }
    void FocusRing(int id, D2D1_RECT_F rect, float radius = 12) {
        if (focused >= 0 && focused < static_cast<int>(hits.size()) && hits[focused].id == id) {
            brush->SetColor(Color(theme::Gold)); target->DrawRoundedRectangle(D2D1::RoundedRect(rect,radius,radius), brush.Get(),1.5f);
        }
    }
    bool Hover(int id) const { return hovered >= 0 && hovered < static_cast<int>(hits.size()) && hits[hovered].id == id; }
    float HoverAmount(int id)const{auto found=hoverMotion.find(id);return found==hoverMotion.end()?0:found->second.value;}
    void SetHover(int next){
        if(next==hovered)return;
        if(hovered>=0&&hovered<static_cast<int>(hits.size()))hoverMotion[hits[hovered].id].goal=0;
        hovered=next;
        if(next>=0&&next<static_cast<int>(hits.size()))hoverMotion[hits[next].id].goal=1;
        ArmMotion();InvalidateRect(window,nullptr,FALSE);
    }
    void Button(int id, D2D1_RECT_F rect, const std::wstring& title, const std::wstring& subtitle,
                const wchar_t* icon, bool active = false, bool enabled = true) {
        AddHit(id,rect,title,enabled);
        Fill(rect,dock::Mix(active?theme::Gold:theme::Card,active?theme::GoldHover:theme::CardHover,HoverAmount(id)),theme::SmallRadius);
        const unsigned foreground = active ? theme::GoldInk : enabled ? theme::Text : 0x808080;
        brush->SetColor(Color(active?theme::GoldInk:theme::Text,active?0.12f:0.07f));target->FillEllipse(D2D1::Ellipse({rect.left+27,(rect.top+rect.bottom)/2},16,16),brush.Get());
        Text(icon,{rect.left+13,rect.top+8,rect.left+41,rect.bottom-8},6,foreground,DWRITE_TEXT_ALIGNMENT_CENTER);
        bool disclosure=id==Wifi||id==Bluetooth||id==Audio||id==Night;
        float reserve=disclosure?32.0f:12.0f;
        Text(title,{rect.left+45,rect.top+5,rect.right-reserve,rect.top+28},7,foreground,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(subtitle,{rect.left+45,rect.top+27,rect.right-reserve,rect.bottom-4},0,active?dock::Mix(theme::GoldInk,theme::Gold,0.18f):theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        if(disclosure){Line(rect.right-32,rect.top+11,rect.right-32,rect.bottom-11,active?dock::Mix(theme::Gold,theme::GoldInk,0.17f):0x303033);Text(L"\xE70D",{rect.right-28,rect.top+12,rect.right-8,rect.bottom-10},6,foreground,DWRITE_TEXT_ALIGNMENT_CENTER);}
        FocusRing(id,rect,10);
    }
    std::wstring Clock(double seconds) {
        auto s = static_cast<int>(std::max(0.0,seconds)); wchar_t buffer[32]; swprintf_s(buffer,L"%d:%02d",s/60,s%60); return buffer;
    }
    void MediaCard(bool compact) {
        if(compact){
            AddHit(Expand,{14,12,330,70},L"Open full controls");
            Fill({18,22,60,64},dock::Mix(theme::Secondary,theme::Gold,0.22f),9);
            Text(currentMedia.title,{76,20,326,43},7,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(currentMedia.artist,{76,41,326,61},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            float progress=75;Line(55,progress,290,progress,0x292929,2);if(currentMedia.duration>0)Line(55,progress,55+235*static_cast<float>(std::clamp(currentMedia.position/currentMedia.duration,0.0,1.0)),progress,theme::Gold,2);
            Text(Clock(currentMedia.position),{10,65,51,84},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);Text(Clock(currentMedia.duration),{294,65,337,84},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            for(int i=0;i<3;++i){D2D1_RECT_F rect{110+i*48.0f,95,140+i*48.0f,126};AddHit(Previous+i,rect,i==0?L"Previous track":i==1?L"Play or pause":L"Next track",currentMedia.available);Text(i==0?L"\xE892":i==2?L"\xE893":currentMedia.playing?L"\xE769":L"\xE768",rect,6,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);}return;
        }
        const float right = compact ? 288.0f : 582.0f, top = compact ? 4.0f : 222.0f;
        if (!compact) { Fill({18,top,right,348},0x131313,12); Text(L"♫  "+system.audioName,{30,top+4,300,top+25},0,0x8B8B8B); }
        float y = top + (compact ? 10 : 34);
        Fill({compact ? 16.0f : 32.0f,y,compact ? 54.0f : 78.0f,y+(compact ? 38 : 46)},0x29262F,8);
        Text(L"\xE8D6",{compact ? 16.0f : 32.0f,y,compact ? 54.0f : 78.0f,y+38},6,0xC6B88D,DWRITE_TEXT_ALIGNMENT_CENTER);
        const float textLeft = compact ? 64.0f : 94.0f, textRight = compact ? 278.0f : 414.0f;
        Text(currentMedia.title,{textLeft,y,textRight,y+22},compact ? 1 : 3,0xF3F2ED,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(currentMedia.artist,{textLeft,y+22,textRight,y+39},0,0x959595,compact ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        float controlsY = compact ? 68.0f : y+4;
        float controlsX = compact ? 98.0f : 440.0f;
        for (int i=0;i<3;++i) {
            D2D1_RECT_F rect{controlsX+i*38,controlsY,controlsX+30+i*38,controlsY+30};
            int id = Previous+i;
            AddHit(id,rect,i==0?L"Previous track":i==1?L"Play or pause":L"Next track",currentMedia.available);
            if (i==1) Fill(rect,currentMedia.available ? 0xE5D8AF : 0x2A2A2A,15);
            else if (Hover(id)) Fill(rect,0x323232,15);
            Text(i==0 ? L"\xE892" : i==2 ? L"\xE893" : currentMedia.playing ? L"\xE769" : L"\xE768",rect,6,
                i==1 && currentMedia.available ? 0x171714 : currentMedia.available ? 0xE8E7E2 : 0x777777,DWRITE_TEXT_ALIGNMENT_CENTER);
            FocusRing(id,rect,15);
        }
        float progressY = compact ? 59.0f : top+103;
        float progressLeft = compact ? 46.0f : 68.0f, progressRight = compact ? 248.0f : 538.0f;
        Fill({progressLeft,progressY,progressRight,progressY+2},0x363636,1);
        if (currentMedia.duration > 0) Fill({progressLeft,progressY,progressLeft+(progressRight-progressLeft)*static_cast<float>(std::clamp(currentMedia.position/currentMedia.duration,0.0,1.0)),progressY+2},0xBAA66F,1);
        Text(Clock(currentMedia.position),{compact?8.0f:28.0f,progressY-7,progressLeft-3,progressY+9},0,0x868686);
        Text(Clock(currentMedia.duration),{progressRight+4,progressY-7,right-10,progressY+9},0,0x868686,DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    void Tabs() {
        constexpr const wchar_t* titles[]{L"Controls",L"Today",L"Stats",L"Clipboard",L"Apps",L"Wallpaper",L"Power"};
        constexpr float edges[]{22,106,174,236,332,395,498,578};
        for (int i=0;i<7;++i) {
            const float offset=(ContentWidth()-600)/2;
            D2D1_RECT_F rect{edges[i]+offset,12,edges[i+1]-5+offset,39};
            AddHit(100+i,rect,titles[i]);
            if (static_cast<int>(tab)==i) Fill(rect,0x2A261A,14);
            else if (Hover(100+i)) Fill(rect,0x222222,14);
            Text(titles[i],rect,7,static_cast<int>(tab)==i?0xD8BD78:0x999999,DWRITE_TEXT_ALIGNMENT_CENTER);
            FocusRing(100+i,rect,14);
        }
    }
    void Controls() {
        Button(Wifi,{18,54,199,111},L"Wi-Fi",system.wifi,L"\xE701",system.wifi != L"Not connected");
        Button(Bluetooth,{209,54,390,111},L"Bluetooth",system.bluetooth,L"\xE702",connections.bluetoothOn);
        Button(Audio,{400,54,582,111},L"Audio",system.audioName,L"\xE767",system.audio != nullptr);
        Button(Night,{18,121,294,178},L"Night light",L"Display settings",L"\xE708");
        Button(Dark,{304,121,582,178},L"Dark mode",L"Color settings",L"\xE790");
        D2D1_RECT_F slider{18,188,582,212};
        AddHit(Volume,slider,L"System volume",system.audio != nullptr);
        Fill(slider,0x202020,12);
        float end = 18+564*system.volume;
        if (end > 25) Fill({18,188,end,212},0xC9AB61,12);
        Text(system.muted?L"\xE74F":L"\xE767",{28,188,52,212},6,0x353024);
        Text(system.audio ? Number(system.volume*100)+L"%" : L"No audio device",{420,188,566,212},0,0xEEECE7,DWRITE_TEXT_ALIGNMENT_TRAILING);
        FocusRing(Volume,slider);
        MediaCard(false);
    }
    void Today() {
        SYSTEMTIME now{};GetLocalTime(&now);wchar_t hour[8]{},minute[8]{},date[48]{};
        swprintf_s(hour,L"%02u",now.wHour);swprintf_s(minute,L"%02u",now.wMinute);
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&now,L"ddd, MMM d",date,48,nullptr);
        Fill({18,54,178,246},theme::Card,theme::Radius);
        Text(hour,{18,74,178,136},5,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(L"• • •",{18,134,178,154},2,theme::Gold,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(minute,{18,152,178,216},5,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(date,{24,216,172,239},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        Fill({18,256,178,392},theme::Inset,theme::Radius);
        SYSTEM_POWER_STATUS power{};GetSystemPowerStatus(&power);
        Text(power.ACLineStatus==1?L"\xE945":L"\xE83F",{18,265,178,301},6,theme::Gold,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(power.BatteryLifePercent<=100?Number(power.BatteryLifePercent)+L"%":L"AC",{18,302,178,350},4,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(power.ACLineStatus==1?L"Plugged in":L"On battery",{18,352,178,380},1,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        Fill({188,54,512,216},theme::Card,theme::Radius);
        Text(L"This week",{202,65,402,91},3);
        constexpr const wchar_t* days[]{L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat",L"Sun"};
        FILETIME file{};SystemTimeToFileTime(&now,&file);ULARGE_INTEGER ticks{};ticks.LowPart=file.dwLowDateTime;ticks.HighPart=file.dwHighDateTime;
        int today=(now.wDayOfWeek+6)%7;
        for(int i=0;i<7;++i){
            float x=201+i*43.0f;ULARGE_INTEGER day{};day.QuadPart=ticks.QuadPart+static_cast<LONGLONG>(i-today)*864000000000LL;
            FILETIME when{day.LowPart,day.HighPart};SYSTEMTIME shown{};FileTimeToSystemTime(&when,&shown);
            Text(days[i],{x,95,x+38,117},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            if(i==today)Fill({x+1,120,x+37,155},theme::Gold,10);
            Text(Number(shown.wDay),{x,120,x+38,155},2,i==today?theme::GoldInk:theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        Fill({200,169,500,205},0x242428,10);
        Text(L"A little room to focus.",{213,169,487,205},1,theme::Muted);
        Fill({188,226,512,392},theme::Inset,theme::Radius);
        DrawRing(250,307,42,focusDuration?static_cast<float>(focusSeconds)/focusDuration:0,theme::Gold,5);
        Text(Clock(focusSeconds),{204,285,296,329},3,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        Fill({308,239,398,269},theme::CardHover,9);Fill({406,239,498,269},theme::CardHover,9);Text(Number(focusSeconds/3600)+L" h",{308,239,398,269},7,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(Number((focusSeconds/60)%60)+L" min",{406,239,498,269},7,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        const int actions[]{FocusOne,FocusShort,FocusTen,FocusMedium,FocusTwenty};const int presets[]{1,5,10,15,20};
        for(int i=0;i<5;++i){D2D1_RECT_F rect{308+i*38.0f,276,342+i*38.0f,303};AddHit(actions[i],rect,Number(presets[i])+L" minute focus session");
            bool active=focusDuration==presets[i]*60;Fill(rect,active?0x3B301A:theme::CardHover,13);Text(Number(presets[i])+L"m",rect,1,active?theme::Gold:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(actions[i],rect);}
        TextButton(FocusStart,{308,322,398,357},focusRunning?L"Pause":L"Start",true);
        TextButton(FocusReset,{406,322,498,357},L"Reset");
        Fill({522,54,782,392},theme::Card,theme::Radius);
        Text(L"Today",{536,65,724,93},3);
        size_t done=static_cast<size_t>(std::count_if(tasks.begin(),tasks.end(),[](const auto& task){return task.done;}));
        Text(Number(static_cast<double>(done))+L"/"+Number(static_cast<double>(tasks.size())),{724,65,768,93},0,theme::Gold,DWRITE_TEXT_ALIGNMENT_TRAILING);
        for(size_t i=0;i<tasks.size()&&i<5;++i){float y=104+static_cast<float>(i)*44;int id=200+static_cast<int>(i);D2D1_RECT_F row{534,y,770,y+37};AddHit(id,row,tasks[i].title);
            Fill(row,dock::Mix(theme::Card,theme::CardHover,HoverAmount(id)),10);
            brush->SetColor(Color(tasks[i].done?theme::Gold:theme::Muted));auto circle=D2D1::Ellipse({554,y+18.5f},7.5f,7.5f);if(tasks[i].done){target->FillEllipse(circle,brush.Get());Line(550,y+18,553,y+21,theme::GoldInk,1.5f);Line(553,y+21,558,y+15,theme::GoldInk,1.5f);}else target->DrawEllipse(circle,brush.Get(),1.5f);
            Text(tasks[i].title,{576,y,756,y+37},7,tasks[i].done?theme::Muted:theme::Text);FocusRing(id,row);}
        if(tasks.empty())Text(L"A clear day.",{538,155,766,234},2,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        Fill({536,351,770,386},theme::CardHover,10);
        if(renderMode||!taskEdit||GetWindowTextLengthW(taskEdit)==0)Text(L"Add a to-do…",{554,355,720,385},1,theme::Muted);
        SmallButton(AddTask,{738,355,766,383},L"\xE72A",L"Add task");
        EnsureTaskEdit();
    }
    void DrawRing(float x,float y,float radius,float value,unsigned color,float thickness){
        brush->SetColor(Color(0x303034));target->DrawEllipse(D2D1::Ellipse({x,y},radius,radius),brush.Get(),thickness);
        value=std::clamp(value,0.0f,1.0f);if(value<=0)return;brush->SetColor(Color(color));
        if(value>=0.999f){target->DrawEllipse(D2D1::Ellipse({x,y},radius,radius),brush.Get(),thickness);return;}
        ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory->CreatePathGeometry(&geometry))||FAILED(geometry->Open(&sink)))return;
        float angle=value*6.28318530718f-1.57079632679f;
        sink->BeginFigure({x,y-radius},D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment({x+radius*std::cos(angle),y+radius*std::sin(angle)},{radius,radius},0,D2D1_SWEEP_DIRECTION_CLOCKWISE,value>0.5f?D2D1_ARC_SIZE_LARGE:D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);if(SUCCEEDED(sink->Close()))target->DrawGeometry(geometry.Get(),brush.Get(),thickness);
    }
    void TextButton(int id,D2D1_RECT_F rect,const std::wstring& text,bool primary=false,bool enabled=true){
        AddHit(id,rect,text,enabled);Fill(rect,dock::Mix(primary?theme::Gold:theme::CardHover,primary?theme::GoldHover:0x36363A,HoverAmount(id)),10);
        Text(text,rect,1,!enabled?0x727178:primary?theme::GoldInk:theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(id,rect,10);
    }
    void Stats() {
        const wchar_t* labels[]{L"CPU",L"GPU",L"RAM"};double percentages[]{system.totalCpu,hardwareState.gpuPercent,static_cast<double>(system.memoryLoad)};
        unsigned colors[]{theme::Gold,0xBC93D8,0xA6CA7D};
        for(int i=0;i<3;++i){float x=18+i*172.0f;Fill({x,54,x+162,236},theme::Card,theme::Radius);
            DrawRing(x+81,113,39,static_cast<float>(percentages[i]/100),colors[i],7);
            Text(i==1&&!hardwareState.gpuValid?L"—":Number(percentages[i])+L"%",{x+35,94,x+127,132},3,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(labels[i],{x,163,x+162,189},3,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
            std::wstring detail=i==0?hardwareState.cpu:i==1?hardwareState.gpu:Number(hardwareState.totalGiB-system.availableGiB,1)+L" / "+Number(hardwareState.totalGiB,1)+L" GB";
            Text(detail,{x+8,189,x+154,211},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            std::wstring chip=i==0?Number(hardwareState.processes)+L" processes":i==1?Number(hardwareState.vramGiB,1)+L" GB VRAM":Number(system.availableGiB,1)+L" GB free";
            Fill({x+23,213,x+139,231},theme::CardHover,7);Text(chip,{x+23,213,x+139,231},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        Fill({534,54,782,181},theme::Card,theme::Radius);Text(L"Top apps · RAM",{547,60,768,83},0,theme::Muted);
        for(size_t i=0;i<hardwareState.top.size();++i){float y=85+static_cast<float>(i)*22;const auto& app=hardwareState.top[i];Fill({546,y+1,563,y+18},theme::CardHover,4);Text(app.first.substr(0,1),{546,y+1,563,y+18},0,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(app.first,{571,y,699,y+19},0);Text(Number(app.second)+L" MB",{699,y,768,y+19},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);double maximum=hardwareState.top[0].second;Fill({571,y+19,571+197*static_cast<float>(app.second/std::max(1.0,maximum)),y+21},theme::Tertiary,1);}
        if(hardwareState.top.empty())Text(L"Reading process memory…",{547,89,768,148},0,theme::Muted);
        Fill({534,191,782,312},theme::Inset,14);Text(L"NETWORK · LIVE TRANSFER",{548,197,768,221},0,theme::Muted);
        auto rate=[](double bytes){return bytes>=1048576?Number(bytes/1048576,1)+L" MB/s":Number(bytes/1024,1)+L" KB/s";};
        Text(L"↓ "+rate(system.receiveRate),{548,226,768,256},4);Text(L"↑ "+rate(system.sendRate),{548,264,768,295},3);
        const wchar_t* metricLabels[]{L"DISK C:",L"VRAM CAPACITY",L"UP FOR"};auto minutes=GetTickCount64()/60000;
        std::wstring values[]{Number(system.diskUsedGiB)+L" / "+Number(system.diskTotalGiB)+L" GB",hardwareState.vramGiB>0?Number(hardwareState.vramGiB,1)+L" GB":L"Shared memory",Number(static_cast<double>(minutes/60))+L" h "+Number(static_cast<double>(minutes%60))+L" m"};
        for(int i=0;i<3;++i){float x=18+i*172.0f;Fill({x,246,x+162,312},theme::Inset,14);Text(metricLabels[i],{x+12,249,x+150,274},0,theme::Muted);Text(values[i],{x+12,276,x+150,307},2);}
    }
    void ReadClipboard() {
        clipboard = L"The clipboard does not contain text.";
        if (!OpenClipboard(window)) { clipboard=L"Clipboard is busy. Try Refresh."; return; }
        if (HANDLE data=GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text=static_cast<const wchar_t*>(GlobalLock(data))) {
                const size_t capacity=GlobalSize(data)/sizeof(wchar_t);
                clipboard.assign(text,wcsnlen_s(text,std::min<size_t>(capacity,8192))); GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    void ClipboardView() {
        Fill({22,54,738,89},0x202020,8); Text(L"Current clipboard",{36,54,630,89},2,0xAAAAAA);
        Fill({22,104,220,341},0x141414,10); Text(L"TEXT",{36,111,200,139},0,0x777777);
        Fill({30,146,212,188},0x30291A,8); Text(clipboard,{42,150,200,184},1,0xD8C28D);
        Fill({232,104,738,341},0x181818,10);
        formats[2]->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        Text(clipboard,{250,120,720,322},2); formats[2]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        Button(ClipboardRefresh,{22,354,290,400},L"Refresh",L"Read current text",L"\xE72C");
        Text(L"History collection is off. Nothing is saved.",{310,354,738,400},0,0x888888);
    }
    void FilterApps() {
        filteredApps.clear(); appPage=0;
        for (size_t i=0;i<apps.size();++i) {
            if (appQuery.empty() || FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE,
                apps[i].name.c_str(),-1,appQuery.c_str(),-1,nullptr,nullptr,nullptr,0)>=0) filteredApps.push_back(i);
        }
        std::stable_sort(filteredApps.begin(),filteredApps.end(),[this](size_t a,size_t b){return AppGroup(apps[a])<AppGroup(apps[b]);});
    }
    size_t AppSlots()const{return static_cast<size_t>(std::clamp(std::floor((LayoutHeight()-210)/39),1.0f,8.0f));}
    void Apps() {
        Fill({18,54,LayoutWidth()-18,100},theme::Card,12);
        Text(L"\xE721",{27,55,45,98},6,theme::Muted);
        EnsureAppSearch();
        if(renderMode||!appSearch||GetWindowTextLengthW(appSearch)==0)Text(L"Search installed apps…",{54,55,LayoutWidth()-42,98},2,theme::Muted);
        int group=-1;float nextY=113;
        for (size_t slot=0;slot<AppSlots() && appPage*AppSlots()+slot<filteredApps.size();++slot) {
            const auto& entry=apps[filteredApps[appPage*AppSlots()+slot]];int current=AppGroup(entry);if(current!=group){group=current;Text(group==0?L"STARRED":group==1?L"RECENT":L"APPLICATIONS",{26,nextY,LayoutWidth()-26,nextY+22},0,theme::Muted);nextY+=25;}
            float y=nextY;nextY+=39;int id=300+static_cast<int>(slot);D2D1_RECT_F row{18,y,LayoutWidth()-18,y+35};AddHit(id,row,entry.name);
            Fill(row,Favorite(entry)?theme::AccentSoft:dock::Mix(theme::Surface,theme::AccentSoft,HoverAmount(id)),8);
            Fill({27,y+4,55,y+31},theme::CardHover,7);Text(entry.name.substr(0,1),{27,y+4,55,y+31},7,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(entry.name,{68,y,LayoutWidth()-155,y+35},7);Text(L"Application",{LayoutWidth()-153,y,LayoutWidth()-60,y+35},0,theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);
            AddHit(900+static_cast<int>(slot),{LayoutWidth()-54,y+2,LayoutWidth()-22,y+33},Favorite(entry)?L"Unstar "+entry.name:L"Star "+entry.name);
            Text(Favorite(entry)?L"\xE735":L"\xE734",{LayoutWidth()-54,y+2,LayoutWidth()-22,y+33},6,Favorite(entry)?theme::Gold:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(id,row);
        }
        if (filteredApps.empty()) Text(apps.empty()?L"Loading installed apps…":L"No matching apps",{24,143,LayoutWidth()-24,246},3,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        float footer=LayoutHeight()-42;
        Text(Number(static_cast<double>(filteredApps.size()))+L" apps",{24,footer,280,footer+28},1,theme::Muted);
        SmallButton(AppsPrevious,{LayoutWidth()-154,footer,LayoutWidth()-94,footer+28},L"\xE76B",L"Previous apps",appPage>0);
        SmallButton(AppsNext,{LayoutWidth()-82,footer,LayoutWidth()-22,footer+28},L"\xE76C",L"Next apps",(appPage+1)*AppSlots()<filteredApps.size());
    }
    void SmallButton(int id,D2D1_RECT_F rect,const wchar_t* icon,const wchar_t* label,bool enabled=true) {
        AddHit(id,rect,label,enabled); Fill(rect,id==AddTask?theme::Gold:Hover(id)?0x303030:0x1E1E1E,10);
        Text(icon,rect,6,id==AddTask?theme::GoldInk:enabled?theme::Gold:0x666666,DWRITE_TEXT_ALIGNMENT_CENTER); FocusRing(id,rect,10);
    }
    void Wallpaper() {
        AddHit(WallpaperCollection,{24,54,116,81},L"Curated wallpaper collection");
        Fill({24,54,116,81},customWallpaperFolder?0x1E1E1E:0x2A261A,13);
        Text(L"Collection",{24,54,116,81},1,customWallpaperFolder?0xAAAAAA:0xD7BE80,DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(WallpaperCollection,{24,54,116,81});
        AddHit(WallpaperFolder,{128,54,242,81},L"Choose wallpaper folder");
        Text(L"My folder",{128,54,242,81},1,customWallpaperFolder?0xD7BE80:0xAAAAAA,DWRITE_TEXT_ALIGNMENT_CENTER); FocusRing(WallpaperFolder,{128,54,242,81});
        Text(Number(static_cast<double>(wallpapers.size()))+L" pictures",{260,54,500,81},0,0x777777);
        if (thumbnails.size()!=wallpapers.size()) {
            thumbnails.clear(); thumbnails.resize(wallpapers.size());
            for (size_t i=0;i<wallpapers.size();++i) {
                const auto& entry=wallpapers[i];
                if (entry.pixels && !entry.pixels->empty()) target->CreateBitmap(D2D1::SizeU(entry.width,entry.height),entry.pixels->data(),entry.width*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&thumbnails[i]);
            }
        }
        std::vector<size_t> order(wallpapers.size());for(size_t i=0;i<order.size();++i)order[i]=i;
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return std::abs(static_cast<float>(a)-wallpaperFocus.value)>std::abs(static_cast<float>(b)-wallpaperFocus.value);});
        for(size_t index:order){
            float relative=static_cast<float>(index)-wallpaperFocus.value,distance=std::abs(relative);if(distance>3.0f)continue;
            float prominence=std::exp(-distance*distance*3.0f),w=64+236*prominence,h=148+10*prominence,y=106-10*prominence;
            float center=420+(relative<0?-1.0f:1.0f)*(90*distance+75*(1-std::exp(-distance*3.0f))),x=center-w/2;
            D2D1_RECT_F rect{x,y,x+w,y+h}; AddHit(500+static_cast<int>(index),rect,L"Preview "+wallpapers[index].title);
            D2D1::Matrix3x2F transform;target->GetTransform(&transform);target->SetTransform(D2D1::Matrix3x2F::Skew(-12,0,{center,y+h/2})*transform);
            Fill(rect,0x222222,10);
            if (thumbnails[index]) {
                auto size=thumbnails[index]->GetSize(); float ratio=std::max(w/size.width,h/size.height);
                float sw=w/ratio,sh=h/ratio;
                ComPtr<ID2D1RoundedRectangleGeometry> mask;Check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect,10,10),&mask));auto layer=D2D1::LayerParameters();layer.geometricMask=mask.Get();target->PushLayer(layer,nullptr);
                target->DrawBitmap(thumbnails[index].Get(),rect,0.55f+0.45f*prominence,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    D2D1::RectF((size.width-sw)/2,(size.height-sh)/2,(size.width+sw)/2,(size.height+sh)/2));
                target->PopLayer();
            }
            FocusRing(500+static_cast<int>(index),rect,10);
            target->SetTransform(transform);
        }
        if (wallpapers.empty()) Text(note.empty()?L"Loading wallpapers…":L"No wallpapers found",{24,100,816,240},3,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
        SmallButton(WallpaperPrevious,{24,178,72,218},L"\xE76B",L"Previous wallpaper",selectedWallpaper>0);
        SmallButton(WallpaperNext,{688,178,736,218},L"\xE76C",L"Next wallpaper",selectedWallpaper+1<wallpapers.size());
        TextButton(WallpaperApply,{304,266,536,294},wallpaperApplying?L"Changing…":L"Set wallpaper",true,!wallpapers.empty()&&!wallpaperApplying);
    }
    void Power() {
        const int actions[]{Lock,SleepNow,RestartNow,ShutdownNow};
        const wchar_t* titles[]{L"Lock",L"Sleep",L"Restart",L"Shut down"};
        const wchar_t* icons[]{L"\xE72E",L"\xE708",L"\xE777",L"\xE7E8"};
        for (int i=0;i<4;++i) {
            float x=18+i*143.5f; D2D1_RECT_F rect{x,54,x+133.5f,136};
            AddHit(actions[i],rect,titles[i]); Fill(rect,Hover(actions[i])?0x2A2A2A:0x1C1C1C,12);
            Text(icons[i],{x,65,x+133.5f,99},6,0xE0DFDA,DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(titles[i],{x,101,x+133.5f,128},7,0xD6D5D0,DWRITE_TEXT_ALIGNMENT_CENTER);
            FocusRing(actions[i],rect);
        }
    }
    void OpenFlyout(Flyout next){
        CapturePane();paneBlend.Snap(reducedMotion||renderMode?1.0f:0.0f);paneBlend.goal=1;
        flyout=next;connectionPage=0;selectedWifi=selectedBluetooth=-1;focused=hovered=-1;
        if(wifiPassword)SetWindowTextW(wifiPassword,L"");
        if(!renderMode){if(!connectivity)connectivity=std::make_unique<Connectivity>(window);
            if(next==Flyout::Wifi)connectivity->RefreshWifi(true);else connectivity->RefreshBluetooth();}
        SetExpanded(true);
    }
    void EnsurePassword(){
        if(wifiPassword||renderMode||capturingPane)return;
        wifiPassword=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,0,0,1,1,window,reinterpret_cast<HMENU>(402),nullptr,nullptr);
        EnsureEditResources();SendMessageW(wifiPassword,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);SendMessageW(wifiPassword,EM_SETLIMITTEXT,64,0);
        SendMessageW(wifiPassword,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Network security key"));PositionInputs();
    }
    size_t ConnectionSlots()const{
        bool selected=flyout==Flyout::Wifi?selectedWifi>=0:selectedBluetooth>=0;
        return static_cast<size_t>(std::max(1.0f,std::floor((LayoutHeight()-(selected?244.0f:152.0f))/56.0f)));
    }
    void ConnectionView(){
        float w=LayoutWidth(),h=LayoutHeight();bool wifi=flyout==Flyout::Wifi;
        SmallButton(FlyoutBack,{18,16,52,48},L"\xE72B",L"Back to controls");
        Text(wifi?L"Wi-Fi":L"Bluetooth",{65,14,w-126,49},3);
        SmallButton(FlyoutRefresh,{w-115,16,w-81,48},L"\xE72C",wifi?L"Refresh networks":L"Find devices",!(wifi?connections.wifiBusy:connections.bluetoothBusy));
        bool enabled=wifi?connections.wifiOn:connections.bluetoothOn,available=wifi?connections.wifiAvailable:connections.bluetoothAvailable;
        D2D1_RECT_F toggle{w-68,21,w-18,45};AddHit(FlyoutToggle,toggle,enabled?L"Turn off":L"Turn on",available);
        Fill(toggle,enabled?theme::Gold:0x3A393F,12);brush->SetColor(Color(enabled?theme::GoldInk:theme::Muted));
        target->FillEllipse(D2D1::Ellipse({enabled?w-31:w-55,33},8,8),brush.Get());FocusRing(FlyoutToggle,toggle,12);
        const auto& message=wifi?connections.wifiMessage:connections.bluetoothMessage;
        formats[0]->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        Text(message.empty()?(wifi?L"Available networks":L"Your devices"):message,{18,52,w-18,84},0,theme::Muted);
        formats[0]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        size_t count=wifi?connections.networks.size():connections.devices.size(),slots=ConnectionSlots();
        for(size_t slot=0;slot<slots&&connectionPage*slots+slot<count;++slot){
            size_t index=connectionPage*slots+slot;float y=90+slot*56.0f;int id=(wifi?1000:1200)+static_cast<int>(index);
            bool selected=wifi?static_cast<int>(index)==selectedWifi:static_cast<int>(index)==selectedBluetooth;
            bool connected=wifi?connections.networks[index].connected:connections.devices[index].connected;
            std::wstring name=wifi?connections.networks[index].name:connections.devices[index].name;
            std::wstring status=wifi?(connected?L"Connected":connections.networks[index].secure?L"Secured":L"Open network"):
                connected?L"Connected":connections.devices[index].paired?L"Paired":L"Ready to pair";
            if(wifi&&connected&&connections.networks[index].secure)status+=L", secured";
            D2D1_RECT_F row{18,y,w-18,y+49};AddHit(id,row,name);
            Fill(row,dock::Mix(selected?0x342D1D:theme::Card,selected?0x443A24:theme::CardHover,HoverAmount(id)),12);
            const wchar_t* icon=wifi?L"\xE701":!connections.devices[index].lowEnergy&&(connections.devices[index].native.ulClassofDevice&0x1F00)==0x0400?L"\xE7F6":L"\xE702";
            Text(icon,{30,y+4,57,y+45},6,connected?theme::Gold:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(name,{70,y+3,w-77,y+26},2);Text(status,{70,y+26,w-74,y+45},0,theme::Muted);
            Text(wifi?Number(connections.networks[index].quality)+L"%":connected?L"\xE73E":L"\xE76C",{w-75,y+6,w-32,y+44},wifi?0:6,connected?theme::Gold:theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);
            FocusRing(id,row,12);
        }
        if(!count){bool busy=wifi?connections.wifiBusy:connections.bluetoothBusy;
            Text(busy?L"Searching…":!available?L"No adapter available":!enabled?wifi?L"Wi-Fi is off":L"Bluetooth is off":wifi?L"No networks found":L"No devices found",{18,112,w-18,224},3,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);}
        bool selected=wifi?selectedWifi>=0:selectedBluetooth>=0;
        float navigation=selected?h-203:h-92;
        if(count>slots){SmallButton(FlyoutPrevious,{w-105,navigation,w-69,navigation+28},L"\xE76B",L"Previous page",connectionPage>0);
            SmallButton(FlyoutNext,{w-57,navigation,w-21,navigation+28},L"\xE76C",L"Next page",(connectionPage+1)*slots<count);}
        if(wifi&&selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())){
            const auto& network=connections.networks[selectedWifi];Fill({18,h-151,w-18,h-62},theme::Inset,12);
            if(network.secure&&network.profile.empty()&&!network.connected){
                Fill({26,h-133,w-26,h-96},theme::CardHover,8);EnsurePassword();
                if(renderMode||!wifiPassword||GetWindowTextLengthW(wifiPassword)==0)Text(L"Network security key",{32,h-133,w-32,h-96},1,theme::Muted);
            }else Text(network.connected?L"You are connected to this network.":network.profile.empty()?L"This network does not require a password.":L"Use the saved Windows network profile.",{30,h-141,w-30,h-101},1,theme::Muted);
            bool canConnect=network.connected||!network.secure||!network.profile.empty()||(wifiPassword&&GetWindowTextLengthW(wifiPassword)>0);
            TextButton(network.connected?FlyoutDisconnect:FlyoutConnect,{w-172,h-94,w-28,h-64},network.connected?L"Disconnect":connections.wifiConnecting?L"Connecting…":L"Connect",!network.connected,canConnect&&!connections.wifiConnecting);
        }
        if(!wifi&&selectedBluetooth>=0&&selectedBluetooth<static_cast<int>(connections.devices.size())){
            const auto& device=connections.devices[selectedBluetooth];Fill({18,h-151,w-18,h-62},theme::Inset,12);
            Text(device.name,{30,h-144,w-30,h-113},2);
            TextButton(device.connected?FlyoutDisconnect:device.paired?FlyoutConnect:FlyoutPair,{w-176,h-104,w-30,h-73},device.connected?L"Disconnect":device.paired?L"Connect":L"Pair",!device.connected);
            if(device.paired)TextButton(FlyoutRemove,{30,h-104,156,h-73},L"Remove device");
        }
        TextButton(FlyoutSettings,{18,h-48,w-18,h-16},L"More "+std::wstring(wifi?L"Wi-Fi":L"Bluetooth")+L" settings  ↗");
    }
    void RefreshConnectionsView(){
        if(!connectivity)return;
        std::wstring ssid=selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())?connections.networks[selectedWifi].name:L"";
        ULONGLONG address=selectedBluetooth>=0&&selectedBluetooth<static_cast<int>(connections.devices.size())?connections.devices[selectedBluetooth].native.Address.ullLong:0;
        connections=connectivity->Snapshot();selectedWifi=selectedBluetooth=-1;
        for(size_t i=0;i<connections.networks.size();++i)if(connections.networks[i].name==ssid)selectedWifi=static_cast<int>(i);
        for(size_t i=0;i<connections.devices.size();++i)if(connections.devices[i].native.Address.ullLong==address)selectedBluetooth=static_cast<int>(i);
        system.wifi=L"Not connected";for(const auto& network:connections.networks)if(network.connected){system.wifi=network.name;break;}
        system.bluetooth=connections.bluetoothOn?L"On":L"Off";for(const auto& device:connections.devices)if(device.connected){system.bluetooth=device.name;break;}
        size_t count=flyout==Flyout::Wifi?connections.networks.size():connections.devices.size(),slots=ConnectionSlots();
        if(connectionPage*slots>=count)connectionPage=0;
        PositionInputs();if(expanded)InvalidateRect(window,nullptr,FALSE);
    }
    void PanelContent(){
        hits.clear();
        if(miniMode){MediaCard(true);return;}
        if(flyout!=Flyout::None){ConnectionView();return;}
        Tabs();
            switch(tab) {
            case Tab::Controls: Controls(); break;
            case Tab::Today: Today(); break;
            case Tab::Stats: Stats(); break;
            case Tab::Clipboard: ClipboardView(); break;
            case Tab::Apps: Apps(); break;
            case Tab::Wallpaper: Wallpaper(); break;
            case Tab::Power: Power(); break;
            }
        if(!note.empty()){float bottom=LayoutHeight();Fill({20,bottom-39,LayoutWidth()-20,bottom-9},0x30291A,8);Text(note,{32,bottom-39,LayoutWidth()-32,bottom-9},0,0xE0CD98);}
    }
    void CapturePane(){
        if(renderMode||reducedMotion||!target||dockReveal.value<0.8f)return;
        auto savedTarget=target;auto savedHits=hits;capturingPane=true;
        try{
            ComPtr<ID2D1BitmapRenderTarget> layer;
            Check(target->CreateCompatibleRenderTarget(D2D1::SizeF(LayoutWidth(),LayoutHeight()),&layer));
            Check(layer.As(&target));target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(Color(theme::Surface));
            PanelContent();Check(target->EndDraw());Check(layer->GetBitmap(&previousPane));
            previousPaneWidth=LayoutWidth();previousPaneHeight=LayoutHeight();
        }catch(const winrt::hresult_error&){previousPane.Reset();}
        target=savedTarget;hits=std::move(savedHits);capturingPane=false;
    }
    void Draw() {
        Graphics(); target->BeginDraw(); target->SetTransform(D2D1::Matrix3x2F::Identity()); target->Clear(renderMode&&expanded?Color(theme::Surface):D2D1::ColorF(0,0.0f));
        if(renderMode){
            if(expanded)PanelContent();else{hits.clear();AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");DrawNotch();}
        }else{
            float center=theme::EnvelopeWidth/2;
            if(browserContext||dockReveal.value<=0.003f)DrawNotch();
            if(dockReveal.value>0.003f){
                (void)center;IslandSurface();
                if(previousPane&&paneBlend.value<1){
                    target->SetTransform(D2D1::Matrix3x2F::Translation((theme::EnvelopeWidth-previousPaneWidth)/2,PanelY()));
                    target->DrawBitmap(previousPane.Get(),{0,0,previousPaneWidth,previousPaneHeight},dockReveal.value*(1-paneBlend.value));
                }
                target->SetTransform(D2D1::Matrix3x2F::Translation(PanelX(),PanelY()));
                auto layer=D2D1::LayerParameters();layer.opacity=std::clamp(dockReveal.value*paneBlend.value,0.0f,1.0f);
                target->PushLayer(layer,nullptr);PanelContent();target->PopLayer();
            }else{hits.clear();AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");}
        }
        HRESULT result=target->EndDraw(); if (result==D2DERR_RECREATE_TARGET) ReleaseGraphics(); else Check(result);
    }
    int HitAt(float x,float y) {
        if(!expanded){float left=renderMode?0:(theme::EnvelopeWidth-theme::DockWidth)/2;return y>=HandleY()&&y<=HandleY()+theme::DockHeight&&x>=left&&x<=left+theme::DockWidth?0:-1;}
        if(!renderMode&&(dockReveal.value<0.94f||std::abs(width-LayoutWidth())>8))return -1;
        x-=PanelX();y-=PanelY();
        for (int i=static_cast<int>(hits.size())-1;i>=0;--i) {
            const auto& r=hits[i].rect;
            if (hits[i].enabled && x>=r.left && x<=r.right && y>=r.top && y<=r.bottom) return i;
        }
        return -1;
    }
    void SetVolumeAt(int x) { system.SetVolume((x*96.0f/dpi-PanelX()-18)/564); InvalidateRect(window,nullptr,FALSE); }
    void Open(const wchar_t* path) {
        auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",path,nullptr,nullptr,SW_SHOWNORMAL));
        if (result<=32) { note=L"Windows could not open this item."; InvalidateRect(window,nullptr,FALSE); }
    }
    void EnsureTaskEdit() {
        if (taskEdit || renderMode||capturingPane) return;
        taskEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(400),nullptr,nullptr);
        SendMessageW(taskEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Add a task…"));
        SendMessageW(taskEdit,EM_SETLIMITTEXT,120,0);
        EnsureEditResources();
        SendMessageW(taskEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        PositionInputs();
    }
    void EnsureEditResources() {
        if (!editFont) editFont=CreateFontW(-MulDiv(13,static_cast<int>(dpi),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        if (!editBackground) editBackground=CreateSolidBrush(RGB(32,32,32));
    }
    void EnsureAppSearch() {
        if (appSearch || renderMode||capturingPane) return;
        appSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(401),nullptr,nullptr);
        SendMessageW(appSearch,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search installed apps…"));
        SendMessageW(appSearch,EM_SETLIMITTEXT,120,0); EnsureEditResources();
        SendMessageW(appSearch,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        PositionInputs();
        if (!appQuery.empty()) SetWindowTextW(appSearch,appQuery.c_str());
    }
    void SaveTasks() {
        if (renderMode) return;
        std::vector<std::pair<std::wstring,bool>> data;
        for (const auto& task:tasks) data.emplace_back(task.title,task.done);
        if (!taskStore.Save(data)) note=L"Could not save tasks on this PC.";
    }
    void OpenApp(const AppEntry& app) {
        PIDLIST_ABSOLUTE item=nullptr;
        if (FAILED(SHParseDisplayName(app.parsingName.c_str(),nullptr,&item,0,nullptr))) { note=L"This application is no longer available."; return; }
        SHELLEXECUTEINFOW action{sizeof(action)};
        action.fMask=SEE_MASK_IDLIST|SEE_MASK_NOASYNC; action.hwnd=window; action.lpVerb=L"open"; action.lpIDList=item; action.nShow=SW_SHOWNORMAL;
        if (!ShellExecuteExW(&action)) note=L"Windows could not open this application.";
        else{auto key=AppKey(app);auto& recent=appPreferences.recent;recent.erase(std::remove(recent.begin(),recent.end(),key),recent.end());recent.insert(recent.begin(),key);if(recent.size()>16)recent.resize(16);appPreferences.Save();FilterApps();}
        CoTaskMemFree(item);
    }
    void ChooseWallpaperFolder() {
        ModalGuard modal(modalOpen);
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return;
        DWORD options=0; dialog->GetOptions(&options); dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
        dialog->SetTitle(L"Choose a wallpaper folder");
        if (FAILED(dialog->Show(window))) return;
        ComPtr<IShellItem> item; if (FAILED(dialog->GetResult(&item))) return;
        PWSTR path=nullptr; if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) return;
        if (!catalog) catalog=std::make_unique<DesktopCatalog>(window);
        customWallpaperFolder=true; selectedWallpaper=0; wallpaperFocus.Snap(0);
        wallpapersRequested=true; catalog->DiscoverWallpapers(path); CoTaskMemFree(path);
        note=L"Loading pictures from your folder…";
    }
    void PowerAction(int id) {
        ModalGuard modal(modalOpen);
        const wchar_t* prompt=id==SleepNow?L"Put this PC to sleep?":id==RestartNow?L"Restart Windows? Save your work before continuing.":L"Shut down Windows? Save your work before continuing.";
        if (MessageBoxW(window,prompt,L"Dako",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return;
        if (id==SleepNow) { if (!SetSuspendState(FALSE,FALSE,FALSE)) note=L"Windows could not put this PC to sleep."; return; }
        HANDLE token=nullptr; bool enabled=false; TOKEN_PRIVILEGES previous{}; DWORD previousSize=sizeof(previous);
        if (OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token)) {
            TOKEN_PRIVILEGES privileges{}; privileges.PrivilegeCount=1;
            if (LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&privileges.Privileges[0].Luid)) {
                privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED; SetLastError(ERROR_SUCCESS);
                enabled=AdjustTokenPrivileges(token,FALSE,&privileges,sizeof(previous),&previous,&previousSize) && GetLastError()==ERROR_SUCCESS;
            }
        }
        bool succeeded=enabled && ExitWindowsEx(id==RestartNow?EWX_REBOOT:EWX_POWEROFF,SHTDN_REASON_MAJOR_OTHER|SHTDN_REASON_MINOR_OTHER|SHTDN_REASON_FLAG_PLANNED);
        if (token) { if (enabled) AdjustTokenPrivileges(token,FALSE,&previous,0,nullptr,nullptr); CloseHandle(token); }
        if (!succeeded)
            note=L"Windows could not complete this power action.";
    }
    void Invoke(int id) {
        note.clear();
        if(id>=900&&id<908&&appPage*AppSlots()+id-900<filteredApps.size()){
            auto key=AppKey(apps[filteredApps[appPage*AppSlots()+id-900]]);auto& favorites=appPreferences.favorites;auto found=std::find(favorites.begin(),favorites.end(),key);if(found==favorites.end())favorites.push_back(key);else favorites.erase(found);if(!renderMode)appPreferences.Save();FilterApps();InvalidateRect(window,nullptr,FALSE);return;
        }
        if(id>=1000&&id<1128&&id-1000<static_cast<int>(connections.networks.size())){
            selectedWifi=id-1000;connectionPage=static_cast<size_t>(selectedWifi)/ConnectionSlots();if(wifiPassword)SetWindowTextW(wifiPassword,L"");
            PositionInputs();InvalidateRect(window,nullptr,FALSE);return;
        }
        if(id>=1200&&id<1328&&id-1200<static_cast<int>(connections.devices.size())){
            selectedBluetooth=id-1200;connectionPage=static_cast<size_t>(selectedBluetooth)/ConnectionSlots();InvalidateRect(window,nullptr,FALSE);return;
        }
        if (id>=100 && id<107) { SetTab(static_cast<Tab>(id-100)); return; }
        if (id>=200 && id<205 && id-200<static_cast<int>(tasks.size())) { tasks[id-200].done=!tasks[id-200].done; SaveTasks(); }
        else if (id>=300 && id<308 && appPage*AppSlots()+id-300<filteredApps.size()) OpenApp(apps[filteredApps[appPage*AppSlots()+id-300]]);
        else if (id>=500 && id<500+static_cast<int>(DesktopCatalog::MaxWallpapers) && id-500<static_cast<int>(wallpapers.size())) selectedWallpaper=id-500;
        else switch(id) {
        case Expand: if(miniMode){miniMode=false;tab=Tab::Controls;}SetExpanded(true);break;
        case Wifi: OpenFlyout(Flyout::Wifi); break;
        case Bluetooth: OpenFlyout(Flyout::Bluetooth); break;
        case Audio: Open(L"ms-settings:sound"); break;
        case Night: Open(L"ms-settings:nightlight"); break;
        case Dark: Open(L"ms-settings:colors"); break;
        case Mute: system.ToggleMute(); break;
        case Previous: case Play: case Next: if (media) media->Transport(id-Previous); break;
        case AddTask: {
            wchar_t text[121]{}; if (taskEdit) GetWindowTextW(taskEdit,text,121);
            if (tasks.size()>=5) note=L"There is room for five tasks. Delete one to add another.";
            else if (wcslen(text)) { tasks.push_back({text,false}); SetWindowTextW(taskEdit,L""); SaveTasks(); }
            else if (taskEdit) SetFocus(taskEdit);
            break;
        }
        case ClipboardRefresh: ReadClipboard(); break;
        case WallpaperSettings: Open(L"ms-settings:personalization-background"); break;
        case PowerSettings: Open(L"ms-settings:powersleep"); break;
        case AppsPrevious: if (appPage>0) --appPage; break;
        case AppsNext: if ((appPage+1)*AppSlots()<filteredApps.size()) ++appPage; break;
        case WallpaperFolder: ChooseWallpaperFolder(); break;
        case WallpaperCollection:
            if (!catalog) catalog=std::make_unique<DesktopCatalog>(window);
            customWallpaperFolder=false;selectedWallpaper=2;wallpaperFocus.Snap(2);
            wallpapersRequested=true; catalog->DiscoverWallpapers(); break;
        case WallpaperPrevious: if (selectedWallpaper>0) --selectedWallpaper; break;
        case WallpaperNext: if (selectedWallpaper+1<wallpapers.size()) ++selectedWallpaper; break;
        case WallpaperApply:
            if(selectedWallpaper<wallpapers.size()&&!wallpaperApplying&&!renderMode){wallpaperApplying=true;wallpaperReveal.Freeze(reducedMotion);if(!catalog)catalog=std::make_unique<DesktopCatalog>(window);catalog->ApplyWallpaperAsync(wallpapers[selectedWallpaper].path);}
            break;
        case SleepNow: case RestartNow: case ShutdownNow: PowerAction(id); break;
        case FlyoutBack:SetTab(Tab::Controls);break;
        case FlyoutRefresh:
            if(connectivity){if(flyout==Flyout::Wifi)connectivity->RefreshWifi(true);else connectivity->RefreshBluetooth(true);}break;
        case FlyoutToggle:
            if(connectivity){if(flyout==Flyout::Wifi)connectivity->SetWifi(!connections.wifiOn);else connectivity->SetBluetooth(!connections.bluetoothOn);}break;
        case FlyoutSettings:Open(flyout==Flyout::Wifi?L"ms-settings:network-wifi":L"ms-settings:bluetooth");break;
        case FlyoutConnect:case FlyoutDisconnect:case FlyoutPair:case FlyoutRemove:
            if(!connectivity)break;
            if(flyout==Flyout::Wifi&&selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())){
                if(id==FlyoutDisconnect)connectivity->DisconnectWifi(connections.networks[selectedWifi]);
                else{wchar_t secret[65]{};if(wifiPassword){GetWindowTextW(wifiPassword,secret,65);SetWindowTextW(wifiPassword,L"");}
                    connectivity->ConnectWifi(connections.networks[selectedWifi],secret);SecureZeroMemory(secret,sizeof(secret));}
            }else if(flyout==Flyout::Bluetooth&&selectedBluetooth>=0&&selectedBluetooth<static_cast<int>(connections.devices.size())){
                if(id==FlyoutRemove){ModalGuard modal(modalOpen);if(MessageBoxW(window,L"Remove this Bluetooth device? You will need to pair it again.",L"Dako",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)break;}
                connectivity->BluetoothAction(connections.devices[selectedBluetooth],id==FlyoutConnect?0:id==FlyoutDisconnect?1:id==FlyoutPair?2:3);
            }break;
        case FlyoutPrevious:if(connectionPage>0)--connectionPage;break;
        case FlyoutNext:{size_t count=flyout==Flyout::Wifi?connections.networks.size():connections.devices.size();if((connectionPage+1)*ConnectionSlots()<count)++connectionPage;break;}
        case FocusStart:
            if(focusRunning){focusSeconds=static_cast<int>((focusDeadline>GetTickCount64()?focusDeadline-GetTickCount64():0)+999)/1000;focusRunning=false;}
            else{if(focusSeconds<=0)focusSeconds=focusDuration;focusDeadline=GetTickCount64()+static_cast<ULONGLONG>(focusSeconds)*1000;focusRunning=true;}UpdateTimers();break;
        case FocusReset:focusRunning=false;focusSeconds=focusDuration;UpdateTimers();break;
        case FocusOne:case FocusShort:case FocusTen:case FocusMedium:case FocusTwenty:case FocusLong:focusRunning=false;focusDuration=focusSeconds=id==FocusOne?60:id==FocusShort?300:id==FocusTen?600:id==FocusMedium?900:id==FocusTwenty?1200:1500;UpdateTimers();break;
        case Lock: LockWorkStation(); break;
        case Quit: PostMessageW(window,WM_CLOSE,0,0); break;
        }
        wallpaperFocus.goal=static_cast<float>(selectedWallpaper);if(reducedMotion||renderMode)wallpaperFocus.Snap(wallpaperFocus.goal);else if(wallpaperFocus.Moving())ArmMotion();
        InvalidateRect(window,nullptr,FALSE);
    }
    void Key(WPARAM key) {
        if (key==VK_ESCAPE) {if(flyout!=Flyout::None)SetTab(Tab::Controls);else SetExpanded(false);return;}
        if (key==VK_TAB && !hits.empty()) {
            keyboardOpen=true;
            int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;
            for (size_t i=0;i<hits.size();++i) {
                focused=(focused+direction+static_cast<int>(hits.size()))%static_cast<int>(hits.size());
                if (hits[focused].enabled) break;
            }
            InvalidateRect(window,nullptr,FALSE); return;
        }
        if (focused<0 || focused>=static_cast<int>(hits.size())) return;
        int id=hits[focused].id;
        if (key==VK_DELETE && id>=200 && id<205 && id-200<static_cast<int>(tasks.size())) {
            tasks.erase(tasks.begin()+id-200); focused=-1; SaveTasks(); InvalidateRect(window,nullptr,FALSE); return;
        }
        if (id==Volume) {
            if (key==VK_LEFT || key==VK_DOWN) system.SetVolume(system.volume-0.02f);
            if (key==VK_RIGHT || key==VK_UP) system.SetVolume(system.volume+0.02f);
            if (key==VK_SPACE) system.ToggleMute();
            InvalidateRect(window,nullptr,FALSE);
        } else if (key==VK_RETURN || key==VK_SPACE) Invoke(id);
    }
    void ContextMenu() {
        ModalGuard modal(modalOpen);
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,expanded?L"Hide dock":L"Show dock");
        AppendMenuW(menu,MF_STRING,4,shell.Active()?L"Restore Windows taskbar\tCtrl+Alt+Shift+T":L"Hide Windows taskbar\tCtrl+Alt+Shift+T");
        AppendMenuW(menu,MF_STRING,5,miniMode?L"Full controls":L"Compact media player");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,2,L"Exit and restore Windows\tCtrl+Alt+Shift+Q");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu); if(command==1){SetExpanded(!expanded);keyboardOpen=expanded;}else if(command==2)PostMessageW(window,WM_CLOSE,0,0);
        else if(command==4){if(shell.Active())shell.Stop();else if(!shell.Start())MessageBoxW(window,L"The recovery process could not start. The Windows taskbar remains available.",L"Dako",MB_OK|MB_ICONERROR);}
        else if(command==5){miniMode=!miniMode;tab=Tab::Controls;flyout=Flyout::None;SetExpanded(true);keyboardOpen=true;}
    }
    // A deterministic render target for visual QA of our own UI, without capturing the desktop.
    void SaveRender(const std::wstring& path) {
        ReleaseGraphics();
        ComPtr<IWICImagingFactory> images;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
        ComPtr<IWICBitmap> bitmap;
        UINT imageWidth=static_cast<UINT>(renderMode?width:theme::EnvelopeWidth),imageHeight=static_cast<UINT>(renderMode?height:envelopeHeight);
        Check(images->CreateBitmap(imageWidth,imageHeight,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        Check(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target));
        Check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush));
        Draw();
        ComPtr<IWICStream> stream; Check(images->CreateStream(&stream)); Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder; Check(images->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
        Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame; Check(encoder->CreateNewFrame(&frame,nullptr)); Check(frame->Initialize(nullptr));
        Check(frame->SetSize(imageWidth,imageHeight));
        WICPixelFormatGUID pixelFormat=GUID_WICPixelFormat32bppPBGRA; Check(frame->SetPixelFormat(&pixelFormat));
        Check(frame->WriteSource(bitmap.Get(),nullptr)); Check(frame->Commit()); Check(encoder->Commit());
        ReleaseGraphics();
    }
    void RenderSuite(const std::wstring& directory) {
        reducedMotion=true; dpi=96;
        hardwareState.cpu=L"Processor";hardwareState.gpu=L"Graphics adapter";hardwareState.gpuValid=true;hardwareState.gpuPercent=12;hardwareState.vramGiB=8;hardwareState.totalGiB=32;hardwareState.processes=229;hardwareState.top={{L"Browser",1400},{L"Editor",820},{L"Music",410},{L"Terminal",300}};system.totalCpu=37;system.memoryLoad=61;system.availableGiB=12.5;
        auto bundled=DesktopCatalog::LoadWallpapers(DesktopCatalog::BundledWallpaperRoot());
        if(bundled.size()!=18)winrt::throw_hresult(E_UNEXPECTED);
        const auto beforeInvalid=DesktopCatalog::CurrentWallpaperPath();
        if(SUCCEEDED(DesktopCatalog::ApplyWallpaper(L"")) || SUCCEEDED(DesktopCatalog::ApplyWallpaper((DesktopCatalog::BundledWallpaperRoot()/L"missing-image.jpg").wstring())) ||
            DesktopCatalog::CurrentWallpaperPath()!=beforeInvalid)winrt::throw_hresult(E_UNEXPECTED);
        wallpapers=DesktopCatalog::LoadDefaultWallpapers();
        if(wallpapers.size()<18 || wallpapers.size()>19)winrt::throw_hresult(E_UNEXPECTED);
        if(wallpapers.size()==19 && wallpapers.back().title!=L"Your wallpaper")winrt::throw_hresult(E_UNEXPECTED);
        for(const auto& entry:wallpapers)if(!entry.pixels||entry.width>320||entry.height>200)winrt::throw_hresult(E_UNEXPECTED);
        tasks={{L"Review the desktop",false},{L"Take a break",true}};
        apps={{L"Browser",L""},{L"Calculator",L""},{L"Files",L""},{L"Music",L""},{L"Notes",L""},{L"Terminal",L""},{L"Text editor",L""},{L"Video player",L""}};appPreferences.favorites={L"Music",L"Notes"};appPreferences.recent={L"Browser",L"Files"};FilterApps();
        constexpr const wchar_t* names[]{L"controls",L"today",L"stats",L"clipboard",L"apps",L"wallpaper",L"power"};
        for (int i=0;i<7;++i) {
            tab=static_cast<Tab>(i); SetExpanded(true);
            SaveRender(directory+L"\\"+names[i]+L".png");
            if (hits.size()<7 || HitAt((ContentWidth()-600)/2+45,25)!=0) winrt::throw_hresult(E_UNEXPECTED);
        }
        // Exercise the old 12-photo boundary and the final dark-mode entry without applying it.
        tab=Tab::Wallpaper;SetExpanded(true);
        for(size_t i=0;i+1<wallpapers.size();++i)Invoke(WallpaperNext);
        const size_t last=wallpapers.size()-1;
        if(selectedWallpaper!=last)winrt::throw_hresult(E_UNEXPECTED);
        Invoke(WallpaperPrevious);Invoke(500+static_cast<int>(last));
        if(selectedWallpaper!=last)winrt::throw_hresult(E_UNEXPECTED);
        SaveRender(directory+L"\\wallpaper-last.png");
        selectedWallpaper=2;wallpaperFocus.Snap(2);
        connections.wifiAvailable=connections.wifiOn=connections.bluetoothAvailable=connections.bluetoothOn=true;
        WifiNetwork connected;connected.name=L"Home network";connected.connected=connected.secure=connected.connectable=true;connected.profile=L"Saved profile";connected.quality=92;
        WifiNetwork nearby;nearby.name=L"Guest network";nearby.secure=nearby.connectable=true;nearby.quality=71;
        WifiNetwork open;open.name=L"Open network";open.connectable=true;open.quality=48;connections.networks={connected,nearby,open};
        OpenFlyout(Flyout::Wifi);selectedWifi=0;SaveRender(directory+L"\\wifi.png");selectedWifi=1;SaveRender(directory+L"\\wifi-password.png");
        BluetoothEntry headphones;headphones.name=L"Wireless headphones";headphones.paired=headphones.connected=true;headphones.native.Address.ullLong=1;headphones.native.ulClassofDevice=0x0400;
        BluetoothEntry speaker;speaker.name=L"Living room speaker";speaker.paired=true;speaker.native.Address.ullLong=2;
        connections.devices={headphones,speaker};OpenFlyout(Flyout::Bluetooth);selectedBluetooth=0;SaveRender(directory+L"\\bluetooth.png");
        flyout=Flyout::None;
        tab=Tab::Controls;
        SetExpanded(false); SaveRender(directory+L"\\compact.png");
        if(width!=theme::DockWidth||height!=theme::DockHeight||hits.size()!=1||hits[0].id!=Expand)winrt::throw_hresult(E_UNEXPECTED);
        Invoke(Expand); if (!expanded || width!=600 || height!=368) winrt::throw_hresult(E_UNEXPECTED);
        renderMode=false;Place();
        HRGN region=CreateRectRgn(0,0,0,0);GetWindowRgn(window,region);
        int middle=static_cast<int>(theme::EnvelopeWidth/2);
        bool safe=PtInRegion(region,middle,5)&&PtInRegion(region,middle,24)&&PtInRegion(region,middle,static_cast<int>(PanelY()+30));
        DeleteObject(region);if(!safe)winrt::throw_hresult(E_UNEXPECTED);
        SaveRender(directory+L"\\dock-open.png");
        float anchor=PanelY();
        for(int i=0;i<7;++i){tab=static_cast<Tab>(i);SetExpanded(true);if(PanelY()!=anchor||PanelX()+(ContentWidth()-600)/2!=150)winrt::throw_hresult(E_UNEXPECTED);SaveRender(directory+L"\\attached-"+names[i]+L".png");}
        tab=Tab::Controls;browserContext=true;SetExpanded(true);Place();
        region=CreateRectRgn(0,0,0,0);GetWindowRgn(window,region);
        safe=PanelY()==112&&!PtInRegion(region,middle,40)&&!PtInRegion(region,middle,100)&&!PtInRegion(region,middle,110)&&PtInRegion(region,middle,142);
        DeleteObject(region);if(!safe)winrt::throw_hresult(E_UNEXPECTED);
        SaveRender(directory+L"\\dock-browser.png");browserContext=false;miniMode=true;SetExpanded(true);SaveRender(directory+L"\\media-mini.png");miniMode=false;
        SetExpanded(true);SetAccent(0x83B8DE);SaveRender(directory+L"\\theme-blue.png");SetAccent(0xD898BC);SaveRender(directory+L"\\theme-pink.png");SetAccent(0xCBB06B);renderMode=true;
        if(!wallpaperReveal.VerifyGeometry())winrt::throw_hresult(E_UNEXPECTED);
        Key(VK_ESCAPE); if(expanded||width!=theme::DockWidth)winrt::throw_hresult(E_UNEXPECTED);Draw();
        KillTimer(window,StatsTimer);KillTimer(window,MediaTimer);KillTimer(window,FocusTimer);KillTimer(window,ConnectivityTimer);
    }
};

// Standard MSAA children let Windows' UI Automation bridge expose the custom-drawn
// controls as named, focusable buttons and a slider instead of an inaccessible canvas.
class IslandAccessible final : public IAccessible {
    std::atomic<ULONG> references_{1};
    HWND window_;
    Island* App() const {
        wchar_t className[64]{};
        if (!IsWindow(window_) || !GetClassNameW(window_,className,64) || wcscmp(className,WindowClass)) return nullptr;
        return reinterpret_cast<Island*>(GetWindowLongPtrW(window_,GWLP_USERDATA));
    }
    int Child(VARIANT value,Island* app) const {
        return app && value.vt==VT_I4 && value.lVal>0 && value.lVal<=static_cast<LONG>(app->hits.size()) ? value.lVal-1 : -1;
    }
    HRESULT String(BSTR* output,const std::wstring& value) const {
        if (!output) return E_POINTER; *output=SysAllocStringLen(value.data(),static_cast<UINT>(value.size()));
        return *output?S_OK:E_OUTOFMEMORY;
    }
public:
    explicit IslandAccessible(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** output) override {
        if (!output) return E_POINTER;
        if (id==IID_IUnknown || id==IID_IDispatch || id==IID_IAccessible) { *output=static_cast<IAccessible*>(this); AddRef(); return S_OK; }
        *output=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (!count) return E_POINTER; *count=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID,REFIID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override {
        if (!parent) return E_POINTER; *parent=nullptr;
        return CreateStdAccessibleObject(window_,OBJID_WINDOW,IID_IDispatch,reinterpret_cast<void**>(parent));
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG* count) override {
        if (!count) return E_POINTER; auto* app=App(); *count=app?static_cast<LONG>(app->hits.size()):0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT,IDispatch** child) override { if (!child) return E_POINTER; *child=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child,BSTR* name) override {
        auto* app=App(); int index=Child(child,app);
        if (index>=0) return String(name,app->hits[index].name);
        if (child.vt==VT_I4 && child.lVal==CHILDID_SELF) return String(name,L"Dako desktop island");
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child,BSTR* value) override {
        if (!value) return E_POINTER; *value=nullptr;
        auto* app=App(); int index=Child(child,app);
        if (index>=0 && app->hits[index].id==Volume) return String(value,Number(app->system.volume*100)+L" percent");
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child,BSTR* description) override {
        auto* app=App(); int index=Child(child,app);
        if (index>=0 && app->hits[index].id==Volume) return String(description,L"System volume. Left and right arrows adjust by two percent. Space mutes.");
        return String(description,L"Use Tab to move between controls, Enter or Space to activate, and Escape to collapse.");
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child,VARIANT* role) override {
        if (!role) return E_POINTER; VariantInit(role); role->vt=VT_I4;
        auto* app=App(); int index=Child(child,app);
        int id=index>=0?app->hits[index].id:0;
        role->lVal=index>=0 ? (id==Volume?ROLE_SYSTEM_SLIDER:id>=200 && id<205?ROLE_SYSTEM_CHECKBUTTON:ROLE_SYSTEM_PUSHBUTTON) : ROLE_SYSTEM_PANE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child,VARIANT* state) override {
        if (!state) return E_POINTER; VariantInit(state); state->vt=VT_I4; state->lVal=0;
        auto* app=App(); int index=Child(child,app);
        if (index>=0) {
            state->lVal=STATE_SYSTEM_FOCUSABLE;
            if (!app->hits[index].enabled) state->lVal|=STATE_SYSTEM_UNAVAILABLE;
            if (app->focused==index && GetFocus()==window_) state->lVal|=STATE_SYSTEM_FOCUSED;
            int id=app->hits[index].id;
            if (id>=100 && id<107 && id-100==static_cast<int>(app->tab)) state->lVal|=STATE_SYSTEM_PRESSED;
            if (id>=200 && id<205 && id-200<static_cast<int>(app->tasks.size()) && app->tasks[id-200].done) state->lVal|=STATE_SYSTEM_CHECKED;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT,BSTR* help) override { if (!help) return E_POINTER; *help=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* file,VARIANT,LONG* topic) override {
        if (file) *file=nullptr; if (topic) *topic=-1; return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child,BSTR* shortcut) override {
        if (!shortcut) return E_POINTER; *shortcut=nullptr;
        if (child.vt==VT_I4 && child.lVal==CHILDID_SELF) return String(shortcut,L"Ctrl+Alt+Space"); return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override {
        if (!focus) return E_POINTER; VariantInit(focus); auto* app=App();
        if (GetFocus()==window_) { focus->vt=VT_I4; focus->lVal=app && app->focused>=0?app->focused+1:CHILDID_SELF; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection) override { if (!selection) return E_POINTER; VariantInit(selection); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child,BSTR* action) override {
        if (!action) return E_POINTER; *action=nullptr; auto* app=App(); int index=Child(child,app);
        return index>=0?String(action,app->hits[index].id==Volume?L"Adjust":L"Press"):S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags,VARIANT child) override {
        auto* app=App(); int index=Child(child,app);
        if (index<0 || !(flags&SELFLAG_TAKEFOCUS)) return E_INVALIDARG;
        PostMessageW(window_,FocusControl,index,0); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(LONG* left,LONG* top,LONG* width,LONG* height,VARIANT child) override {
        if (!left || !top || !width || !height) return E_POINTER;
        auto* app=App(); int index=Child(child,app); RECT bounds{}; GetWindowRect(window_,&bounds);
        if (index>=0) {
            const auto& r=app->hits[index].rect;
            float scale=app->dpi/96.0f;
            float originX=app->expanded?app->PanelX():app->renderMode?0:(theme::EnvelopeWidth-theme::DockWidth)/2;
            float originY=app->expanded?app->PanelY():app->HandleY();
            *left=bounds.left+static_cast<LONG>((originX+r.left)*scale); *top=bounds.top+static_cast<LONG>((originY+r.top)*scale);
            *width=static_cast<LONG>((r.right-r.left)*scale); *height=static_cast<LONG>((r.bottom-r.top)*scale);
        } else { *left=bounds.left; *top=bounds.top; *width=bounds.right-bounds.left; *height=bounds.bottom-bounds.top; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction,VARIANT start,VARIANT* end) override {
        if (!end) return E_POINTER; VariantInit(end); auto* app=App(); if (!app || app->hits.empty()) return S_FALSE;
        int index=Child(start,app), destination=-1;
        if (direction==NAVDIR_FIRSTCHILD) destination=0;
        else if (direction==NAVDIR_LASTCHILD) destination=static_cast<int>(app->hits.size())-1;
        else if (direction==NAVDIR_NEXT && index>=0 && index+1<static_cast<int>(app->hits.size())) destination=index+1;
        else if (direction==NAVDIR_PREVIOUS && index>0) destination=index-1;
        if (destination<0) return S_FALSE; end->vt=VT_I4; end->lVal=destination+1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x,LONG y,VARIANT* child) override {
        if (!child) return E_POINTER; VariantInit(child); auto* app=App(); if (!app) return S_FALSE;
        POINT point{x,y}; ScreenToClient(window_,&point);
        int index=app->HitAt(point.x*96.0f/app->dpi,point.y*96.0f/app->dpi);
        child->vt=VT_I4; child->lVal=index>=0?index+1:CHILDID_SELF; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto* app=App(); int index=Child(child,app);
        if (index<0 || !app->hits[index].enabled) return E_INVALIDARG;
        PostMessageW(window_,InvokeControl,app->hits[index].id,0); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT,BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT,BSTR) override { return E_NOTIMPL; }
};

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app=reinterpret_cast<Island*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lparam); app=static_cast<Island*>(create->lpCreateParams);
        app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window,message,wparam,lparam);
    try {
        if(message==app->shell.taskbarCreated&&!app->renderMode){app->shell.ExplorerRestarted();return 0;}
        switch(message) {
        case WM_CREATE: app->Initialize(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; BeginPaint(window,&paint);
            if(!app->renderMode&&app->dockReveal.value==0){
                app->PaintNotch(paint.hdc);
                app->hits.clear();app->AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");EndPaint(window,&paint);return 0;
            }
            try { app->Draw(); } catch (...) { EndPaint(window,&paint); throw; }
            EndPaint(window,&paint); return 0;
        }
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam)==OBJID_CLIENT) {
                if (!app->accessible) app->accessible.Attach(new IslandAccessible(window));
                return LresultFromObject(IID_IAccessible,wparam,app->accessible.Get());
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (app->hostTarget) app->hostTarget->Resize(D2D1::SizeU(LOWORD(lparam),HIWORD(lparam)));
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_DPICHANGED:
            app->dpi=static_cast<float>(HIWORD(wparam)); app->ReleaseGraphics();
            if (app->taskEdit) { DestroyWindow(app->taskEdit); app->taskEdit=nullptr; }
            if (app->appSearch) { DestroyWindow(app->appSearch); app->appSearch=nullptr; }
            if(app->wifiPassword){DestroyWindow(app->wifiPassword);app->wifiPassword=nullptr;}
            if (app->editFont) { DeleteObject(app->editFont); app->editFont=nullptr; }
            app->Place(); return 0;
        case WM_DISPLAYCHANGE: app->Place(); return 0;
        case WM_SETTINGCHANGE: { BOOL enabled=TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0); app->reducedMotion=!enabled; return 0; }
        case WM_TIMER:
            if(wparam==desktop::ShellTimer)app->shell.Reconcile();
            else if (wparam==TransitionTimer) app->Transition();
            else if (wparam==StatsTimer) { app->SampleStats(); InvalidateRect(window,nullptr,FALSE); }
            else if (wparam==MediaTimer) { app->currentMedia.position=std::min(app->currentMedia.position+1,app->currentMedia.duration); InvalidateRect(window,nullptr,FALSE); }
            else if(wparam==ProximityTimer)app->ProximityTick();
            else if(wparam==FocusTimer){if(app->focusRunning){ULONGLONG now=GetTickCount64();app->focusSeconds=static_cast<int>((app->focusDeadline>now?app->focusDeadline-now:0)+999)/1000;
                if(!app->focusSeconds){app->focusRunning=false;app->note=L"Focus session complete.";app->UpdateTimers();}}
                if(app->expanded&&app->tab==Tab::Today)InvalidateRect(window,nullptr,FALSE);}
            else if(wparam==ConnectivityTimer&&app->connectivity){if(app->flyout==Flyout::Wifi)app->connectivity->RefreshWifi();else if(app->flyout==Flyout::Bluetooth)app->connectivity->RefreshBluetooth();}
            return 0;
        case desktop::ShellChanged:app->shell.WindowEvent(static_cast<DWORD>(wparam),reinterpret_cast<HWND>(lparam));return 0;
        case desktop::RailChanged:app->shell.PositionRail();return 0;
        case desktop::TrayMessage:
            if(LOWORD(lparam)==WM_CONTEXTMENU||LOWORD(lparam)==WM_RBUTTONUP)app->ContextMenu();
            else if(LOWORD(lparam)==NIN_SELECT||LOWORD(lparam)==NIN_KEYSELECT){app->SetExpanded(!app->expanded);app->keyboardOpen=app->expanded;SetForegroundWindow(window);}
            return 0;
        case ConnectivityChanged:app->RefreshConnectionsView();return 0;
        case RevealDock:
            if(wparam==1)app->SetTab(Tab::Stats);else app->SetExpanded(true);
            app->launchPreview=true;app->keyboardOpen=true;SetForegroundWindow(window);SetFocus(window);return 0;
        case StateChanged:
            app->system.RefreshVolume(); if (app->media) app->currentMedia=app->media->Snapshot();
            app->UpdateTimers();if(app->expanded)InvalidateRect(window,nullptr,FALSE);return 0;
        case AudioDeviceChanged:
            app->system.BindAudio(window); InvalidateRect(window,nullptr,FALSE); return 0;
        case CatalogChanged:
            if (app->catalog) {
                std::wstring error; app->catalog->Snapshot(app->apps,app->wallpapers,error);
                app->selectedWallpaper=std::min(app->selectedWallpaper,app->wallpapers.empty()?size_t{0}:app->wallpapers.size()-1);
                app->thumbnails.clear(); app->FilterApps(); app->note=std::move(error);
                NotifyWinEvent(EVENT_OBJECT_REORDER,window,OBJID_CLIENT,CHILDID_SELF);
                InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        case ThemeChanged:app->SetAccent(static_cast<unsigned>(wparam));return 0;
        case HardwareChanged:if(app->hardware)app->hardwareState=app->hardware->Snapshot();if(app->expanded&&app->tab==Tab::Stats)InvalidateRect(window,nullptr,FALSE);return 0;
        case WallpaperApplied:app->wallpaperApplying=false;if(wparam)app->wallpaperReveal.Begin();else{app->wallpaperReveal.Cancel();app->note=L"This wallpaper could not be applied.";}InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lparam)==app->appSearch && HIWORD(wparam)==EN_CHANGE) {
                int length=GetWindowTextLengthW(app->appSearch);
                std::wstring query(static_cast<size_t>(length)+1,L'\0');
                GetWindowTextW(app->appSearch,query.data(),length+1); query.resize(length);
                app->appQuery=std::move(query); app->FilterApps(); InvalidateRect(window,nullptr,FALSE);
            }
            if(reinterpret_cast<HWND>(lparam)==app->wifiPassword&&HIWORD(wparam)==EN_CHANGE)InvalidateRect(window,nullptr,FALSE);
            return 0;
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wparam),RGB(237,236,231));
            SetBkColor(reinterpret_cast<HDC>(wparam),RGB(32,32,32));
            app->EnsureEditResources(); return reinterpret_cast<LRESULT>(app->editBackground);
        case InvokeControl: app->Invoke(static_cast<int>(wparam)); return 0;
        case FocusControl:
            app->focused=static_cast<int>(wparam); SetFocus(window);
            NotifyWinEvent(EVENT_OBJECT_FOCUS,window,OBJID_CLIENT,app->focused+1);
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_MOUSEMOVE: {
            if (app->draggingVolume) { app->SetVolumeAt(GET_X_LPARAM(lparam)); return 0; }
            app->keyboardOpen=false;app->launchPreview=false;
            if (!app->tracking) { TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track); app->tracking=true; }
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            app->SetHover(hit);
            return 0;
        }
        case WM_MOUSELEAVE: app->tracking=false; app->SetHover(-1);return 0;
        case WM_MOUSEACTIVATE:return app->expanded?MA_ACTIVATE:MA_NOACTIVATE;
        case WM_NCHITTEST:{POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ScreenToClient(window,&point);
            float y=point.y*96.0f/app->dpi;if(!app->renderMode&&!((y>=0&&y<=theme::DockHeight)||(app->dockReveal.value>0.003f&&y>=app->PanelY()&&y<=app->PanelY()+app->height)))return HTTRANSPARENT;break;}
        case WM_ACTIVATE:
            if(LOWORD(wparam)==WA_INACTIVE&&app->expanded&&!app->modalOpen){HWND next=reinterpret_cast<HWND>(lparam);if(!next||GetWindow(next,GW_OWNER)!=window)app->SetExpanded(false);}return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            app->focused=hit;
            if (hit>=0 && app->hits[hit].id==Volume) { app->draggingVolume=true; SetCapture(window); app->SetVolumeAt(GET_X_LPARAM(lparam)); }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (app->draggingVolume) { app->draggingVolume=false; ReleaseCapture(); return 0; }
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            if (hit>=0 && hit==app->focused) app->Invoke(app->hits[hit].id);
            return 0;
        }
        case WM_CAPTURECHANGED: app->draggingVolume=false; return 0;
        case WM_MOUSEWHEEL:
            if (GET_WHEEL_DELTA_WPARAM(wparam)<0 && !app->expanded) app->SetExpanded(true);
            else if (GET_WHEEL_DELTA_WPARAM(wparam)>0 && app->expanded) {
                POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)}; ScreenToClient(window,&point);
                if (point.y<(app->PanelY()+48)*app->dpi/96) app->SetExpanded(false);
            }
            return 0;
        case WM_KEYDOWN: app->Key(wparam); return 0;
        case WM_HOTKEY: if (wparam==1){app->SetExpanded(!app->expanded);if(app->expanded){app->keyboardOpen=true;SetForegroundWindow(window);SetFocus(window);}}else if(wparam==2)DestroyWindow(window);else if(wparam==4){if(app->shell.Active())app->shell.Stop();else app->shell.Start();}return 0;
        case WM_CONTEXTMENU: app->ContextMenu(); return 0;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,app->hovered>=0?IDC_HAND:IDC_ARROW)); return TRUE;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            app->wallpaperReveal.Cancel();
            app->shell.Stop();app->shell.RemoveTray();
            KillTimer(window,TransitionTimer); KillTimer(window,StatsTimer); KillTimer(window,MediaTimer);
            KillTimer(window,ProximityTimer);KillTimer(window,FocusTimer);KillTimer(window,ConnectivityTimer);
            if (app->toggleHotkey) UnregisterHotKey(window,1); if (app->quitHotkey) UnregisterHotKey(window,2);
            if(app->taskbarHotkey)UnregisterHotKey(window,4);
            PostQuitMessage(0); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); break;
        }
    } catch (const winrt::hresult_error& error) {
        OutputDebugStringW(error.message().c_str());
        if (message==WM_CREATE) return -1;
        if (message==WM_PAINT) { ValidateRect(window,nullptr); app->ReleaseGraphics(); }
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(!wcscmp(command,L"--effects-test")){
        WallpaperReveal reveal;if(!reveal.VerifyGeometry()||!reveal.Freeze(false,false))return 7;reveal.Begin();ULONGLONG deadline=GetTickCount64()+2000;
        while(reveal.Running()&&GetTickCount64()<deadline){MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
        return reveal.Running()?8:0;
    }
    if(wcsncmp(command,L"--shell-guard ",14)==0)return desktop::Guard(command+14);
    if(!wcscmp(command,L"--exit")){
        HWND existing=FindWindowW(WindowClass,nullptr);if(!existing)return 0;DWORD processId=0;GetWindowThreadProcessId(existing,&processId);
        HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,processId);PostMessageW(existing,WM_CLOSE,0,0);
        DWORD wait=process?WaitForSingleObject(process,15000):WAIT_FAILED;if(process)CloseHandle(process);return wait==WAIT_OBJECT_0?0:6;
    }
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return 1;
    if(!wcscmp(command,L"--runtime-check")){
        WNDCLASSW type{};type.hInstance=instance;type.lpfnWndProc=DefWindowProcW;type.lpszClassName=L"Dako.Runtime.Check";RegisterClassW(&type);HWND target=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Dako runtime check",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
        if(!target){CoUninitialize();return 9;}unsigned accent=0;HardwareSnapshot snapshot;int samples=0;bool second=false;ULONGLONG start=GetTickCount64();
        {DesktopCatalog catalog(target);HardwareStats hardware(target);hardware.Request();
            while(GetTickCount64()-start<8000&&(samples<2||accent==0)){
                if(samples>0&&!second&&GetTickCount64()-start>1200){second=true;hardware.Request();}
                MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==ThemeChanged)accent=static_cast<unsigned>(message.wParam);if(message.message==HardwareChanged){snapshot=hardware.Snapshot();++samples;}TranslateMessage(&message);DispatchMessageW(&message);}
            }
        }
        DestroyWindow(target);wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::ofstream report(std::filesystem::path(executable).parent_path()/L"runtime-check.txt",std::ios::trunc);
        report<<"hardware_samples="<<samples<<"\nphysical_memory_gib="<<snapshot.totalGiB<<"\ntop_process_count="<<snapshot.top.size()<<"\ngpu_counter_available="<<snapshot.gpuValid<<"\nwallpaper_accent_rgb="<<accent<<"\n";report.close();CoUninitialize();return samples>=2&&snapshot.totalGiB>0&&accent!=0?0:9;
    }
    if(wcsstr(command,L"--restore-original-wallpaper")) {
        const auto original=DesktopCatalog::PreservePersonalWallpaper();
        const HRESULT restored=DesktopCatalog::ApplyWallpaper(original);
        const auto current=DesktopCatalog::CurrentWallpaperPath();
        const bool verified=SUCCEEDED(restored) && CompareStringOrdinal(current.c_str(),-1,original.c_str(),-1,TRUE)==CSTR_EQUAL;
        CoUninitialize();return verified?0:3;
    }
    int result=0;
    {
        Island app;
        std::wstring renderDirectory;
        int argCount=0; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argCount);
        if (args) {
            for (int i=1;i<argCount;++i) {
                const std::wstring arg=args[i];
                if (arg.starts_with(L"--render-dir=")) renderDirectory=arg.substr(13);
            }
            LocalFree(args);
        }
        app.renderMode=!renderDirectory.empty();
        HANDLE singleton=nullptr;
        if (!app.renderMode) {
            singleton=CreateMutexW(nullptr,FALSE,L"Local\\Dako.Native.Island.Singleton");
            if (singleton && GetLastError()==ERROR_ALREADY_EXISTS) {
                HWND existing=FindWindowW(WindowClass,nullptr);if(existing){DWORD processId=0;GetWindowThreadProcessId(existing,&processId);AllowSetForegroundWindow(processId);PostMessageW(existing,RevealDock,wcsstr(command,L"--tab=stats")?1:0,0);}
                CloseHandle(singleton);CoUninitialize();return 0;
            }
        }
        WNDCLASSW windowClass{}; windowClass.hInstance=instance; windowClass.lpfnWndProc=WindowProcedure;
        windowClass.lpszClassName=WindowClass; windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&windowClass);
        bool inspectMode=wcsstr(command,L"--inspect")!=nullptr;
        HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,WindowClass,L"Dako — Desktop Dock",WS_POPUP|WS_CLIPCHILDREN,
            0,0,300,100,nullptr,nullptr,instance,&app);
        if (!window) { MessageBoxW(nullptr,L"Dako could not create its native window.",L"Dako",MB_OK|MB_ICONERROR); result=1; }
        else if (app.renderMode) {
            try {
                app.RenderSuite(renderDirectory);
                app.accessible.Attach(new IslandAccessible(window));
                LONG children=0; Check(app.accessible->get_accChildCount(&children));
                VARIANT child{}; child.vt=VT_I4; child.lVal=1; BSTR name=nullptr;
                Check(app.accessible->get_accName(child,&name));
                bool valid=children==1 && name && std::wstring(name)==L"Reveal dock";
                SysFreeString(name); if (!valid) winrt::throw_hresult(E_UNEXPECTED);
            }
            catch (const winrt::hresult_error& error) { OutputDebugStringW(error.message().c_str()); result=2; }
            DestroyWindow(window);
        } else {
            app.shell.Initialize(window);
            if(!app.shell.Start())MessageBoxW(window,L"Dako could not start taskbar recovery. The Windows taskbar remains available.",L"Dako",MB_OK|MB_ICONERROR);
            if (wcsstr(command,L"--tab=stats")){app.SetTab(Tab::Stats);app.launchPreview=true;}
            else if (wcsstr(command,L"--expanded")||inspectMode){app.SetExpanded(true);app.launchPreview=true;}
            ShowWindow(window,SW_SHOWNOACTIVATE); UpdateWindow(window);
            MSG message{}; BOOL status=0;
            while ((status=GetMessageW(&message,nullptr,0,0))>0) {
                if ((message.hwnd==app.taskEdit || message.hwnd==app.appSearch||message.hwnd==app.wifiPassword) && message.message==WM_KEYDOWN) {
                    if (message.wParam==VK_RETURN) { app.Invoke(message.hwnd==app.taskEdit?AddTask:message.hwnd==app.appSearch?300:FlyoutConnect); continue; }
                    if (message.wParam==VK_ESCAPE) { SetFocus(window); app.Key(VK_ESCAPE); continue; }
                    if (message.wParam==VK_TAB) { SetFocus(window); app.Key(VK_TAB); continue; }
                }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            result=status<0?1:static_cast<int>(message.wParam);
        }
        if (singleton) CloseHandle(singleton);
    }
    CoUninitialize(); return result;
}
