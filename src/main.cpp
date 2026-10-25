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
#include "LayoutEngine.h"
#include "MotionController.h"
#include "CompositionSurface.h"
#include "FrameClock.h"
#include "AsyncWait.h"
#include <map>
#include <cstdint>
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
constexpr UINT MotionFrame=WM_APP+31;
constexpr UINT_PTR TransitionTimer = 1, StatsTimer = 2, MediaTimer = 3, ProximityTimer=4, FocusTimer=5, ConnectivityTimer=6;
constexpr wchar_t WindowClass[] = L"Dako.Native.Island";

void Check(HRESULT result) { if (FAILED(result)) winrt::throw_hresult(result); }
std::wstring Number(double value, int digits = 0) {
    wchar_t buffer[64];swprintf_s(buffer,L"%.*f",digits,value);
    wchar_t decimal[8]{};if(GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_SDECIMAL,decimal,8)&&decimal[0]!=L'.'){
        std::wstring result=buffer;auto at=result.find(L'.');if(at!=std::wstring::npos)result.replace(at,1,decimal);return result;
    }return buffer;
}
std::wstring Wide(const char* value, int length) {
    int size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    std::wstring result(size, L' ');
    if (size) MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), size);
    return result;
}

struct MediaState {
    bool available = false, playing = false,canPrevious=false,canPlay=false,canNext=false;
    std::wstring transportError;
    std::wstring title = L"Nothing playing", artist = L"Play music in a supported app";
    double position = 0, duration = 0;
    steady_clock::time_point sampled=steady_clock::now();
};

// All asynchronous media work lives in one MTA, never on the window thread.
class MediaService {
    struct CallbackGate{std::mutex mutex;MediaService* owner;explicit CallbackGate(MediaService* value):owner(value){}};
    std::shared_ptr<CallbackGate> callbacks_=std::make_shared<CallbackGate>(this);
    HWND window_;
    std::mutex mutex_, stateMutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    MediaState state_;
    GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    GlobalSystemMediaTransportControlsSession session_{nullptr};
    winrt::event_token currentToken_{}, propertiesToken_{}, playbackToken_{}, timelineToken_{};
    std::atomic<bool> cancel_{false};
    template<class Async> auto Await(Async operation){return AwaitNative(std::move(operation),[this]{return cancel_.load();});}
    std::atomic<bool> visible_{true},refreshPending_{false},propertiesPending_{false};
    std::thread worker_;

    void Queue(std::function<void()> job) {
        { std::scoped_lock lock(mutex_); if (stopping_) return; jobs_.push_back(std::move(job)); }
        ready_.notify_one();
    }
    void RequestRefresh(bool properties){
        if(properties)propertiesPending_=true;if(refreshPending_.exchange(true))return;
        Queue([this]{bool read=propertiesPending_.exchange(false);refreshPending_=false;Refresh(read);});
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
                if(readProperties){auto metadata=Await(session_.TryGetMediaPropertiesAsync());
                    next.title=metadata.Title().empty()?L"Untitled media":std::wstring(metadata.Title());
                    next.artist=metadata.Artist().empty()?std::wstring(session_.SourceAppUserModelId()):std::wstring(metadata.Artist());}
                auto playback = session_.GetPlaybackInfo();
                auto timeline = session_.GetTimelineProperties();
                next.available = true;
                next.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                auto controls=playback.Controls();next.canPrevious=controls.IsPreviousEnabled();next.canNext=controls.IsNextEnabled();next.canPlay=controls.IsPlayPauseToggleEnabled()||(next.playing?controls.IsPauseEnabled():controls.IsPlayEnabled());
                next.position = duration<double>(timeline.Position()-timeline.StartTime()).count();next.sampled=steady_clock::now();
                next.duration = duration<double>(timeline.EndTime() - timeline.StartTime()).count();
            }else next=MediaState{};
        } catch (const winrt::hresult_error&) { next.available=next.playing=next.canPrevious=next.canPlay=next.canNext=false;next.artist = L"Media session unavailable"; }
        { std::scoped_lock lock(stateMutex_); state_ = std::move(next); }
        PostMessageW(window_, StateChanged, 0, 0);
    }
    void AttachSession() {
        DetachSession();
        session_ = manager_.GetCurrentSession();
        if (session_) {
            auto shared=callbacks_;
            propertiesToken_ = session_.MediaPropertiesChanged([shared](auto&&, auto&&) {std::scoped_lock lock(shared->mutex);if(auto* owner=shared->owner;owner&&owner->visible_)owner->RequestRefresh(true);});
            playbackToken_ = session_.PlaybackInfoChanged([shared](auto&&, auto&&) {std::scoped_lock lock(shared->mutex);if(auto* owner=shared->owner;owner&&owner->visible_)owner->RequestRefresh(false);});
            timelineToken_ = session_.TimelinePropertiesChanged([shared](auto&&, auto&&) {std::scoped_lock lock(shared->mutex);if(auto* owner=shared->owner;owner&&owner->visible_)owner->RequestRefresh(false);});
        }
        if(visible_)Refresh();
    }
    void Run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            manager_ = Await(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            auto shared=callbacks_;currentToken_ = manager_.CurrentSessionChanged([shared](auto&&, auto&&) {std::scoped_lock lock(shared->mutex);if(auto* owner=shared->owner)owner->Queue([owner] {owner->AttachSession();});});
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
        try{DetachSession();if(manager_)manager_.CurrentSessionChanged(currentToken_);}catch(const winrt::hresult_error&){}
        manager_ = nullptr;
        winrt::uninit_apartment();
    }
public:
    explicit MediaService(HWND window) : window_(window), worker_([this] { Run(); }) {}
    ~MediaService() {
        {std::scoped_lock lock(callbacks_->mutex);callbacks_->owner=nullptr;}
        cancel_=true;
        { std::scoped_lock lock(mutex_); stopping_ = true; }
        ready_.notify_one(); worker_.join();
    }
    MediaState Snapshot() { std::scoped_lock lock(stateMutex_);auto result=state_;if(result.playing){result.position+=duration<double>(steady_clock::now()-result.sampled).count();if(result.duration>0)result.position=std::min(result.position,result.duration);}return result; }
    void Rebase(){if(visible_)RequestRefresh(true);}
    void SetVisible(bool value){bool previous=visible_.exchange(value);if(value&&!previous)Queue([this]{Refresh();});}
    void Transport(int action) {
        Queue([this, action] {
            try{if (!session_) return;
            auto playback=session_.GetPlaybackInfo();auto controls=playback.Controls();
            bool succeeded=false;
            if(action==0&&controls.IsPreviousEnabled())succeeded=Await(session_.TrySkipPreviousAsync());
            else if(action==2&&controls.IsNextEnabled())succeeded=Await(session_.TrySkipNextAsync());
            else if(action==1){if(controls.IsPlayPauseToggleEnabled())succeeded=Await(session_.TryTogglePlayPauseAsync());else if(playback.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing&&controls.IsPauseEnabled())succeeded=Await(session_.TryPauseAsync());else if(controls.IsPlayEnabled())succeeded=Await(session_.TryPlayAsync());}
            Refresh();if(!succeeded){{std::scoped_lock lock(stateMutex_);state_.transportError=L"The media app declined this action.";}PostMessageW(window_,StateChanged,0,0);}
            }catch(const winrt::hresult_error&){if(!cancel_){{std::scoped_lock lock(stateMutex_);state_.transportError=L"Windows could not complete this media action. Try again or use the media app.";}PostMessageW(window_,StateChanged,0,0);}}
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
    bool muted = false,wifiConnected=false;
    std::wstring wifi = L"Not connected", bluetooth = L"Not available";
    std::wstring audioName=L"No audio device",diskRoot;
    bool memoryValid=false,cpuValid=false,diskValid=false,networkValid=false;
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
        wifi = L"Not connected";wifiConnected=false;
        HANDLE handle = nullptr; DWORD negotiated = 0;
        if (WlanOpenHandle(2, nullptr, &negotiated, &handle) == ERROR_SUCCESS) {
            PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
            if (WlanEnumInterfaces(handle, nullptr, &interfaces) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
                    auto& item = interfaces->InterfaceInfo[i];
                    if (item.isState != wlan_interface_state_connected) continue;wifiConnected=true;
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
        memoryValid=cpuValid=diskValid=networkValid=false;
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory)) { memoryValid=true;memoryLoad = memory.dwMemoryLoad; availableGiB = memory.ullAvailPhys / 1073741824.0; }
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
            cpuValid=systemKernel&&total;if(cpuValid)totalCpu=std::clamp(100.0*(1.0-static_cast<double>(i-systemIdle)/total),0.0,100.0);
            systemIdle=i;systemKernel=k;systemUser=u;
        }
        wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);std::wstring root=std::wstring(windows).substr(0,3);diskRoot=root;
        ULARGE_INTEGER freeBytes{},totalBytes{},totalFree{};
        if(GetDiskFreeSpaceExW(root.c_str(),&freeBytes,&totalBytes,&totalFree)&&totalBytes.QuadPart){diskValid=true;diskTotalGiB=totalBytes.QuadPart/1073741824.0;diskUsedGiB=(totalBytes.QuadPart-totalFree.QuadPart)/1073741824.0;diskPercent=100*diskUsedGiB/diskTotalGiB;}
        PMIB_IF_TABLE2 interfaces=nullptr;
        if(GetIfTable2(&interfaces)==NO_ERROR){ULONGLONG received=0,sent=0;
            for(ULONG i=0;i<interfaces->NumEntries;++i){const auto& row=interfaces->Table[i];if(row.OperStatus==IfOperStatusUp&&row.InterfaceAndOperStatusFlags.HardwareInterface){received+=row.InOctets;sent+=row.OutOctets;}}
            ULONGLONG now=GetTickCount64();if(networkTime&&now>networkTime&&received>=receivedBytes&&sent>=sentBytes){networkValid=true;receiveRate=(received-receivedBytes)*1000.0/(now-networkTime);sendRate=(sent-sentBytes)*1000.0/(now-networkTime);}
            receivedBytes=received;sentBytes=sent;networkTime=now;FreeMibTable(interfaces);
        }
    }
};

#include "DesktopCatalog.h"
#include "WallpaperReveal.h"
#include "HardwareStats.h"
#include "Connectivity.h"

namespace type {enum Role {Caption,Secondary,Body,Section,Metric,Display,Icon,Control,Editorial,Mono};}

enum class Tab { Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power };
enum class Flyout { None, Wifi, Bluetooth };
enum Action { Expand = 1, Wifi = 20, Bluetooth, Audio, Night, Dark, Volume, Mute, Previous, Play, Next,
    AddTask = 60, ClipboardRefresh, ClipboardClear, WallpaperSettings, PowerSettings, Lock, Quit, Minimize,
    AppsPrevious, AppsNext, WallpaperFolder, WallpaperPrevious, WallpaperNext, WallpaperApply, WallpaperCollection,
    SleepNow = 80, RestartNow, ShutdownNow,
    FlyoutBack=600,FlyoutRefresh,FlyoutToggle,FlyoutSettings,FlyoutConnect,FlyoutDisconnect,FlyoutPair,FlyoutRemove,FlyoutPrevious,FlyoutNext,
    FocusStart=700,FocusReset,FocusShort,FocusMedium,FocusLong,FocusOne,FocusTen,FocusTwenty };
struct Hit {
    int id; D2D1_RECT_F rect; std::wstring name; bool enabled=true;
    std::wstring key; LONG child=0; float skew=0,radius=10; D2D1_RECT_F clip{}; HWND input=nullptr;
};
struct Task { std::wstring title; bool done = false; unsigned identity=0; };
struct ModalGuard{bool& flag;explicit ModalGuard(bool& value):flag(value){flag=true;}~ModalGuard(){flag=false;}};

class Island {
public:
    HWND window = nullptr;
    bool fixtureAudioAvailable=true;
    bool fixtureGeometry=false,previewMode=false,layoutOnly=false,buildingBody=false,motionOverride=false,opaqueMaterial=true,highContrast=false,lightAppearance=true,systemAppearance=false;
    bool backdropAvailable=false,systemReduced=false;HRESULT backdropResult=E_PENDING;DWORD systemTransparency=1;
    ComPtr<ID2D1RoundedRectangleGeometry> wallpaperMask;
    ComPtr<ID2D1StrokeStyle> ringStroke;
    HWND backdropWindow=nullptr;
    RECT backdropPlacement{};
    HMONITOR placementMonitor=nullptr;
    std::array<int,9> regionSignature{-1,-1,-1,-1,-1,-1,-1,-1,-1};
    float envelopeWidth=theme::EnvelopeWidth,scroll=0,pointerX=-10000,textScale=1;
    dock::Scheduler scheduler;
    FrameClock frameClock;bool frameTickReady=false;
    UINT motionInterval=16;
    dock::Spring wallpaperParallax{0},volumeMotion{0};
    std::wstring diagnosticsPath;std::vector<double> frameIntervals,paintTimes;std::function<void()> beforePresent;
    dock::Spring tabLeft{20},tabRight{100},railPointer{-10000},railStrength{0},paletteBlend{1},contentBlend{1},togglePosition{0};
    std::array<dock::Spring,4> rings{dock::Spring(0),dock::Spring(0),dock::Spring(0),dock::Spring(0)};
    std::map<std::wstring,dock::Feedback> feedback;
    std::map<std::wstring,dock::Spring> rowPositions;
    std::wstring hoveredKey,focusedKey,pressedKey,pendingWallpaper,appliedWallpaper,taskDraft,lastStatus;
    std::map<std::wstring,LONG> childIds;
    LONG nextChild=1;unsigned nextTask=1;
    int pressedId=0;DWORD draftStart=0,draftEnd=0;
    std::vector<layout::AppPage> appPages;
    std::vector<size_t> railApps;
    std::wstring clipboardState=L"Not read";
    size_t clipboardOffset=0;
    unsigned previousAccent=theme::Signal,targetAccent=theme::Signal;
    std::wstring wifiIdentity,bluetoothIdentity;
    float bodyDirection=1;
    layout::Result Layout()const{
        layout::View v=miniMode?layout::View::Media:flyout!=Flyout::None?layout::View::Wireless:static_cast<layout::View>(tab);
        return layout::Solve({std::max(100.0f,(envelopeWidth-40)/textScale),(envelopeHeight-PanelY()-12)/textScale,v,filteredApps.size()});
    }
    static D2D1_RECT_F R(layout::Rect r){return {r.left,r.top,r.right,r.bottom};}
    D2D1_RECT_F Body()const{return R(Layout().body);}
    D2D1_RECT_F Footer()const{return R(Layout().footer);}
    bool CanInvoke(const Hit& hit)const{return hit.enabled&&VisibleHit(hit)&&((hit.id==Expand&&!expanded&&!YieldResting())||Actionable());}
    // Input follows the presented surface; spring settlement must never gate clicks.
    bool Actionable()const{return expanded&&!capturingPane;}
    int IndexForKey(const std::wstring& key)const{for(size_t i=0;i<hits.size();++i)if(hits[i].key==key)return static_cast<int>(i);return -1;}
    int IndexForChild(LONG child)const{for(size_t i=0;i<hits.size();++i)if(hits[i].child==child)return static_cast<int>(i);return -1;}
    std::wstring StableKey(int id)const{
        if(id>=200&&id<205&&id-200<static_cast<int>(tasks.size()))return L"task:"+std::to_wstring(tasks[id-200].identity);
        if(id>=220&&id<225&&id-220<static_cast<int>(tasks.size()))return L"delete:"+std::to_wstring(tasks[id-220].identity);
        if(id>=300&&id<400&&AppIndex(id-300)<apps.size())return L"app:"+AppKey(apps[AppIndex(id-300)]);
        if(id>=900&&id<1000&&AppIndex(id-900)<apps.size())return L"star:"+AppKey(apps[AppIndex(id-900)]);
        if(id>=1400&&id<1410&&id-1400<static_cast<int>(railApps.size()))return L"rail:"+AppKey(apps[railApps[id-1400]]);
        if(id>=500&&id<564&&id-500<static_cast<int>(wallpapers.size()))return L"wallpaper:"+wallpapers[id-500].path;
        if(id>=1000&&id<1128&&id-1000<static_cast<int>(connections.networks.size()))return WifiKey(connections.networks[id-1000]);
        if(id>=1200&&id<1328&&id-1200<static_cast<int>(connections.devices.size()))return BluetoothKey(connections.devices[id-1200]);
        if(id>=2000&&id<2100)return L"reading:"+std::to_wstring(static_cast<int>(tab))+L":"+std::to_wstring(miniMode)+L":"+std::to_wstring(id);
        return L"action:"+std::to_wstring(id);
    }
    static std::wstring WifiKey(const WifiNetwork& n){wchar_t guid[40]{};StringFromGUID2(n.adapter,guid,40);return L"wifi:"+std::wstring(guid)+L":"+n.name+L":"+std::to_wstring(n.authentication)+L":"+std::to_wstring(n.cipher);}
    static std::wstring BluetoothKey(const BluetoothEntry& d){return L"bt:"+(d.identifier.empty()?std::to_wstring(d.native.Address.ullLong):d.identifier);}
    size_t AppIndex(size_t slot)const{return appPage<appPages.size()&&appPages[appPage].begin+slot<appPages[appPage].end?filteredApps[appPages[appPage].begin+slot]:apps.size();}
    bool VisibleHit(const Hit& h)const{auto r=h.rect,c=h.clip;return r.bottom>c.top&&r.top<c.bottom&&r.right>c.left&&r.left<c.right;}
    void RequestWallpaperDetail(){if(catalog&&!renderMode&&tab==Tab::Wallpaper&&selectedWallpaper<wallpapers.size())catalog->RequestWallpaperDetail(wallpapers[selectedWallpaper].path);}
    void ReplaceWallpapers(std::vector<WallpaperEntry> next){
        std::wstring selected=selectedWallpaper<wallpapers.size()?wallpapers[selectedWallpaper].path:L"";wallpapers=std::move(next);selectedWallpaper=0;for(size_t i=0;i<wallpapers.size();++i)if(wallpapers[i].path==selected)selectedWallpaper=i;
        wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));thumbnails.clear();
    }
    void RequestVisibleIcons(){
        if(!catalog||renderMode||tab!=Tab::Apps)return;std::vector<std::wstring> keys;for(size_t i:railApps)keys.push_back(AppKey(apps[i]));for(size_t slot=0;slot<AppSlots();++slot){size_t i=AppIndex(slot);if(i<apps.size())keys.push_back(AppKey(apps[i]));}catalog->RequestIcons(std::move(keys));
    }
    void RebuildLayout(){
        int previous=focused;bool hadFocus=!focusedKey.empty();bool saved=layoutOnly;layoutOnly=true;PanelContent();layoutOnly=saved;
        focused=IndexForKey(focusedKey);hovered=IndexForKey(hoveredKey);
        if(focused<0&&hadFocus&&!hits.empty()){int start=std::clamp(previous,0,static_cast<int>(hits.size())-1);for(int distance=0;distance<static_cast<int>(hits.size());++distance){int i=(start+distance)%static_cast<int>(hits.size());if(hits[i].enabled){focused=i;focusedKey=hits[i].key;break;}}}if(focused<0)focusedKey.clear();
        if(!pressedKey.empty()&&IndexForKey(pressedKey)<0){pressedKey.clear();pressedId=0;}
        if(rowPositions.size()>256){for(auto it=rowPositions.begin();it!=rowPositions.end();)if(IndexForKey(it->first)<0)it=rowPositions.erase(it);else ++it;}
        if(childIds.size()>8192){for(auto it=childIds.begin();it!=childIds.end();)if(IndexForKey(it->first)<0)it=childIds.erase(it);else ++it;}
        std::wstring status=note;if(flyout!=Flyout::None)status+=flyout==Flyout::Wifi?connections.wifiMessage:connections.bluetoothMessage;if(status!=lastStatus){lastStatus=status;LONG child=CHILDID_SELF;for(const auto& hit:hits)if(hit.id==1800||hit.id==2040)child=hit.child;NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED,window,OBJID_CLIENT,child);}
    }
    void RefreshGeometry(){
        wallpaperReveal.Cancel();Place();scroll=std::clamp(scroll,0.0f,std::max(0.0f,Layout().contentHeight-Layout().body.Height()));
        dockWidth.Snap(expanded?SurfaceWidth():theme::DockWidth);dockHeight.Snap(expanded?SurfaceHeight():theme::DockHeight);
        width=dockWidth.value;height=dockHeight.value;paneBlend.Snap(1);previousPane.Reset();transitioning=false;RebuildLayout();Place();InvalidateRect(window,nullptr,FALSE);
    }
    void SnapMotion(){
        for(auto* v:{&dockReveal,&dockWidth,&dockHeight,&paneBlend,&wallpaperFocus,&tabLeft,&tabRight,&railPointer,&railStrength,&paletteBlend,&contentBlend,&togglePosition,&wallpaperParallax,&volumeMotion,&composerFocus})v->Snap(v->goal);
        for(auto& v:rings)v.Snap(v.goal);
        for(auto& [key,v]:hoverMotion){(void)key;v.Snap(v.goal);}
        for(auto& [key,v]:tileState){(void)key;v.Snap(v.goal);}
        for(auto& [key,v]:feedback){(void)key;v.Snap(v.goal);}
        for(auto& [key,v]:rowPositions){(void)key;v.Snap(v.goal);}
        previousPane.Reset();wallpaperReveal.Cancel();frameClock.Stop();frameTickReady=false;scheduler.Stop();KillTimer(window,TransitionTimer);transitioning=false;width=dockWidth.value;height=dockHeight.value;Place();
        if(!expanded&&!renderMode)ReleaseGraphics();
    }

    float dpi = 96, width = theme::DockWidth, height = theme::DockHeight;
    bool expanded = false, transitioning = false, tracking = false, draggingVolume = false;
    Tab tab = Tab::Controls;
    Flyout flyout=Flyout::None;
    dock::Spring dockReveal{0},dockWidth{theme::DockWidth},dockHeight{theme::DockHeight},paneBlend{1};
    dock::Spring wallpaperFocus{0};
    dock::Proximity proximity;
    std::map<int,dock::Spring> hoverMotion,tileState;
    dock::Spring composerFocus{0};
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
    bool appsRequested = false, wallpapersRequested = false,appsLoading=false,wallpaperLoading=false;
    bool customWallpaperFolder = false;
    std::vector<AppEntry> apps;
    std::vector<WallpaperEntry> wallpapers;
    std::vector<ComPtr<ID2D1Bitmap>> thumbnails;
    std::vector<size_t> filteredApps;
    std::wstring appQuery,selectedAppKey;
    size_t appPage = 0, selectedWallpaper = 0;
    std::wstring clipboard = L"Open this tab to read the current clipboard.", note;
    std::unique_ptr<MediaService> media;
    SystemState system;
    MediaState currentMedia;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1HwndRenderTarget> hostTarget;
    CompositionSurface composition;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::array<ComPtr<IDWriteTextFormat>, 10> formats;
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
    bool taskbarHotkey=false;
    HWND contextWindow=nullptr;
    bool browserContext=false,fullscreenContext=false;
    bool YieldResting()const{return browserContext||fullscreenContext;}
    bool wallpaperApplying=false,miniMode=false;
    WallpaperReveal wallpaperReveal;
    std::unique_ptr<HardwareStats> hardware;
    HardwareSnapshot hardwareState;
    AppPreferences appPreferences;
    std::wstring AppKey(const AppEntry& app)const{return app.parsingName.empty()?app.name:app.parsingName;}
    bool Favorite(const AppEntry& app)const{return std::find(appPreferences.favorites.begin(),appPreferences.favorites.end(),AppKey(app))!=appPreferences.favorites.end();}
    int AppGroup(const AppEntry& app)const{return Favorite(app)?0:std::find(appPreferences.recent.begin(),appPreferences.recent.end(),AppKey(app))!=appPreferences.recent.end()?1:2;}
    void SampleStats(){system.Sample();if(!renderMode){if(!hardware)hardware=std::make_unique<HardwareStats>(window);hardware->Request();}}
    static unsigned SystemColor(int index){auto c=GetSysColor(index);return (GetRValue(c)<<16)|(GetGValue(c)<<8)|GetBValue(c);}
    static double Luminance(unsigned color){auto channel=[](unsigned v){double x=v/255.0;return x<=0.04045?x/12.92:std::pow((x+0.055)/1.055,2.4);};return 0.2126*channel((color>>16)&255)+0.7152*channel((color>>8)&255)+0.0722*channel(color&255);}
    static double Contrast(unsigned a,unsigned b){double x=Luminance(a),y=Luminance(b);return (std::max(x,y)+0.05)/(std::min(x,y)+0.05);}
    std::wstring preferencesPath;
    bool fixedAccent=true;
    void LoadSettings(){
        PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return;preferencesPath=std::wstring(local)+L"\\Dako\\appearance.ini";CoTaskMemFree(local);
        motionOverride=GetPrivateProfileIntW(L"Dako",L"ReducedMotion",0,preferencesPath.c_str())!=0;opaqueMaterial=GetPrivateProfileIntW(L"Dako",L"Opaque",1,preferencesPath.c_str())!=0;
        int appearance=GetPrivateProfileIntW(L"Dako",L"Appearance",1,preferencesPath.c_str());systemAppearance=appearance==0;lightAppearance=appearance==1;
        browserClearance=static_cast<float>(std::clamp(GetPrivateProfileIntW(L"Dako",L"BrowserClearance",112,preferencesPath.c_str()),64U,240U));
        fixedAccent=GetPrivateProfileIntW(L"Dako",L"FixedAccent",1,preferencesPath.c_str())!=0;
        shell.SetMaximizeNew(GetPrivateProfileIntW(L"Dako",L"MaximizeNew",0,preferencesPath.c_str())!=0);
    }
    bool SaveSettings(){
        if(renderMode||previewMode||preferencesPath.empty())return true;std::error_code error;std::filesystem::create_directories(std::filesystem::path(preferencesPath).parent_path(),error);if(error)return false;
        auto temporary=preferencesPath+L".tmp";bool ok=true;
        for(auto pair:{std::pair{L"ReducedMotion",motionOverride?1:0},std::pair{L"Opaque",opaqueMaterial?1:0},std::pair{L"Appearance",systemAppearance?0:lightAppearance?1:2},std::pair{L"BrowserClearance",static_cast<int>(browserClearance)},std::pair{L"MaximizeNew",shell.MaximizeNew()?1:0},std::pair{L"FixedAccent",fixedAccent?1:0}})ok=WritePrivateProfileStringW(L"Dako",pair.first,std::to_wstring(pair.second).c_str(),temporary.c_str())&&ok;
        return ok&&MoveFileExW(temporary.c_str(),preferencesPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void ReadTextScale(){
        if(renderMode)return;DWORD percent=100,bytes=sizeof(percent);
        RegGetValueW(HKEY_CURRENT_USER,LR"(Software\Microsoft\Accessibility)",L"TextScaleFactor",RRF_RT_REG_DWORD,nullptr,&percent,&bytes);
        float next=std::clamp(static_cast<float>(percent)/100,1.0f,2.25f);
        if(next==textScale)return;textScale=next;ReleaseGraphics();
        if(editFont){DeleteObject(editFont);editFont=nullptr;}EnsureEditResources();
        for(HWND input:{taskEdit,appSearch,wifiPassword,clipboardEdit})if(input)SendMessageW(input,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
    }
    void ConfigureAppearance(bool forceHighContrast=false){
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);highContrast=forceHighContrast||(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;
        if(systemAppearance&&!renderMode){DWORD light=0,size=sizeof(light);RegGetValueW(HKEY_CURRENT_USER,LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);lightAppearance=light!=0;}
        theme::Surface=lightAppearance?theme::Paper:theme::Ink;theme::Card=lightAppearance?0xF7F5EF:0x1B1B18;theme::CardHover=lightAppearance?0xDEDBD4:0x35352F;theme::Inset=lightAppearance?0xE6E3DB:0x242420;theme::Border=lightAppearance?0xCFCCC5:0x3B3B35;theme::Text=lightAppearance?theme::Ink:theme::Paper;theme::Muted=lightAppearance?0x65635E:0xB5B2A9;
        if(highContrast){theme::Surface=SystemColor(COLOR_WINDOW);theme::Card=theme::Inset=SystemColor(COLOR_BTNFACE);theme::CardHover=SystemColor(COLOR_BTNFACE);theme::Text=SystemColor(COLOR_WINDOWTEXT);theme::Muted=SystemColor(COLOR_BTNTEXT);theme::Border=theme::Text;}
        ApplyAccent(fixedAccent?theme::Signal:targetAccent);ConfigureMaterial();
        if(editBackground){DeleteObject(editBackground);editBackground=nullptr;}EnsureEditResources();InvalidateRect(window,nullptr,FALSE);
    }
    void ApplyAccent(unsigned rgb){
        if(fixedAccent)rgb=theme::Signal;
        if(highContrast){rgb=SystemColor(COLOR_HIGHLIGHT);theme::Accent=theme::AccentSoft=theme::AccentHover=rgb;theme::SelectedText=SystemColor(COLOR_HIGHLIGHTTEXT);if(Contrast(theme::SelectedText,rgb)<4.5)theme::SelectedText=Contrast(0,rgb)>Contrast(0xFFFFFF,rgb)?0:0xFFFFFF;theme::AccentInk=theme::SelectedText;theme::Focus=SystemColor(COLOR_WINDOWTEXT);theme::Secondary=theme::Tertiary=theme::Focus;return;}
        theme::Accent=rgb;theme::AccentHover=dock::Mix(rgb,lightAppearance?theme::Ink:theme::Paper,0.10f);theme::AccentInk=Contrast(rgb,theme::Ink)>Contrast(rgb,theme::Paper)?theme::Ink:theme::Paper;theme::AccentSoft=dock::Mix(theme::Surface,rgb,0.12f);
        unsigned readable=rgb;for(int i=0;i<32&&Contrast(readable,theme::AccentSoft)<4.5;++i)readable=dock::Mix(readable,lightAppearance?0x000000:0xFFFFFF,0.08f);
        theme::SelectedText=theme::Focus=readable;
        theme::Secondary=lightAppearance?0x526E62:0xA5BCAE;theme::Tertiary=lightAppearance?0x546C82:0xA3B8CC;
    }
    void SetAccent(unsigned rgb){if(fixedAccent)rgb=theme::Signal;if(rgb==targetAccent)return;previousAccent=theme::Accent;targetAccent=rgb;paletteBlend.Snap(0);paletteBlend.goal=1;if(reducedMotion||renderMode){paletteBlend.Snap(1);ApplyAccent(rgb);}else ArmMotion();InvalidateRect(window,nullptr,FALSE);}
    void UpdateContext(){
        if(renderMode)return;
        HWND foreground=GetForegroundWindow();DWORD processId=0;GetWindowThreadProcessId(foreground,&processId);
        if(!foreground||processId==GetCurrentProcessId()||GetWindow(foreground,GW_OWNER)==window||desktop::IsTaskbar(foreground))return;
        bool wasFullscreen=fullscreenContext;RECT activeBounds{};MONITORINFO activeMonitor{sizeof(activeMonitor)};
        GetWindowRect(foreground,&activeBounds);GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST),&activeMonitor);
        fullscreenContext=!(GetWindowLongPtrW(foreground,GWL_STYLE)&WS_CAPTION)&&activeBounds.left<=activeMonitor.rcMonitor.left&&activeBounds.top<=activeMonitor.rcMonitor.top&&activeBounds.right>=activeMonitor.rcMonitor.right&&activeBounds.bottom>=activeMonitor.rcMonitor.bottom;
        if(foreground==contextWindow){if(wasFullscreen!=fullscreenContext){if(expanded)RefreshGeometry();else Place();}return;}
        bool wasBrowser=browserContext;contextWindow=foreground;browserContext=false;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
        if(process){wchar_t path[32768]{};DWORD length=32768;if(QueryFullProcessImageNameW(process,0,path,&length))browserContext=dock::BrowserExecutable(std::wstring_view(path,length));CloseHandle(process);}
        wchar_t name[64]{};GetClassNameW(foreground,name,64);if(!wcscmp(name,L"MozillaWindowClass"))browserContext=true;
        if(wasBrowser!=browserContext||wasFullscreen!=fullscreenContext){if(expanded)RefreshGeometry();else Place();}
    }

    ~Island() {frameClock.Stop(); if(backdropWindow)DestroyWindow(backdropWindow); if (editFont) DeleteObject(editFont); if (editBackground) DeleteObject(editBackground); }
    void PreviewTrace(const char* stage){if(!previewMode)return;wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);std::ofstream trace(std::filesystem::path(executable).parent_path()/L"preview-startup.txt",std::ios::app);trace<<GetTickCount64()<<" "<<stage<<"\n";}
    void Initialize() {
        PreviewTrace("initialize");if(previewMode)SetPropW(window,L"Dako.IsolatedPreview",reinterpret_cast<HANDLE>(1));
        dpi = static_cast<float>(GetDpiForWindow(window));
        BOOL animation = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0); systemReduced=!animation;reducedMotion=systemReduced;
        if(!renderMode)LoadSettings();reducedMotion=systemReduced||motionOverride;ReadTextScale();ConfigureAppearance();
        CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&accessibleProperties));
        Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())));
        const float sizes[]{12,12,14,16,28,38,18,13,32,11};
        ComPtr<IDWriteFontCollection> fontCollection;UINT32 iconFamily=0;BOOL hasFluent=FALSE;
        if(SUCCEEDED(textFactory->GetSystemFontCollection(&fontCollection)))fontCollection->FindFamilyName(L"Segoe Fluent Icons",&iconFamily,&hasFluent);
        const wchar_t* iconFont=hasFluent?L"Segoe Fluent Icons":L"Segoe MDL2 Assets";
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};GetUserDefaultLocaleName(locale,LOCALE_NAME_MAX_LENGTH);
        for (size_t i = 0; i < formats.size(); ++i) {
            const wchar_t* family=i==type::Icon?iconFont:i==type::Editorial||i==type::Display||i==type::Metric?L"Georgia":i==type::Mono?L"Consolas":L"Segoe UI";
            Check(textFactory->CreateTextFormat(family, nullptr,
                i==3||i==7 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, sizes[i], locale, &formats[i]));
            formats[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis; textFactory->CreateEllipsisTrimmingSign(formats[i].Get(), &ellipsis);
            formats[i]->SetTrimming(&trim, ellipsis.Get());
        }
        int corner = 1; DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        PreviewTrace("native-services");if(!renderMode&&!fixtureGeometry)system.Initialize(window);PreviewTrace("native-services-ready");
        if (!renderMode) {
            catalog=std::make_unique<DesktopCatalog>(window);
            appPreferences.Load();
            for (const auto& task : taskStore.Load()) tasks.push_back({task.first,task.second,nextTask++});
            if(!previewMode){
                toggleHotkey = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE) != FALSE;
                quitHotkey = RegisterHotKey(window, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != FALSE;
                taskbarHotkey=RegisterHotKey(window,4,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,'T')!=FALSE;
            }
        }
        Place();
        if(!renderMode&&!previewMode&&!toggleHotkey)note=L"Global shortcut unavailable. Use the tray menu.";
        RebuildLayout();PreviewTrace("initialized");
        if(!renderMode)SetCoalescableTimer(window,ProximityTimer,100,nullptr,50);
    }
    void ReleaseGraphics() {frameClock.Stop();frameTickReady=false; if(backdropWindow)ShowWindow(backdropWindow,SW_HIDE);previousPane.Reset();wallpaperMask.Reset();appIcons.clear();thumbnails.clear(); brush.Reset(); target.Reset(); hostTarget.Reset();composition.Reset();if(scheduler.active&&!renderMode&&!reducedMotion)SetTimer(window,TransitionTimer,motionInterval,nullptr);if(!renderMode)SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)&~WS_EX_NOREDIRECTIONBITMAP); }
    float LayoutWidth()const{return Layout().panel.Width();}
    float LayoutHeight()const{return Layout().panel.Height();}
    float SurfaceWidth()const{return LayoutWidth()*textScale;}
    float SurfaceHeight()const{return LayoutHeight()*textScale;}
    float ContentWidth() const {return LayoutWidth();}
    float PanelX()const{return renderMode?0:(envelopeWidth-SurfaceWidth())/2;}
    float HandleY()const{return 0;}
    float PanelY()const{return renderMode?0:browserContext?std::min(browserClearance,std::max(12.0f,envelopeHeight-272.0f)):12.0f;}
    float browserClearance=112;
    std::array<POINT,35> NotchPixels(float scale)const{
        auto curve=dock::Notch(renderMode?0:(envelopeWidth-theme::DockWidth)/2,HandleY(),theme::DockWidth,theme::DockHeight);
        std::array<POINT,35> points{};for(size_t i=0;i<points.size();++i)points[i]={static_cast<LONG>(std::lround(curve[i].x*scale)),static_cast<LONG>(std::lround(curve[i].y*scale))};return points;
    }
    void PaintNotch(HDC dc){
        if(YieldResting())return;
        auto points=NotchPixels(dpi/96);HBRUSH fill=CreateSolidBrush(RGB(13,13,15));auto oldBrush=SelectObject(dc,fill);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        Polygon(dc,points.data(),static_cast<int>(points.size()));SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(fill);
    }
    void DrawNotch(){
        auto points=dock::Notch(renderMode?0:(envelopeWidth-theme::DockWidth)/2,HandleY(),theme::DockWidth,theme::DockHeight);
        ComPtr<ID2D1PathGeometry> geometry;Check(factory->CreatePathGeometry(&geometry));ComPtr<ID2D1GeometrySink> sink;Check(geometry->Open(&sink));
        sink->BeginFigure({points[0].x,points[0].y},D2D1_FIGURE_BEGIN_FILLED);for(size_t i=1;i<points.size();++i)sink->AddLine({points[i].x,points[i].y});sink->EndFigure(D2D1_FIGURE_END_CLOSED);Check(sink->Close());
        brush->SetColor(Color(theme::Surface));target->FillGeometry(geometry.Get(),brush.Get());
    }
    std::array<POINT,69> AttachedPixels(float scale)const{auto outline=dock::Attached((envelopeWidth-width)/2,0,width,height);std::array<POINT,69> points{};
        for(size_t i=0;i<points.size();++i)points[i]={static_cast<LONG>(std::lround(outline[i].x*scale)),static_cast<LONG>(std::lround(outline[i].y*scale))};return points;}
    void IslandSurface(){
        D2D1_RECT_F rect{(envelopeWidth-width)/2,PanelY(),(envelopeWidth+width)/2,PanelY()+height};
        brush->SetColor(Color(theme::Surface,backdropAvailable&&!opaqueMaterial&&!highContrast?0.94f:1.0f));
        target->FillRoundedRectangle(D2D1::RoundedRect(rect,theme::Radius,theme::Radius),brush.Get());
        brush->SetColor(Color(theme::Border));
        target->DrawRoundedRectangle(D2D1::RoundedRect({rect.left+0.5f,rect.top+0.5f,rect.right-0.5f,rect.bottom-0.5f},theme::Radius,theme::Radius),brush.Get(),1);
    }
    static LRESULT CALLBACK BackdropProcedure(HWND handle,UINT message,WPARAM wparam,LPARAM lparam){
        if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(handle,message,wparam,lparam);
    }
    void PositionBackdrop(){
        if(!backdropWindow)return;
        if(!expanded||dockReveal.value<0.01f||opaqueMaterial||highContrast||!backdropAvailable){ShowWindow(backdropWindow,SW_HIDE);return;}
        RECT host{};GetWindowRect(window,&host);float scale=dpi/96;int x=host.left+static_cast<int>(std::lround((envelopeWidth-width)*scale/2)),y=host.top+static_cast<int>(std::lround(PanelY()*scale)),w=static_cast<int>(std::lround(width*scale)),h=static_cast<int>(std::lround(height*scale));
        RECT next{x,y,x+w,y+h};if(EqualRect(&next,&backdropPlacement)&&IsWindowVisible(backdropWindow))return;backdropPlacement=next;
        int radius=static_cast<int>(theme::Radius*scale);HRGN region=CreateRoundRectRgn(0,0,w+1,h+1,radius*2,radius*2);if(!SetWindowRgn(backdropWindow,region,FALSE))DeleteObject(region);
        SetWindowPos(backdropWindow,window,x,y,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    void ConfigureMaterial(){
        DWORD transparency=1,size=sizeof(transparency);RegGetValueW(HKEY_CURRENT_USER,LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&size);
        systemTransparency=transparency;
        // The DWM backdrop must own a tight panel HWND. Applying it to our large
        // transparent input envelope can blur unrelated browser content.
        int none=DWMSBT_NONE;DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&none,sizeof(none));MARGINS clear{};DwmExtendFrameIntoClientArea(window,&clear);
        if(!renderMode&&!opaqueMaterial&&!highContrast&&transparency&&!backdropWindow){
            WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=BackdropProcedure;type.lpszClassName=L"Dako.Panel.Material";RegisterClassW(&type);
            backdropWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,type.lpszClassName,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,type.hInstance,nullptr);
        }
        int backdrop=opaqueMaterial||highContrast||!transparency?DWMSBT_NONE:DWMSBT_TRANSIENTWINDOW;
        backdropResult=backdropWindow?DwmSetWindowAttribute(backdropWindow,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop)):HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        backdropAvailable=backdropWindow&&SUCCEEDED(backdropResult)&&backdrop==DWMSBT_TRANSIENTWINDOW;
        if(backdropWindow){MARGINS margins=backdropAvailable?MARGINS{-1,-1,-1,-1}:MARGINS{};DwmExtendFrameIntoClientArea(backdropWindow,&margins);BOOL dark=!lightAppearance;DwmSetWindowAttribute(backdropWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));}
        PositionBackdrop();
    }
    void Graphics(){
        if(target)return;PreviewTrace("graphics-create");RECT client{};GetClientRect(window,&client);
        if(!renderMode){SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)|WS_EX_NOREDIRECTIONBITMAP);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
        if(!renderMode&&SUCCEEDED(composition.Create(window,factory.Get(),static_cast<UINT>(client.right),static_cast<UINT>(client.bottom),dpi,&target))){ConfigureMaterial();}
        else{
            composition.Reset();if(backdropWindow)ShowWindow(backdropWindow,SW_HIDE);SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)&~WS_EX_NOREDIRECTIONBITMAP);backdropAvailable=false;int backdrop=DWMSBT_NONE;DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop));
            Check(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(),dpi,dpi),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(static_cast<UINT>(client.right),static_cast<UINT>(client.bottom))),&hostTarget));Check(hostTarget.As(&target));
        }
        Check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush));PreviewTrace("graphics-ready");if(scheduler.active&&composition.FrameReady()&&frameClock.Start(window,MotionFrame,composition.FrameReady()))KillTimer(window,TransitionTimer);
    }
    void Place() {
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        const float scale = dpi / 96.0f;
        auto monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        if(monitor!=placementMonitor){placementMonitor=monitor;MONITORINFOEXW display{};display.cbSize=sizeof(display);DEVMODEW mode{};mode.dmSize=sizeof(mode);if(GetMonitorInfoW(monitor,&display)&&EnumDisplaySettingsW(display.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>=30)motionInterval=std::max(4U,1000U/std::min(240U,static_cast<UINT>(mode.dmDisplayFrequency)));}
        if(!fixtureGeometry){envelopeWidth=std::min(theme::EnvelopeWidth,(info.rcMonitor.right-info.rcMonitor.left)/scale);
        envelopeHeight=std::min(theme::EnvelopeHeight,(info.rcMonitor.bottom-info.rcMonitor.top)/scale);}
        const int w=static_cast<int>(std::lround((renderMode?width:envelopeWidth)*scale));
        const int h=static_cast<int>(std::lround((renderMode?height:envelopeHeight)*scale));
        RECT bounds{};GetWindowRect(window,&bounds);
        int left=info.rcMonitor.left+(info.rcMonitor.right-info.rcMonitor.left-w)/2;
        int top=info.rcMonitor.top;
        if(bounds.left!=left||bounds.top!=top||bounds.right-bounds.left!=w||bounds.bottom-bounds.top!=h)
            SetWindowPos(window,HWND_TOPMOST,left,top,w,h,SWP_NOACTIVATE);
        const int radius=static_cast<int>(theme::Radius*scale);
        std::array<int,9> signature{w,h,static_cast<int>(std::lround(width*scale)),static_cast<int>(std::lround(height*scale)),static_cast<int>(PanelY()*scale),static_cast<int>(dpi),dockReveal.value>0.003f,YieldResting(),renderMode};
        if(signature!=regionSignature){regionSignature=signature;
        auto notch=NotchPixels(scale);
        HRGN shape=renderMode?CreateRoundRectRgn(0,0,w+1,h+1,radius*2,radius*2):YieldResting()?CreateRectRgn(0,0,0,0):CreatePolygonRgn(notch.data(),static_cast<int>(notch.size()),WINDING);
        if(!renderMode&&dockReveal.value>0.003f){
            int x=static_cast<int>((envelopeWidth-width)*0.5f*scale),y=static_cast<int>(PanelY()*scale);
            HRGN body=CreateRoundRectRgn(x,y,x+static_cast<int>(std::lround(width*scale)),y+static_cast<int>(std::lround(height*scale)),radius*2,radius*2);
            CombineRgn(shape,shape,body,RGN_OR);DeleteObject(body);
        }
        if (!SetWindowRgn(window, shape, TRUE)) DeleteObject(shape);
        }
        PositionBackdrop();PositionInputs();
    }
    void PositionInputs(){
        if(layoutOnly)return;
        bool visible=expanded&&!capturingPane;float scale=dpi/96;
        auto position=[&](HWND input,int id){if(!input)return;auto found=std::find_if(hits.begin(),hits.end(),[&](const Hit& h){return h.id==id;});bool show=visible&&found!=hits.end();
            if(show){EnableWindow(input,found->enabled);auto r=found->rect;auto c=found->clip;show=r.top>=c.top-0.01f&&r.bottom<=c.bottom+0.01f;if(show){r.top=std::max(r.top,c.top);r.bottom=std::min(r.bottom,c.bottom);}if(show)SetWindowPos(input,nullptr,static_cast<int>(std::lround((PanelX()+r.left*textScale)*scale)),static_cast<int>(std::lround((PanelY()+r.top*textScale)*scale)),static_cast<int>(std::lround((r.right-r.left)*textScale*scale)),static_cast<int>(std::lround((r.bottom-r.top)*textScale*scale)),SWP_NOZORDER|SWP_NOACTIVATE);}
            ShowWindow(input,show?SW_SHOWNOACTIVATE:SW_HIDE);
        };position(taskEdit,400);position(appSearch,401);position(wifiPassword,402);position(clipboardEdit,403);
    }
    void ArmMotion(){
        if(renderMode||reducedMotion){SnapMotion();return;}
        bool started=scheduler.Start();if(started)lastMotion=steady_clock::now();
        if(composition.FrameReady()&&frameClock.Start(window,MotionFrame,composition.FrameReady()))KillTimer(window,TransitionTimer);
        else if(started)SetTimer(window,TransitionTimer,motionInterval,nullptr);
    }
    void SetExpanded(bool value,bool deliberate=true) {
        if(value&&!expanded)UpdateContext();
        expanded = value; hovered = -1; if(!value){focusedKey.clear();pressedKey.clear();focused=-1;}
        if(value){system.RefreshVolume();if(!renderMode&&!fixtureGeometry&&tab==Tab::Controls&&flyout==Flyout::None){if(!connectivity)connectivity=std::make_unique<Connectivity>(window);connectivity->RefreshWifi(false);connectivity->RefreshBluetooth(false);}if(!renderMode&&!fixtureGeometry&&!media&&tab==Tab::Controls&&flyout==Flyout::None)media=std::make_unique<MediaService>(window);}
        if(media)media->SetVisible(value&&tab==Tab::Controls&&flyout==Flyout::None);
        if(!value){clipboard.clear();if(clipboardEdit)SetWindowTextW(clipboardEdit,L"");clipboardState=L"Preview cleared";keyboardOpen=false;launchPreview=false;if(deliberate)proximity.Dismiss();else{proximity.outside=0;proximity.dwell=0;}if(wifiPassword)SetWindowTextW(wifiPassword,L"");if(connectivity)connectivity->CancelDiscovery();}
        dockReveal.goal=value?1.0f:0.0f;dockWidth.goal=value?SurfaceWidth():theme::DockWidth;dockHeight.goal=value?SurfaceHeight():theme::DockHeight;
        transitioning=!reducedMotion&&!renderMode;
        if(transitioning)ArmMotion();
        else{dockReveal.Snap(dockReveal.goal);dockWidth.Snap(dockWidth.goal);dockHeight.Snap(dockHeight.goal);width=dockWidth.value;height=dockHeight.value;Place();if(!value&&!renderMode)ReleaseGraphics();}
        RebuildLayout();UpdateTimers(); InvalidateRect(window, nullptr, FALSE);
    }
    void Transition() {
        if(!scheduler.active)return;
        auto now=steady_clock::now();float dt=duration<float>(now-lastMotion).count();lastMotion=now;if(!diagnosticsPath.empty()&&frameIntervals.size()<8192)frameIntervals.push_back(dt*1000);
        if(dt>0.5f){SnapMotion();InvalidateRect(window,nullptr,FALSE);return;}
        // Analytic springs consume elapsed time in bounded steps without stretching a stall.
        bool moving=false;
        auto advance=[&](dock::Spring& v,float frequency=23.0f){float remaining=dt;while(remaining>0){float step=std::min(remaining,0.05f);v.Step(step,frequency);remaining-=step;}moving|=v.Moving();};
        for(auto* v:{&dockReveal,&dockWidth,&dockHeight})advance(*v,38);
        for(auto* v:{&paneBlend,&wallpaperFocus,&tabLeft,&tabRight,&railPointer,&railStrength,&paletteBlend,&contentBlend,&togglePosition,&wallpaperParallax,&volumeMotion,&composerFocus})advance(*v,34);
        for(auto& v:rings)advance(v,20);
        for(auto& [id,v]:hoverMotion){(void)id;advance(v,30);}
        for(auto& [id,v]:tileState){(void)id;advance(v,24);}
        for(auto& [key,v]:feedback){(void)key;v.Step(dt);moving|=v.Moving();}
        for(auto& [key,v]:rowPositions){(void)key;advance(v,24);}
        for(auto it=feedback.begin();it!=feedback.end();)if(!it->second.Moving())it=feedback.erase(it);else ++it;
        width=dockWidth.value;height=dockHeight.value;
        transitioning=dockReveal.Moving()||dockWidth.Moving()||dockHeight.Moving();
        ApplyAccent(dock::Mix(previousAccent,targetAccent,paletteBlend.value));
        if(paneBlend.value==1)previousPane.Reset();
        if(!moving){frameClock.Stop();frameTickReady=false;scheduler.Stop();KillTimer(window,TransitionTimer);for(auto it=hoverMotion.begin();it!=hoverMotion.end();)if(it->second.value==0)it=hoverMotion.erase(it);else ++it;}
        if(dockReveal.value==0&&!expanded)SnapMotion();
        Place();RebuildLayout();InvalidateRect(window,nullptr,FALSE);
    }
    void ProximityTick(){
        UpdateContext();
        shell.UpdateBrowserCutout(nullptr); // Restore legacy cutouts; never alter foreign chrome.
        auto now=steady_clock::now();float dt=duration<float>(now-lastProximity).count();lastProximity=now;
        POINT cursor{};GetCursorPos(&cursor);ScreenToClient(window,&cursor);
        float x=cursor.x*96.0f/dpi,y=cursor.y*96.0f/dpi,center=envelopeWidth/2;
        bool buttonDown=(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000);
        bool approaching=std::abs(x-center)<theme::DockWidth/2+18&&y>=0&&y<theme::DockHeight+6&&!buttonDown;
        bool withinBody=std::abs(x-center)<width/2&&y>=PanelY()&&y<PanelY()+height;
        bool inside=std::abs(x-center)<std::max(width,theme::DockWidth)/2+18&&y>=0&&y<PanelY()+height+18;
        HWND foreground=GetForegroundWindow();bool ownedModal=foreground&&GetWindow(foreground,GW_OWNER)==window;
        bool editing=foreground==window&&(GetFocus()==taskEdit||GetFocus()==appSearch||GetFocus()==wifiPassword||GetFocus()==clipboardEdit);
        bool protectedInput=previewMode||modalOpen||ownedModal||editing||draggingVolume||launchPreview||(keyboardOpen&&foreground==window);
        if(expanded&&buttonDown&&!withinBody&&!protectedInput){SetExpanded(false);return;}
        if(!expanded&&dockReveal.value>0.05f&&proximity.cooldown==0&&withinBody&&!buttonDown){SetExpanded(true);return;}
        int action=proximity.Step(dt,expanded,approaching,inside,protectedInput);
        if(action>0)SetExpanded(true);else if(action<0)SetExpanded(false,false);
    }
    void UpdateTimers() {
        if(renderMode||fixtureGeometry)return;
        auto timer=[&](UINT_PTR id,bool enabled,UINT period,bool& active){if(enabled==active)return;active=enabled;if(enabled)SetTimer(window,id,period,nullptr);else KillTimer(window,id);};
        bool stats=expanded&&tab==Tab::Stats&&flyout==Flyout::None;
        if(stats&&!statsTicking)SampleStats();timer(StatsTimer,stats,2000,statsTicking);
        timer(MediaTimer,currentMedia.playing&&expanded&&tab==Tab::Controls&&flyout==Flyout::None,1000,mediaTicking);
        timer(FocusTimer,focusRunning||(expanded&&tab==Tab::Today&&flyout==Flyout::None),1000,focusTicking);
        timer(ConnectivityTimer,expanded&&flyout!=Flyout::None,2500,connectionsTicking);
    }
    void SetTab(Tab value) {
        if(expanded&&!miniMode&&flyout==Flyout::None&&tab==value)return;
        miniMode=false;
        previousPane.Reset();paneBlend.Snap(1);
        if(connectivity)connectivity->CancelDiscovery();flyout=Flyout::None;
        bodyDirection=static_cast<int>(value)>=static_cast<int>(tab)?1.0f:-1.0f;tab = value;note.clear(); scroll=0;focusedKey.clear();focused = hovered = -1;
        if (tab == Tab::Clipboard&&!fixtureGeometry) ReadClipboard();
        if (!renderMode&&!fixtureGeometry && (tab==Tab::Apps || tab==Tab::Wallpaper)) {
            if (!catalog) catalog=std::make_unique<DesktopCatalog>(window);
            if (tab==Tab::Apps && (!appsRequested||(!appsLoading&&apps.empty()))) { appsRequested=true;appsLoading=true; catalog->DiscoverApps(); }
            if (tab==Tab::Wallpaper && !wallpapersRequested) { wallpapersRequested=true;wallpaperLoading=true;selectedWallpaper=2;wallpaperFocus.Snap(2);catalog->DiscoverWallpapers(); }
        }
        SetExpanded(true);
        PositionInputs();
    }
    D2D1_COLOR_F Color(unsigned rgb,float alpha=1){return D2D1::ColorF(rgb,alpha);}
    void Fill(D2D1_RECT_F rect, unsigned color, float radius = 12) {
        if(layoutOnly)return;
        if(rect.bottom-rect.top>=48&&rect.right-rect.left>=60)radius=std::min(radius,10.0f);
        brush->SetColor(Color(color)); target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
        if(radius>=10&&rect.bottom-rect.top>=32&&rect.right-rect.left>=60){
            brush->SetColor(Color(theme::Border,highContrast?1.0f:0.65f));D2D1_RECT_F edge{rect.left+0.5f,rect.top+0.5f,rect.right-0.5f,rect.bottom-0.5f};
            target->DrawRoundedRectangle(D2D1::RoundedRect(edge,radius,radius),brush.Get(),1);
        }
    }
    void Text(const std::wstring& text, D2D1_RECT_F rect, int format = type::Body, unsigned color = theme::Text,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
        if(layoutOnly||rect.right<=rect.left||rect.bottom<=rect.top)return;
        brush->SetColor(Color(color)); formats[format]->SetTextAlignment(align);
        target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), formats[format].Get(), rect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Line(float x1, float y1, float x2, float y2, unsigned color, float thickness = 1) {
        if(layoutOnly)return;
        brush->SetColor(Color(color)); target->DrawLine({x1,y1},{x2,y2},brush.Get(),thickness);
    }
    void AddHit(int id,D2D1_RECT_F rect,std::wstring name,bool enabled=true){
        auto original=rect;if(buildingBody){rect.top-=scroll;rect.bottom-=scroll;}
        auto clip=!expanded?D2D1_RECT_F{0,0,theme::DockWidth,theme::DockHeight}:buildingBody?Body():R(Layout().panel);
        auto visible=layout::Intersection({rect.left,rect.top,rect.right,rect.bottom},{clip.left,clip.top,clip.right,clip.bottom});
        (void)visible;
        auto key=StableKey(id);auto [it,inserted]=childIds.try_emplace(key,nextChild);if(inserted)++nextChild;
        Hit hit{id,rect,std::move(name),enabled,key,it->second,0,10,clip,nullptr};
        if(id==400||id==401||id==402||id==403){hit.input=id==400?taskEdit:id==401?appSearch:id==402?wifiPassword:clipboardEdit;}
        hits.push_back(std::move(hit));(void)original;
    }
    void FocusRing(int id, D2D1_RECT_F rect, float radius = 12) {
        if(layoutOnly)return;
        if (focusedKey==StableKey(id)) {
            brush->SetColor(Color(theme::Focus)); target->DrawRoundedRectangle(D2D1::RoundedRect(rect,radius,radius), brush.Get(),highContrast?3.0f:1.5f);
            if(highContrast){brush->SetColor(Color(theme::Surface));target->DrawRoundedRectangle(D2D1::RoundedRect(rect,radius,radius),brush.Get(),1);}
        }
    }
    bool Hover(int id) const { return hovered >= 0 && hovered < static_cast<int>(hits.size()) && hits[hovered].id == id; }
    float HoverAmount(int id)const{if(highContrast)return 0;auto found=hoverMotion.find(id);return found==hoverMotion.end()?0:found->second.value;}
    void SetHover(int next){
        if(next==hovered)return;
        if(hovered>=0&&hovered<static_cast<int>(hits.size()))hoverMotion[hits[hovered].id].goal=0;
        hovered=next;hoveredKey=next>=0&&next<static_cast<int>(hits.size())?hits[next].key:L"";
        if(next>=0&&next<static_cast<int>(hits.size()))hoverMotion[hits[next].id].goal=1;
        if(reducedMotion||renderMode){for(auto& [id,v]:hoverMotion){(void)id;v.Snap(v.goal);}}else ArmMotion();InvalidateRect(window,nullptr,FALSE);
    }
    D2D1_RECT_F Tactile(int id,D2D1_RECT_F rect){
        float amount=pressedKey==StableKey(id)?1.0f:0.0f;
        auto found=feedback.find(StableKey(id));if(found!=feedback.end())amount+=found->second.value;
        float inset=std::clamp(amount,-0.3f,1.0f)*1.5f;return {rect.left+inset,rect.top+inset,rect.right-inset,rect.bottom-inset};
    }
    void Button(int id,D2D1_RECT_F rect,const std::wstring& title,const std::wstring& subtitle,const wchar_t* icon,bool active=false,bool enabled=true){
        AddHit(id,rect,title+L". "+subtitle,enabled);auto visual=Tactile(id,rect);
        auto [state,inserted]=tileState.try_emplace(id,active?1.0f:0.0f);(void)inserted;state->second.goal=active?1.0f:0.0f;if(reducedMotion||renderMode||highContrast)state->second.Snap(state->second.goal);else if(!layoutOnly&&state->second.Moving())ArmMotion();
        Fill(visual,dock::Mix(dock::Mix(theme::Card,theme::AccentSoft,state->second.value),theme::CardHover,HoverAmount(id)),layout::CardRadius);
        Text(icon,{rect.left+12,rect.top+12,rect.left+40,rect.bottom-12},type::Icon,active?(highContrast?theme::SelectedText:theme::Accent):theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(title,{rect.left+50,rect.top+7,rect.right-26,rect.top+30},type::Control,highContrast&&active?theme::SelectedText:enabled?theme::Text:theme::Muted);
        Text(subtitle,{rect.left+50,rect.top+30,rect.right-26,rect.bottom-6},type::Secondary,highContrast&&active?theme::SelectedText:theme::Muted);
        Text(L"\xE76C",{rect.right-24,rect.top+15,rect.right-8,rect.bottom-15},type::Icon,highContrast&&active?theme::SelectedText:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(id,rect);
    }
    void Paragraph(const std::wstring& text,D2D1_RECT_F rect,unsigned color=theme::Muted,int format=type::Body){
        if(layoutOnly||rect.right<=rect.left||rect.bottom<=rect.top)return;
        ComPtr<IDWriteTextLayout> textLayout;
        if(SUCCEEDED(textFactory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),formats[format].Get(),rect.right-rect.left,rect.bottom-rect.top,&textLayout))){
            textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);textLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            brush->SetColor(Color(color));target->DrawTextLayout({rect.left,rect.top},textLayout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    float RowY(const std::wstring& key,float goal){
        // Reordering rows independently can cross headings and neighboring controls.
        // Stable geometry makes rapid filtering, pagination and device updates safe.
        auto [it,inserted]=rowPositions.try_emplace(key,goal);(void)inserted;it->second.Snap(goal);
        return it->second.value;
    }
    std::wstring Clock(double seconds) {
        auto s = static_cast<int>(std::max(0.0,seconds)); wchar_t buffer[32]; swprintf_s(buffer,L"%d:%02d",s/60,s%60); return buffer;
    }
    void MediaCard(bool compact) {
        auto b=Body();float top=compact?4.0f:b.top+(Layout().narrow?358.0f:230.0f),left=b.left,right=b.right;
        float cardHeight=compact?std::min(116.0f,LayoutHeight()-8):116.0f;
        float clusterHeight=std::clamp(cardHeight-52,28.0f,60.0f),controlsTop=top+clusterHeight+4,controlHeight=std::min(36.0f,cardHeight-clusterHeight-16);
        Fill({left,top,right,top+cardHeight},theme::Inset,14);
        if(compact)AddHit(Expand,{left,top,right,top+clusterHeight},L"Open full controls");
        AddHit(2000,{left,top,right,top+clusterHeight},currentMedia.title+L". "+currentMedia.artist+L". "+Clock(currentMedia.position)+L" of "+Clock(currentMedia.duration),false);
        D2D1::Matrix3x2F cluster;if(!layoutOnly){target->GetTransform(&cluster);auto layer=D2D1::LayerParameters();layer.opacity=contentBlend.value;target->PushLayer(layer,nullptr);target->SetTransform(D2D1::Matrix3x2F::Translation(8*(1-contentBlend.value),0)*cluster);}
        float art=std::min(42.0f,clusterHeight-8),inset=clusterHeight<44?4.0f:12.0f;
        Fill({left+12,top+inset,left+12+art,top+inset+art},theme::AccentSoft,10);
        Text(L"\xE8D6",{left+12,top+inset,left+12+art,top+inset+art},type::Icon,theme::Accent,DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(currentMedia.title,{left+24+art,top,right-12,top+(clusterHeight<44?clusterHeight:34)},type::Control);
        if(clusterHeight>=44)Text(currentMedia.artist,{left+24+art,top+34,right-12,top+54},type::Secondary,theme::Muted);
        if(!layoutOnly){target->SetTransform(cluster);target->PopLayer();}
        float center=(left+right)/2;
        for(int i=0;i<3;++i){D2D1_RECT_F rect{center-66+i*48,controlsTop,center-30+i*48,controlsTop+controlHeight};int id=Previous+i;bool enabled=currentMedia.available&&(i==0?currentMedia.canPrevious:i==1?currentMedia.canPlay:currentMedia.canNext);
            AddHit(id,rect,i==0?L"Previous track":i==1?L"Play or pause":L"Next track",enabled);
            Fill(Tactile(id,rect),i==1&&enabled?theme::Accent:theme::CardHover,18);
            Text(i==0?L"\xE892":i==2?L"\xE893":currentMedia.playing?L"\xE769":L"\xE768",rect,type::Icon,i==1&&enabled?theme::AccentInk:enabled?theme::Text:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(id,rect,18);
        }
        float progress=static_cast<float>(currentMedia.duration>0?std::clamp(currentMedia.position/currentMedia.duration,0.0,1.0):0);
        Line(left+12,top+cardHeight-8,right-12,top+cardHeight-8,theme::CardHover,2);Line(left+12,top+cardHeight-8,left+12+(right-left-24)*progress,top+cardHeight-8,theme::Accent,2);
        if(compact)FocusRing(Expand,{left,top,right,top+clusterHeight});
    }
    D2D1_RECT_F TabRect(int index)const{
        auto l=Layout();constexpr float weights[]{84,68,62,96,63,103,80};
        float offset=l.editorial?44.0f:0.0f;
        if(l.narrow){int row=index<4?0:1,start=row?4:0,end=row?7:4;float pitch=l.headerHeight<80?32.0f:40.0f,top=(l.headerHeight<80?4.0f:8.0f)+offset;float total=0,prior=0;for(int i=start;i<end;++i){total+=weights[i];if(i<index)prior+=weights[i];}
            float available=l.header.Width();return {l.header.left+available*prior/total,top+row*pitch,l.header.left+available*(prior+weights[index])/total-4,top+pitch-4+row*pitch};}
        float total=556,available=l.header.Width(),left=l.header.left,prior=0;for(int i=0;i<index;++i)prior+=weights[i];
        return {left+available*prior/total,8+offset,left+available*(prior+weights[index])/total-4,44+offset};
    }
    void CompactHeader(bool wireless=false){
        auto l=Layout();float mid=(l.header.left+l.header.right)/2;
        TextButton(wireless?FlyoutBack:1900,{l.header.left,0,mid-3,32},wireless?L"Back":TabTitle());
        TextButton(1901,{mid+3,0,l.header.right,32},note.empty()?L"Actions":L"Actions · status");
    }
    std::wstring TabTitle()const{constexpr const wchar_t* names[]{L"Controls",L"Today",L"Stats",L"Clipboard",L"Apps",L"Wallpaper",L"Power"};return names[static_cast<int>(tab)];}
    void Tabs(){
        if(Layout().emergency){CompactHeader();return;}
        auto l=Layout();
        if(l.editorial){
            Text(L"dako",{l.header.left,4,l.header.left+100,44},type::Editorial);
            Text(L"DESKTOP / EVERYDAY",{l.header.left+112,8,l.header.right,40},type::Mono,theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);
            if(!layoutOnly){brush->SetColor(Color(theme::Accent));target->FillEllipse(D2D1::Ellipse({l.header.left+75,33},3,3),brush.Get());}
            float titleTop=l.headerHeight-48;
            constexpr const wchar_t* headings[]{L"A little more control.",L"Make room for today.",L"Under the surface.",L"Keep it close.",L"Your next move.",L"A change of scenery.",L"Call it a day."};
            Text(headings[static_cast<int>(tab)],{l.header.left,titleTop,l.header.right,titleTop+44},type::Editorial);
        }
        constexpr const wchar_t* titles[]{L"Controls",L"Today",L"Stats",L"Clipboard",L"Apps",L"Wallpaper",L"Power"};
        auto selected=TabRect(static_cast<int>(tab));tabLeft.goal=selected.left;tabRight.goal=selected.right;
        if(reducedMotion||renderMode||Layout().narrow){tabLeft.Snap(selected.left);tabRight.Snap(selected.right);}
        for(int i=0;i<7;++i){auto rect=TabRect(i);AddHit(100+i,rect,titles[i]);
            if(HoverAmount(100+i)>0)Fill(rect,dock::Mix(theme::Surface,theme::Inset,HoverAmount(100+i)),6);
            Text(titles[i],rect,type::Control,i==static_cast<int>(tab)?theme::Text:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(100+i,rect,6);}
        float baseline=l.narrow?TabRect(6).bottom+2:selected.bottom+2;
        Line(l.header.left,baseline,l.header.right,baseline,theme::Border);
        // The underline is decorative; the label and hit rectangle update immediately.
        Line(tabLeft.value+10,selected.bottom+2,tabRight.value-10,selected.bottom+2,theme::Accent,2);
    }
    D2D1_RECT_F VolumeRect()const{auto b=Body();float y=b.top+(Layout().narrow?310.0f:182.0f);return {b.left,y,b.right,y+36};}
    bool HasAudio()const{return fixtureGeometry?fixtureAudioAvailable:system.audio!=nullptr;}
    void Controls(){
        auto b=Layout().body;bool narrow=Layout().narrow;int columns=narrow?2:3;
        const int ids[]{Wifi,Bluetooth,Audio,Night,Dark};
        const wchar_t* titles[]{L"Wi-Fi",L"Bluetooth",L"Audio settings",L"Night light settings",L"Color settings"};
        std::wstring subtitles[]{system.wifi,system.bluetooth,system.audioName,L"Open Windows Settings ↗",L"Open Windows Settings ↗"};
        const wchar_t* icons[]{L"\xE701",L"\xE702",L"\xE767",L"\xE708",L"\xE790"};
        for(int i=0;i<5;++i){int col=i%columns,row=i/columns,count=columns;
            if(!narrow&&i>=3){col=i-3;row=1;count=2;}
            auto tile=layout::Column(b,col,count);tile.top=b.top+row*(narrow?88.0f:74.0f);tile.bottom=tile.top+(narrow?76:62);
            Button(ids[i],R(tile),titles[i],subtitles[i],icons[i],(i==0&&system.wifiConnected)||(i==1&&std::any_of(connections.devices.begin(),connections.devices.end(),[](const BluetoothEntry& device){return device.connected;})));}
        auto slider=VolumeRect();AddHit(Volume,slider,L"System volume. "+Number(system.volume*100)+L" percent",HasAudio());
        Fill(slider,theme::Inset,18);float inset=12,track=slider.right-slider.left-inset*2;volumeMotion.goal=system.volume;if(reducedMotion||renderMode||draggingVolume)volumeMotion.Snap(volumeMotion.goal);float x=slider.left+inset+track*volumeMotion.value;
        Line(slider.left+inset,slider.top+18,slider.right-inset,slider.top+18,theme::Muted,3);Line(slider.left+inset,slider.top+18,x,slider.top+18,theme::Accent,3);
        if(!layoutOnly){brush->SetColor(Color(theme::Text));target->FillEllipse(D2D1::Ellipse({x,slider.top+18},6,6),brush.Get());}
        Text(L"OUTPUT VOLUME",{slider.left,slider.top-28,slider.right-140,slider.top-4},type::Mono,theme::Muted);
        Text(system.muted?L"Muted":HasAudio()?Number(system.volume*100)+L"%":L"No audio device",{slider.right-135,slider.top-28,slider.right,slider.top-4},type::Mono,theme::Text,DWRITE_TEXT_ALIGNMENT_TRAILING);FocusRing(Volume,slider,18);MediaCard(false);
    }
    D2D1_RECT_F TaskCard()const{auto b=Body();if(LayoutWidth()>=720){float start=b.left+160+12+(b.right-b.left-160-24)*0.54f+12;return {start,b.top,b.right,b.top+350};}return {b.left,b.top+410,b.right,b.top+760};}
    D2D1_RECT_F TaskInputRect()const{auto c=TaskCard();return {c.left+12,c.bottom-12-std::min(30.0f,Layout().body.Height()),c.right-52,c.bottom-12};}
    void Today(){
        auto b=Body();bool wide=LayoutWidth()>=720;auto task=TaskCard();
        D2D1_RECT_F clock{b.left,b.top,wide?b.left+160:b.right,b.top+(wide?180.0f:110.0f)};
        Fill(clock,theme::Card,14);SYSTEMTIME now{};GetLocalTime(&now);if(renderMode||fixtureGeometry)now={2026,10,5,23,14,42,0,0};wchar_t time[96]{},date[128]{};
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,time,96);GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_LONGDATE,&now,nullptr,date,128,nullptr);
        Text(time,{clock.left+8,clock.top+16,clock.right-8,clock.top+76},wide?4:5,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Paragraph(date,{clock.left+12,clock.top+86,clock.right-12,clock.bottom-12});
        AddHit(2001,clock,std::wstring(time)+L". "+date,false);
        SYSTEM_POWER_STATUS power{};bool valid=true;if(renderMode||fixtureGeometry){power.BatteryLifePercent=76;power.ACLineStatus=1;}else valid=GetSystemPowerStatus(&power)!=FALSE;
        D2D1_RECT_F battery{b.left,wide?b.top+192:b.top+122,wide?b.left+160:b.right,wide?b.top+350:b.top+180};Fill(battery,theme::Inset,14);
        std::wstring state=!valid?L"Power unavailable":power.BatteryFlag==128?L"AC · no battery":power.BatteryLifePercent<=100?Number(power.BatteryLifePercent)+L"% · "+(power.ACLineStatus==1?L"Plugged in":power.ACLineStatus==0?L"On battery":L"Power unknown"):L"Battery unknown";
        AddHit(2002,battery,state,false);
        Paragraph(state,{battery.left+12,battery.top+16,battery.right-12,battery.bottom-8},theme::Text);
        D2D1_RECT_F center=wide?D2D1_RECT_F{b.left+172,b.top,task.left-12,b.top+350}:D2D1_RECT_F{b.left,b.top+192,b.right,b.top+398};Fill(center,theme::Inset,14);
        Text(L"Local week",{center.left+12,center.top+8,center.right-12,center.top+32},type::Control);
        DWORD first=0;GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_IFIRSTDAYOFWEEK|LOCALE_RETURN_NUMBER,reinterpret_cast<LPWSTR>(&first),sizeof(first)/sizeof(wchar_t));int today=(now.wDayOfWeek+6-static_cast<int>(first)+7)%7;
        FILETIME file{};SystemTimeToFileTime(&now,&file);ULARGE_INTEGER ticks{};ticks.LowPart=file.dwLowDateTime;ticks.HighPart=file.dwHighDateTime;float pitch=(center.right-center.left-24)/7;
        for(int i=0;i<7;++i){ULARGE_INTEGER day{};day.QuadPart=ticks.QuadPart+static_cast<LONGLONG>(i-today)*864000000000LL;FILETIME when{day.LowPart,day.HighPart};SYSTEMTIME shown{};FileTimeToSystemTime(&when,&shown);wchar_t label[24]{};GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&shown,L"ddd",label,24,nullptr);float x=center.left+12+i*pitch;
            Text(label,{x,center.top+34,x+pitch,center.top+56},type::Secondary,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);if(i==today)Fill({x+2,center.top+58,x+pitch-2,center.top+88},theme::AccentSoft,12);AddHit(2010+i,{x,center.top+34,x+pitch,center.top+88},std::wstring(label)+L" "+Number(shown.wDay)+(i==today?L", today":L""),false);Text(Number(shown.wDay),{x,center.top+58,x+pitch,center.top+88},type::Body,i==today?theme::SelectedText:theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);}
        float fy=center.top+(wide?142.0f:98.0f),cx=center.left+50;rings[3].goal=focusDuration?static_cast<float>(focusSeconds)/focusDuration:0;if(reducedMotion||renderMode)rings[3].Snap(rings[3].goal);
        AddHit(2003,{center.left,fy,center.right,fy+76},L"Focus timer. "+Clock(focusSeconds)+(focusRunning?L", running":L", paused"),false);
        DrawRing(cx,fy+36,30,rings[3].value,theme::Accent,4);Text(Clock(focusSeconds),{cx-30,fy+20,cx+30,fy+50},type::Control,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);
        const int ids[]{FocusShort,FocusMedium,FocusTwenty};const int minutes[]{5,15,20};float px=center.left+94,pw=(center.right-px-12)/3;
        for(int i=0;i<3;++i)TextButton(ids[i],{px+i*pw,fy,px+(i+1)*pw-4,fy+28},Number(minutes[i])+L"m",focusDuration==minutes[i]*60);
        float half=(center.right-px-20)/2;TextButton(FocusStart,{px,fy+38,px+half,fy+72},focusRunning?L"Pause":L"Start",true);TextButton(FocusReset,{px+half+8,fy+38,center.right-12,fy+72},L"Reset");
        Fill(task,theme::Card,14);Text(L"Today · five tasks",{task.left+12,task.top+8,task.right-12,task.top+38},type::Control);
        for(size_t i=0;i<tasks.size()&&i<5;++i){int id=200+static_cast<int>(i);float y=RowY(StableKey(id),task.top+48+static_cast<float>(i)*46);D2D1_RECT_F row{task.left+8,y,task.right-40,y+40};AddHit(id,row,tasks[i].title);Fill(Tactile(id,row),tasks[i].done?theme::AccentSoft:theme::Inset,10);
            Text(tasks[i].done?L"\xE73E":L"\xEA3A",{row.left+6,y,row.left+32,y+40},type::Icon,tasks[i].done?(highContrast?theme::SelectedText:theme::Accent):theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);Text(tasks[i].title,{row.left+38,y,row.right-8,y+40},type::Body,tasks[i].done?(highContrast?theme::SelectedText:theme::Muted):theme::Text);FocusRing(id,row);SmallButton(220+static_cast<int>(i),{task.right-36,y+4,task.right-8,y+36},L"\xE711",L"Delete "+tasks[i].title);}
        if(tasks.empty())Paragraph(L"Add up to five tasks. Your draft stays here until it is saved.",{task.left+16,task.top+68,task.right-16,task.top+168});auto input=TaskInputRect();Fill({input.left-4-2*composerFocus.value,input.top-4-2*composerFocus.value,task.right-8+2*composerFocus.value,input.bottom+4+2*composerFocus.value},dock::Mix(theme::CardHover,theme::AccentSoft,composerFocus.value),10);FocusRing(400,{input.left-4,input.top-4,task.right-8,input.bottom+4},10);AddHit(400,input,L"New task, up to 120 characters",tasks.size()<5);
        if(renderMode)Text(tasks.size()>=5?L"Five tasks · delete one to add":L"Add a task…",input,type::Secondary,theme::Muted);SmallButton(AddTask,{task.right-44,input.top,task.right-12,input.bottom},L"\xE72A",L"Add task",tasks.size()<5);EnsureTaskEdit();
    }
    void DrawRing(float x,float y,float radius,float value,unsigned color,float thickness){
        if(layoutOnly)return;
        brush->SetColor(Color(theme::CardHover));target->DrawEllipse(D2D1::Ellipse({x,y},radius,radius),brush.Get(),thickness);
        value=std::clamp(value,0.0f,1.0f);if(value<=0)return;brush->SetColor(Color(color));
        if(value>=0.999f){target->DrawEllipse(D2D1::Ellipse({x,y},radius,radius),brush.Get(),thickness);return;}
        // A small bounded polyline avoids allocating an immutable path every tick.
        constexpr int segments=120;float end=value*6.28318530718f,start=-1.57079632679f;
        D2D1_POINT_2F previous{x,y-radius};int count=std::max(1,static_cast<int>(std::ceil(value*segments)));
        for(int i=1;i<=count;++i){float angle=start+end*i/count;D2D1_POINT_2F point{x+radius*std::cos(angle),y+radius*std::sin(angle)};if(!ringStroke)Check(factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND),nullptr,0,&ringStroke));target->DrawLine(previous,point,brush.Get(),thickness,ringStroke.Get());previous=point;}

    }
    void TextButton(int id,D2D1_RECT_F rect,const std::wstring& text,bool primary=false,bool enabled=true){
        AddHit(id,rect,text,enabled);Fill(Tactile(id,rect),dock::Mix(primary&&enabled?theme::Accent:theme::Inset,primary&&enabled?theme::AccentHover:theme::CardHover,HoverAmount(id)),8);
        Text(text,rect,type::Control,!enabled?theme::Muted:primary?theme::AccentInk:theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(id,rect,8);
    }
    void Stats(){
        auto b=Body();bool wide=LayoutWidth()>=720;float metricsRight=wide?b.left+(b.right-b.left)*0.67f:b.right;
        double values[]{system.totalCpu,hardwareState.gpuPercent,static_cast<double>(system.memoryLoad)};const wchar_t* labels[]{L"CPU",L"GPU",L"RAM"};unsigned colors[]{theme::Accent,theme::Secondary,theme::Tertiary};
        for(int i=0;i<3;++i){float cell=(metricsRight-b.left-24)/3,x=b.left+i*(cell+12);Fill({x,b.top,x+cell,b.top+180},theme::Card,14);bool valid=i==0?system.cpuValid:i==1?hardwareState.gpuValid:system.memoryValid;rings[i].goal=valid?static_cast<float>(values[i]/100):0;if(reducedMotion||renderMode)rings[i].Snap(rings[i].goal);
            AddHit(2020+i,{x,b.top,x+cell,b.top+180},std::wstring(labels[i])+L". "+(valid?Number(values[i])+L" percent":L"Sample unavailable")+L". "+(i==0?hardwareState.cpu:i==1?hardwareState.gpu:Number(system.availableGiB,1)+L" GiB free"),false);
            DrawRing(x+cell/2,b.top+58,std::min(36.0f,cell/2-14),rings[i].value,colors[i],5);Text(!valid?L"—":Number(values[i])+L"%",{x+4,b.top+38,x+cell-4,b.top+76},type::Control,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(labels[i],{x+4,b.top+104,x+cell-4,b.top+128},type::Control,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(i==0?hardwareState.cpu:i==1?(hardwareState.gpuValid?hardwareState.gpu:L"Sample unavailable"):Number(system.availableGiB,1)+L" GiB free",{x+8,b.top+134,x+cell-8,b.top+168},type::Secondary,theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);}
        float sy=b.top+192;AddHit(2023,{b.left,sy,metricsRight,sy+72},(system.diskValid?L"System volume "+system.diskRoot+L". "+Number(system.diskUsedGiB,1)+L" of "+Number(system.diskTotalGiB,1)+L" GiB":L"System volume sample unavailable")+L". Dedicated VRAM capacity "+Number(hardwareState.vramGiB,1)+L" GiB",false);Paragraph((system.diskValid?L"System volume "+system.diskRoot+L" · "+Number(system.diskUsedGiB,1)+L" / "+Number(system.diskTotalGiB,1)+L" GiB":L"System volume sample unavailable")+L"\nDedicated VRAM capacity · "+Number(hardwareState.vramGiB,1)+L" GiB",{b.left+8,sy,metricsRight-8,sy+72});
        D2D1_RECT_F process=wide?D2D1_RECT_F{metricsRight+12,b.top,b.right,b.top+172}:D2D1_RECT_F{b.left,b.top+280,b.right,b.top+450};Fill(process,theme::Card,14);Text(L"Processes · working set",{process.left+12,process.top+8,process.right-12,process.top+32},type::Secondary,theme::Muted);
        for(size_t i=0;i<hardwareState.top.size();++i){const auto& entry=hardwareState.top[i];float y=RowY(L"process:"+(entry.identity?std::to_wstring(entry.identity):entry.name+L":"+std::to_wstring(i)),process.top+40+static_cast<float>(i)*30);AddHit(2030+static_cast<int>(i),{process.left,y,process.right,y+30},entry.name+L". Working set "+Number(entry.workingMiB)+L" MiB",false);Text(entry.name,{process.left+12,y,process.right-80,y+24},type::Secondary);Text(Number(entry.workingMiB)+L" MiB",{process.right-80,y,process.right-12,y+24},type::Secondary,theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);float fraction=static_cast<float>(entry.workingMiB/std::max(1.0,hardwareState.top.front().workingMiB));Line(process.left+12,y+25,process.left+12+(process.right-process.left-24)*fraction,y+25,theme::Tertiary,2);}
        if(hardwareState.top.empty())Paragraph(L"No readable process samples.",{process.left+12,process.top+44,process.right-12,process.bottom-12});float ny=process.bottom+12;Fill({process.left,ny,process.right,ny+86},theme::Inset,14);auto rate=[](double bytes){return bytes>=1048576?Number(bytes/1048576,1)+L" MiB/s":Number(bytes/1024,1)+L" KiB/s";};AddHit(2024,{process.left,ny,process.right,ny+86},system.networkValid?L"Interface transfer. Receive "+rate(system.receiveRate)+L". Send "+rate(system.sendRate):L"Interface transfer sample unavailable",false);Text(L"Interface transfer",{process.left+12,ny+6,process.right-12,ny+30},type::Secondary,theme::Muted);Text(system.networkValid?L"↓ "+rate(system.receiveRate)+L"   ↑ "+rate(system.sendRate):L"Sample unavailable",{process.left+12,ny+36,process.right-12,ny+72},type::Body);
    }
    void ReadClipboard(){
        clipboard.clear();clipboardOffset=0;
        if(!OpenClipboard(window)){clipboardState=L"Clipboard busy · try Refresh";return;}
        clipboardState=CountClipboardFormats()==0?L"Clipboard empty":L"Current item contains no text";
        if(HANDLE data=GetClipboardData(CF_UNICODETEXT))if(const auto* text=static_cast<const wchar_t*>(GlobalLock(data))){size_t capacity=GlobalSize(data)/sizeof(wchar_t),length=wcsnlen_s(text,std::min<size_t>(capacity,8193));bool truncated=length>8192;length=std::min<size_t>(length,8192);if(length&&text[length-1]>=0xD800&&text[length-1]<=0xDBFF)--length;clipboard.assign(text,length);GlobalUnlock(data);clipboardState=truncated?L"Showing first 8,192 UTF-16 units":clipboard.empty()?L"Clipboard text is empty":L"Current text · select or scroll to read";}
        CloseClipboard();if(clipboardEdit)SetWindowTextW(clipboardEdit,clipboard.c_str());
    }
    HWND clipboardEdit=nullptr;
    D2D1_RECT_F ClipboardRect()const{auto b=Body();return {b.left+12,b.top+(Layout().body.Height()>=100?44.0f:0.0f),b.right-12,b.bottom-(Layout().body.Height()>=100?12.0f:0.0f)};}
    void EnsureClipboardEdit(){if(clipboardEdit||renderMode||capturingPane||layoutOnly)return;clipboardEdit=CreateWindowExW(0,L"EDIT",clipboard.c_str(),WS_CHILD|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(403),nullptr,nullptr);LabelInput(clipboardEdit,L"Current clipboard text, read only");EnsureEditResources();SendMessageW(clipboardEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);PositionInputs();}
    void ClipboardView(){auto b=Body();Fill(b,theme::Inset,14);if(Layout().body.Height()>=100)Text(clipboardState,{b.left+12,b.top+4,b.right-12,b.top+36},type::Control);auto rect=ClipboardRect();AddHit(403,rect,L"Current clipboard text, read only");if(renderMode)Paragraph(clipboard.empty()?L"Nothing to preview.":clipboard,rect,theme::Text);EnsureClipboardEdit();}

    void RetargetLayout(){
        if(!expanded)return;dockWidth.goal=SurfaceWidth();dockHeight.goal=SurfaceHeight();
        if(reducedMotion||renderMode){dockWidth.Snap(dockWidth.goal);dockHeight.Snap(dockHeight.goal);width=dockWidth.value;height=dockHeight.value;}
        else if(dockWidth.Moving()||dockHeight.Moving()){transitioning=true;ArmMotion();}
    }
    void FilterApps(bool reset=true){
        filteredApps.clear();if(reset)appPage=0;
        for(size_t i=0;i<apps.size();++i)if(appQuery.empty()||FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE,apps[i].name.c_str(),-1,appQuery.c_str(),-1,nullptr,nullptr,nullptr,0)>=0)filteredApps.push_back(i);
        std::stable_sort(filteredApps.begin(),filteredApps.end(),[this](size_t a,size_t b){int ga=AppGroup(apps[a]),gb=AppGroup(apps[b]);if(ga!=gb)return ga<gb;if(ga==1){auto ai=std::find(appPreferences.recent.begin(),appPreferences.recent.end(),AppKey(apps[a])),bi=std::find(appPreferences.recent.begin(),appPreferences.recent.end(),AppKey(apps[b]));return ai<bi;}return CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE,apps[a].name.c_str(),-1,apps[b].name.c_str(),-1,nullptr,nullptr,0)==CSTR_LESS_THAN;});
        if(std::none_of(filteredApps.begin(),filteredApps.end(),[this](size_t i){return AppKey(apps[i])==selectedAppKey;}))selectedAppKey=filteredApps.empty()?L"":AppKey(apps[filteredApps.front()]);
        railApps.clear();for(size_t i:filteredApps)if(AppGroup(apps[i])<2&&railApps.size()<7)railApps.push_back(i);RepageApps();if(tab==Tab::Apps)RetargetLayout();
    }
    bool CompactApps()const{return Layout().body.Height()<124;}
    bool ShowAppRail()const{return Layout().body.Height()>=280&&!railApps.empty();}
    void RepageApps(){std::vector<int> groups;for(auto index:filteredApps)groups.push_back(AppGroup(apps[index]));auto b=Layout().body;float top=b.top+(CompactApps()?40.0f:56.0f)+(ShowAppRail()?100.0f:0.0f),row=CompactApps()?40.0f:44.0f,heading=CompactApps()?20.0f:24.0f;appPages=layout::Paginate(groups,std::max(row+heading,b.bottom-top),row,heading);if(appPage>=appPages.size())appPage=appPages.empty()?0:appPages.size()-1;}
    size_t AppSlots()const{return appPage<appPages.size()?appPages[appPage].end-appPages[appPage].begin:0;}
    D2D1_RECT_F SearchRect()const{auto b=Body();return {b.left+36,b.top+(CompactApps()?4.0f:8.0f),b.right-12,b.top+(CompactApps()?28.0f:36.0f)};}
    void Apps(){
        RepageApps();auto b=Body();bool compact=CompactApps();Fill({b.left,b.top,b.right,b.top+(compact?32.0f:44.0f)},theme::Card,12);Text(L"\xE721",{b.left+8,b.top+8,b.left+32,b.top+36},type::Icon,theme::Muted);AddHit(401,SearchRect(),L"Search installed apps");EnsureAppSearch();if(renderMode)Text(appQuery.empty()?L"Search installed apps…":appQuery,SearchRect(),type::Body,theme::Muted);float nextY=b.top+(compact?40.0f:56.0f);
        if(ShowAppRail()){
            auto rail=layout::Rail({b.left,nextY+4,b.right,nextY+70},railApps.size(),railPointer.value,reducedMotion?0:railStrength.value);Fill({b.left,nextY,b.right,nextY+88},theme::Inset,18);
            for(size_t i=0;i<railApps.size();++i){int id=1400+static_cast<int>(i);const auto& app=apps[railApps[i]];auto rect=R(rail[i].presented);auto f=feedback.find(StableKey(id));if(f!=feedback.end()){rect.top+=f->second.value*5;rect.bottom+=f->second.value*5;}AddHit(id,rect,L"Launch "+app.name);Fill(Tactile(id,rect),theme::CardHover,12);DrawAppIcon(app,rect);FocusRing(id,rect);if(Hover(id)||focusedKey==StableKey(id))Text(app.name,{b.left,nextY+64,b.right,nextY+88},type::Secondary,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);}
            nextY+=100;
        }
        int group=-1;
        for(size_t slot=0;slot<AppSlots();++slot){size_t index=AppIndex(slot);if(index>=apps.size())break;const auto& entry=apps[index];int current=AppGroup(entry);if(group!=current){group=current;Text(group==0?L"Starred":group==1?L"Recent · launched through Dako":L"Installed applications",{b.left+4,nextY,b.right-4,nextY+(compact?20.0f:24.0f)},type::Secondary,theme::Muted);nextY+=compact?20.0f:24.0f;}
            int id=300+static_cast<int>(slot);float y=RowY(StableKey(id),nextY);nextY+=compact?40.0f:44.0f;D2D1_RECT_F row{b.left,y,b.right-44,y+(compact?36.0f:40.0f)};AddHit(id,row,entry.name);Fill(Tactile(id,row),(Favorite(entry)||AppKey(entry)==selectedAppKey)?theme::AccentSoft:theme::Card,10);DrawAppIcon(entry,{b.left+8,y+6,b.left+36,y+34});Text(entry.name,{b.left+48,y,b.right-56,y+40},type::Body,highContrast&&(Favorite(entry)||AppKey(entry)==selectedAppKey)?theme::SelectedText:theme::Text);FocusRing(id,row);int star=900+static_cast<int>(slot);SmallButton(star,{b.right-36,y+2,b.right,y+38},Favorite(entry)?L"\xE735":L"\xE734",(Favorite(entry)?L"Unstar ":L"Star ")+entry.name);}
        if(filteredApps.empty())Paragraph(apps.empty()?(appsLoading?L"Loading installed apps…":L"No installed applications available. Reopen Apps to retry."):L"No matching apps. Change the search text.",{b.left+12,nextY+16,b.right-12,b.bottom-8});
    }
    std::map<std::wstring,ComPtr<ID2D1Bitmap>> appIcons;
    void DrawAppIcon(const AppEntry& app,D2D1_RECT_F rect){
        if(layoutOnly)return;auto found=appIcons.find(AppKey(app));if(found==appIcons.end()&&app.iconPixels&&!app.iconPixels->empty()){ComPtr<ID2D1Bitmap> icon;target->CreateBitmap(D2D1::SizeU(app.iconWidth,app.iconHeight),app.iconPixels->data(),app.iconWidth*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&icon);if(appIcons.size()>=48)appIcons.erase(appIcons.begin());found=appIcons.emplace(AppKey(app),icon).first;}
        if(found!=appIcons.end()&&found->second)target->DrawBitmap(found->second.Get(),rect);else{Fill(rect,theme::CardHover,8);Text(app.name.substr(0,1),rect,type::Control,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);}
    }
    void SmallButton(int id,D2D1_RECT_F rect,const wchar_t* icon,const std::wstring& label,bool enabled=true) {
        AddHit(id,rect,label,enabled); Fill(Tactile(id,rect),id==AddTask&&enabled?theme::Accent:Hover(id)?theme::CardHover:theme::Card,10);
        Text(icon,rect,type::Icon,id==AddTask&&enabled?theme::AccentInk:enabled?theme::SelectedText:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER); FocusRing(id,rect,10);
    }
    void Wallpaper(){
        auto l=Layout();auto b=Body();TextButton(WallpaperCollection,{b.left,b.top,b.left+100,b.top+32},L"Collection",!customWallpaperFolder);TextButton(WallpaperFolder,{b.left+112,b.top,b.left+224,b.top+32},L"My folder",customWallpaperFolder);
        if(thumbnails.size()!=wallpapers.size())thumbnails.resize(wallpapers.size());
        float focus=std::clamp(wallpaperFocus.value,0.0f,static_cast<float>(wallpapers.empty()?0:wallpapers.size()-1));std::array<size_t,7> order{};size_t count=0;
        int start=std::max(0,static_cast<int>(std::ceil(focus-3))),end=std::min(static_cast<int>(wallpapers.size())-1,static_cast<int>(std::floor(focus+3)));
        for(size_t i=0;i<thumbnails.size();++i)if(std::abs(static_cast<float>(i)-focus)>4)thumbnails[i].Reset();
        for(int i=start;i<=end&&count<order.size();++i)order[count++]=static_cast<size_t>(i);std::sort(order.begin(),order.begin()+count,[&](size_t a,size_t c){return std::abs(static_cast<float>(a)-focus)>std::abs(static_cast<float>(c)-focus);});auto stage=l.stage;
        if(!layoutOnly)target->PushAxisAlignedClip(R(stage),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for(size_t n=0;n<count;++n){size_t index=order[n];float relative=static_cast<float>(index)-focus,distance=std::abs(relative),prominence=std::exp(-distance*distance*2.8f),h=std::max(40.0f,std::min(190.0f,stage.Height()-16)),w=std::min(300.0f,stage.Width()*0.68f);
            // A real filmstrip: every adjacent card retains a 12-DIP gutter,
            // including fractional positions during rapid navigation.
            float center=stage.CenterX()+relative*(w+12),y=stage.top+12+(1-prominence)*12;D2D1_RECT_F rect{center-w/2,y,center+w/2,y+h-12*(1-prominence)};int id=500+static_cast<int>(index);
            AddHit(id,rect,wallpapers[index].title+L", "+std::to_wstring(index+1)+L" of "+std::to_wstring(wallpapers.size())+(index==selectedWallpaper?L", selected":L"")+(wallpapers[index].path==appliedWallpaper?L", applied":L""));
            if(!hits.empty()&&hits.back().id==id){hits.back().clip=R(stage);hits.back().clip.top-=scroll;hits.back().clip.bottom-=scroll;auto c=hits.back().clip;hits.back().clip=R(layout::Intersection({c.left,c.top,c.right,c.bottom},{b.left,b.top,b.right,b.bottom}));}
            if(layoutOnly)continue;const auto& image=wallpapers[index];auto pixels=image.detailPixels?image.detailPixels:image.pixels;UINT iw=image.detailPixels?image.detailWidth:image.width,ih=image.detailPixels?image.detailHeight:image.height;if(!thumbnails[index]&&pixels&&!pixels->empty())target->CreateBitmap(D2D1::SizeU(iw,ih),pixels->data(),iw*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&thumbnails[index]);
            D2D1::Matrix3x2F saved;target->GetTransform(&saved);Fill(rect,theme::CardHover,10);
            if(thumbnails[index]){auto size=thumbnails[index]->GetSize();float ratio=std::max(w/size.width,(rect.bottom-rect.top)/size.height),sw=w/ratio,sh=(rect.bottom-rect.top)/ratio;if(!wallpaperMask)Check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0,0,100,100),6,6),&wallpaperMask));auto layer=D2D1::LayerParameters();layer.geometricMask=wallpaperMask.Get();layer.maskTransform=D2D1::Matrix3x2F::Scale(w/100,(rect.bottom-rect.top)/100)*D2D1::Matrix3x2F::Translation(rect.left,rect.top);target->PushLayer(layer,nullptr);target->DrawBitmap(thumbnails[index].Get(),rect,0.58f+0.42f*prominence,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,D2D1::RectF((size.width-sw)/2,(size.height-sh)/2,(size.width+sw)/2,(size.height+sh)/2));target->PopLayer();}FocusRing(id,rect);target->SetTransform(saved);
        }
        if(!layoutOnly)target->PopAxisAlignedClip();if(wallpapers.empty())Paragraph(wallpaperLoading?L"Loading wallpapers…":L"No wallpapers. Choose another folder.",{stage.left,stage.top+24,stage.right,stage.bottom});
        SmallButton(WallpaperPrevious,R(l.previous),L"\xE76B",L"Previous wallpaper",selectedWallpaper>0);SmallButton(WallpaperNext,R(l.next),L"\xE76C",L"Next wallpaper",selectedWallpaper+1<wallpapers.size());
    }
    void Power(){
        const int actions[]{Lock,SleepNow,RestartNow,ShutdownNow};const wchar_t* labels[]{L"Lock",L"Sleep",L"Restart",L"Shut down"};const wchar_t* icons[]{L"\xE72E",L"\xE708",L"\xE777",L"\xE7E8"};auto b=Layout().body;int cols=Layout().narrow?2:4;
        for(int i=0;i<4;++i){auto r=layout::Column(b,i%cols,cols);r.top=b.top+(i/cols)*100.0f;r.bottom=r.top+88;auto rect=R(r);AddHit(actions[i],rect,labels[i]);Fill(Tactile(actions[i],rect),dock::Mix(theme::Card,theme::CardHover,HoverAmount(actions[i])),14);Text(icons[i],{r.left,r.top+8,r.right,r.top+44},type::Icon,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);Text(labels[i],{r.left,r.top+48,r.right,r.bottom-8},type::Control,theme::Text,DWRITE_TEXT_ALIGNMENT_CENTER);FocusRing(actions[i],rect);}
    }
    void OpenFlyout(Flyout next){
        previousPane.Reset();paneBlend.Snap(1);
        flyout=next;connectionPage=0;selectedWifi=selectedBluetooth=-1;focused=hovered=-1;
        if(wifiPassword)SetWindowTextW(wifiPassword,L"");
        if(!renderMode&&!fixtureGeometry){if(!connectivity)connectivity=std::make_unique<Connectivity>(window);
            if(next==Flyout::Wifi)connectivity->RefreshWifi(true);else connectivity->RefreshBluetooth();}
        SetExpanded(true);
    }
    void EnsurePassword(){
        if(wifiPassword||renderMode||capturingPane||layoutOnly)return;
        wifiPassword=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,0,0,1,1,window,reinterpret_cast<HMENU>(402),nullptr,nullptr);
        LabelInput(wifiPassword,L"Network security key");EnsureEditResources();SendMessageW(wifiPassword,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);SendMessageW(wifiPassword,EM_SETLIMITTEXT,64,0);
        SendMessageW(wifiPassword,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Network security key"));PositionInputs();
    }
    size_t ConnectionSlots()const{
        bool selected=flyout==Flyout::Wifi?selectedWifi>=0:selectedBluetooth>=0;float available=std::max(Layout().body.Height(),Layout().contentHeight);
        return static_cast<size_t>(std::clamp(std::floor((available-76-(selected?156.0f:0.0f))/56),1.0f,5.0f));
    }
    void ConnectionView(){
        auto l=Layout();float w=l.panel.Width();bool wifi=flyout==Flyout::Wifi,busy=wifi?connections.wifiBusy:connections.bluetoothBusy,enabled=wifi?connections.wifiOn:connections.bluetoothOn,available=wifi?connections.wifiAvailable:connections.bluetoothAvailable;
        if(l.emergency)CompactHeader(true);else{
        SmallButton(FlyoutBack,{20,8,56,44},L"\xE72B",L"Back to controls");Text(wifi?L"Wi-Fi":L"Bluetooth",{68,8,w-132,44},type::Control);
        SmallButton(FlyoutRefresh,{w-116,8,w-80,44},L"\xE72C",wifi?L"Refresh networks":L"Find devices",!busy);
        D2D1_RECT_F toggle{w-68,14,w-20,38};AddHit(FlyoutToggle,toggle,enabled?L"Turn radio off":L"Turn radio on",available&&!busy);Fill(toggle,enabled?theme::Accent:theme::CardHover,12);
        togglePosition.goal=enabled?1.0f:0.0f;if(reducedMotion||renderMode)togglePosition.Snap(togglePosition.goal);
        if(!layoutOnly){brush->SetColor(Color(enabled?theme::AccentInk:theme::Muted));target->FillEllipse(D2D1::Ellipse({w-56+24*togglePosition.value,26},8,8),brush.Get());}FocusRing(FlyoutToggle,toggle,12);}
        auto b=Body();D2D1::Matrix3x2F saved;if(!layoutOnly){target->GetTransform(&saved);target->PushAxisAlignedClip(b,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);auto layer=D2D1::LayerParameters();layer.opacity=std::clamp(paneBlend.value,0.0f,1.0f);target->PushLayer(layer,nullptr);target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection*12*(1-paneBlend.value),-scroll)*saved);}buildingBody=true;
        const auto& message=wifi?connections.wifiMessage:connections.bluetoothMessage;AddHit(2040,{b.left,b.top,b.right,b.top+44},message.empty()?(busy?L"Searching":wifi?L"Available networks":L"Your devices"):message,false);Paragraph(message.empty()?(busy?L"Searching…":wifi?L"Available networks":L"Your devices"):message,{b.left,b.top,b.right,b.top+44});
        size_t count=wifi?connections.networks.size():connections.devices.size(),slots=ConnectionSlots();
        for(size_t slot=0;slot<slots&&connectionPage*slots+slot<count;++slot){size_t index=connectionPage*slots+slot;int id=(wifi?1000:1200)+static_cast<int>(index);float y=RowY(StableKey(id),b.top+52+static_cast<float>(slot)*56);bool selected=wifi?static_cast<int>(index)==selectedWifi:static_cast<int>(index)==selectedBluetooth;bool connected=wifi?connections.networks[index].connected:connections.devices[index].connected;
            std::wstring name=wifi?connections.networks[index].name:connections.devices[index].name,status=connected?L"Connected":wifi?(connections.networks[index].secure?L"Secured":L"Open network"):connections.devices[index].paired?L"Paired":L"Ready to pair";
            D2D1_RECT_F row{b.left,y,b.right,y+48};AddHit(id,row,name+L". "+status);Fill(Tactile(id,row),selected?theme::AccentSoft:theme::Card,12);Text(wifi?L"\xE701":L"\xE702",{b.left+12,y+8,b.left+40,y+40},type::Icon,highContrast&&selected?theme::SelectedText:connected?theme::Accent:theme::Muted,DWRITE_TEXT_ALIGNMENT_CENTER);Text(name,{b.left+52,y+2,b.right-12,y+26},type::Body,highContrast&&selected?theme::SelectedText:theme::Text);Text(status,{b.left+52,y+26,b.right-12,y+46},type::Secondary,highContrast&&selected?theme::SelectedText:theme::Muted);FocusRing(id,row);}
        float detail=b.top+52+static_cast<float>(slots)*56+12;
        if(!count)Paragraph(busy?L"Searching…":!available?L"No adapter available. Use More settings.":!enabled?L"Radio is off.":L"No results. Try Refresh.",{b.left+12,b.top+76,b.right-12,b.top+160});
        if(wifi&&selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())){const auto& network=connections.networks[selectedWifi];Fill({b.left,detail,b.right,detail+120},theme::Inset,12);
            bool needs=network.secure&&network.profile.empty()&&!network.connected;
            if(needs){D2D1_RECT_F input{b.left+12,detail+12,b.right-12,detail+12+std::min(32.0f,Layout().body.Height())};Fill(input,theme::CardHover,8);AddHit(402,input,L"Network security key, 8 to 63 characters or 64 hexadecimal digits");EnsurePassword();if(renderMode)Text(L"Network security key",input,type::Secondary,theme::Muted);}
            else Paragraph(network.connected?L"Connected to "+network.name:L"Windows saved profile or open network.",{b.left+12,detail+8,b.right-12,detail+52});
            bool can=network.connected||!needs||(wifiPassword&&GetWindowTextLengthW(wifiPassword)>0);
            TextButton(network.connected?FlyoutDisconnect:FlyoutConnect,{b.right-164,detail+64,b.right-12,detail+104},network.connected?L"Disconnect":connections.wifiConnecting?L"Connecting…":L"Connect",!network.connected,can&&!connections.wifiConnecting&&!busy);
        }
        if(!wifi&&selectedBluetooth>=0&&selectedBluetooth<static_cast<int>(connections.devices.size())){const auto& device=connections.devices[selectedBluetooth];Fill({b.left,detail,b.right,detail+120},theme::Inset,12);Text(device.name,{b.left+12,detail+6,b.right-12,detail+42},type::Body);TextButton(device.connected?FlyoutDisconnect:device.paired?FlyoutConnect:FlyoutPair,{b.right-160,detail+60,b.right-12,detail+100},device.connected?L"Disconnect":device.paired?L"Connect":L"Pair",!device.connected,!busy);if(device.paired)TextButton(FlyoutRemove,{b.left+12,detail+60,b.left+150,detail+100},L"Remove device",false,!busy);}
        buildingBody=false;if(!layoutOnly){target->SetTransform(saved);target->PopLayer();target->PopAxisAlignedClip();}
        if(!l.emergency){auto f=Footer();TextButton(FlyoutSettings,{f.left,f.top,f.right-104,f.bottom},L"More settings ↗");SmallButton(FlyoutPrevious,{f.right-92,f.top,f.right-52,f.bottom},L"\xE76B",L"Previous devices",connectionPage>0);SmallButton(FlyoutNext,{f.right-40,f.top,f.right,f.bottom},L"\xE76C",L"Next devices",(connectionPage+1)*slots<count);}
        if(!l.emergency&&!note.empty())Paragraph(note,R(l.status),theme::Text,type::Caption);else if(l.contentHeight>l.body.Height())Text(L"Scroll for device details",R(l.status),type::Caption,theme::Muted);
    }

    void RefreshConnectionsView(){
        if(!connectivity)return;
        std::wstring networkKey=selectedWifi>=0&&selectedWifi<static_cast<int>(connections.networks.size())?WifiKey(connections.networks[selectedWifi]):L"";
        std::wstring deviceKey=selectedBluetooth>=0&&selectedBluetooth<static_cast<int>(connections.devices.size())?BluetoothKey(connections.devices[selectedBluetooth]):L"";
        connections=connectivity->Snapshot();selectedWifi=selectedBluetooth=-1;
        for(size_t i=0;i<connections.networks.size();++i)if(WifiKey(connections.networks[i])==networkKey)selectedWifi=static_cast<int>(i);
        for(size_t i=0;i<connections.devices.size();++i)if(BluetoothKey(connections.devices[i])==deviceKey)selectedBluetooth=static_cast<int>(i);
        if(selectedWifi<0&&wifiPassword)SetWindowTextW(wifiPassword,L"");
        system.wifi=connections.wifiAvailable?(connections.wifiOn?L"Not connected":L"Off"):L"Not available";system.wifiConnected=false;for(const auto& network:connections.networks)if(network.connected){system.wifi=network.name;system.wifiConnected=true;break;}
        system.bluetooth=connections.bluetoothAvailable?(connections.bluetoothOn?L"On":L"Off"):L"Not available";for(const auto& device:connections.devices)if(device.connected){system.bluetooth=device.name;break;}
        int selected=flyout==Flyout::Wifi?selectedWifi:selectedBluetooth;size_t count=flyout==Flyout::Wifi?connections.networks.size():connections.devices.size(),slots=ConnectionSlots();
        if(selected>=0)connectionPage=static_cast<size_t>(selected)/slots;else if(connectionPage*slots>=count)connectionPage=0;
        RebuildLayout();PositionInputs();if(expanded){ArmMotion();InvalidateRect(window,nullptr,FALSE);}
    }
    void PanelContent(bool header=true){
        hits.clear();buildingBody=false;
        if(!expanded){AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");return;}
        if(miniMode){MediaCard(true);return;}if(flyout!=Flyout::None){ConnectionView();return;}if(header)Tabs();
        auto b=Body();scroll=std::clamp(scroll,0.0f,std::max(0.0f,Layout().contentHeight-(b.bottom-b.top)));D2D1::Matrix3x2F saved;
        if(!layoutOnly){target->GetTransform(&saved);target->PushAxisAlignedClip(b,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);auto layer=D2D1::LayerParameters();layer.opacity=std::clamp(paneBlend.value,0.0f,1.0f);target->PushLayer(layer,nullptr);target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection*12*(1-paneBlend.value),-scroll)*saved);}buildingBody=true;
        switch(tab){case Tab::Controls:Controls();break;case Tab::Today:Today();break;case Tab::Stats:Stats();break;case Tab::Clipboard:ClipboardView();break;case Tab::Apps:Apps();break;case Tab::Wallpaper:Wallpaper();break;case Tab::Power:Power();break;}buildingBody=false;
        if(!layoutOnly){target->SetTransform(saved);target->PopLayer();target->PopAxisAlignedClip();}
        if(!Layout().emergency&&tab==Tab::Apps){auto f=Footer();Text(Number(static_cast<double>(filteredApps.size()))+L" apps",{f.left,f.top,f.right-104,f.bottom},type::Secondary,theme::Muted);SmallButton(AppsPrevious,{f.right-92,f.top,f.right-52,f.bottom},L"\xE76B",L"Previous apps",appPage>0);SmallButton(AppsNext,{f.right-40,f.top,f.right,f.bottom},L"\xE76C",L"Next apps",appPage+1<appPages.size());}
        if(!Layout().emergency&&tab==Tab::Wallpaper){auto f=Footer();TextButton(WallpaperApply,{f.right-180,f.top,f.right,f.bottom},wallpaperApplying?L"Changing…":L"Set wallpaper",true,!wallpapers.empty()&&!wallpaperApplying);if(selectedWallpaper<wallpapers.size())Text(std::to_wstring(selectedWallpaper+1)+L" / "+std::to_wstring(wallpapers.size())+L" · "+wallpapers[selectedWallpaper].title,{f.left,f.top,f.right-192,f.bottom},type::Secondary,theme::Muted);}
        if(!Layout().emergency&&tab==Tab::Clipboard){auto f=Footer();float mid=(f.left+f.right)/2-6;TextButton(ClipboardRefresh,{f.left,f.top,mid,f.bottom},L"Refresh");TextButton(ClipboardClear,{mid+12,f.top,f.right,f.bottom},L"Clear preview");}
        auto status=R(Layout().status);if(!Layout().emergency&&!note.empty()){AddHit(1800,status,note,false);Fill(status,theme::AccentSoft,8);Paragraph(note,{status.left+8,status.top+2,status.right-8,status.bottom-2},highContrast?theme::SelectedText:theme::Text,type::Caption);}else if(tab==Tab::Clipboard)Text(L"Read in memory · no history · cleared when closed",status,type::Secondary,theme::Muted);
        if(!Layout().emergency&&note.empty()){
            Line(status.left,status.top-4,status.right,status.top-4,theme::Border);
            if(tab!=Tab::Clipboard)Text(L"DAKO / "+TabTitle(),status,type::Mono,theme::Muted);
            if(tab!=Tab::Clipboard)Text(Layout().contentHeight>Layout().body.Height()?L"SCROLL FOR MORE":previewMode?L"ISOLATED PREVIEW":L"CTRL + ALT + SPACE",{status.left+160,status.top,status.right,status.bottom},type::Mono,theme::Muted,DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
    void Draw(){
        auto paintStart=steady_clock::now();Graphics();target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());target->Clear(renderMode&&expanded?Color(theme::Surface):D2D1::ColorF(0,0.0f));
        if(renderMode){if(expanded){target->SetTransform(D2D1::Matrix3x2F::Scale(textScale,textScale));PanelContent();}else{hits.clear();AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");DrawNotch();}}
        else{
            if(!YieldResting()&&dockReveal.value<=0.003f)DrawNotch();
            if(dockReveal.value>0.003f){IslandSurface();float left=(envelopeWidth-width)/2;
                target->PushAxisAlignedClip({left,PanelY(),left+width,PanelY()+height},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                target->SetTransform(D2D1::Matrix3x2F::Scale(textScale,textScale)*D2D1::Matrix3x2F::Translation(PanelX(),PanelY()));PanelContent();target->PopAxisAlignedClip();
            }else{hits.clear();AddHit(Expand,{0,0,theme::DockWidth,theme::DockHeight},L"Reveal dock");}
        }
        if(!diagnosticsPath.empty()&&paintTimes.size()<8192)paintTimes.push_back(duration<double,std::milli>(steady_clock::now()-paintStart).count());
        HRESULT result=target->EndDraw();if(result==D2DERR_RECREATE_TARGET){ReleaseGraphics();InvalidateRect(window,nullptr,FALSE);}else{Check(result);if(composition.Active()){if(beforePresent)beforePresent();
            if(FAILED(composition.Present())){ReleaseGraphics();InvalidateRect(window,nullptr,FALSE);}
            else if(frameTickReady){frameTickReady=false;frameClock.Presented();}
        }}PositionInputs();
    }

    bool PresentedContains(float x,float y)const{
        if(renderMode)return x>=0&&x<=width&&y>=0&&y<=height;
        float left=(envelopeWidth-width)/2;
        return dockReveal.value>0.003f&&layout::RoundedContains({left,PanelY(),left+width,PanelY()+height},theme::Radius,x,y);
    }
    int HitAt(float x,float y,bool accessibleHit=false){
        RebuildLayout();
        if(!expanded){float left=renderMode?0:(envelopeWidth-theme::DockWidth)/2;return !YieldResting()&&y>=HandleY()&&y<=HandleY()+theme::DockHeight&&x>=left&&x<=left+theme::DockWidth?0:-1;}
        if(!Actionable()||!PresentedContains(x,y))return -1;x=(x-PanelX())/textScale;y=(y-PanelY())/textScale;
        if(x<0||x>LayoutWidth()||y<0||y>LayoutHeight())return -1;
        for(int i=static_cast<int>(hits.size())-1;i>=0;--i){const auto& h=hits[i];auto r=h.rect,c=h.clip;if((h.enabled||accessibleHit)&&x>=c.left&&x<=c.right&&y>=c.top&&y<=c.bottom&&layout::SkewContains({r.left,r.top,r.right,r.bottom},h.skew,std::min(10.0f,(r.bottom-r.top)/2),x,y))return i;}return -1;
    }
    void SetVolumeAt(int x){auto rect=VolumeRect();system.SetVolume(((x*96.0f/dpi-PanelX())/textScale-rect.left-12)/(rect.right-rect.left-24));volumeMotion.goal=system.volume;ArmMotion();InvalidateRect(window,nullptr,FALSE);}

    void Open(const wchar_t* path) {
        auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",path,nullptr,nullptr,SW_SHOWNORMAL));
        if (result<=32) { note=L"Windows could not open this item."; InvalidateRect(window,nullptr,FALSE); }
    }
    void LabelInput(HWND input,const wchar_t* name){if(accessibleProperties&&input)accessibleProperties->SetHwndPropStr(input,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,PROPID_ACC_NAME,name);}
    void EnsureTaskEdit() {
        if (taskEdit || renderMode||capturingPane||layoutOnly) return;
        taskEdit=CreateWindowExW(0,L"EDIT",taskDraft.c_str(),WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(400),nullptr,nullptr);
        LabelInput(taskEdit,L"New task, up to 120 characters");
        SendMessageW(taskEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Add a task…"));
        SendMessageW(taskEdit,EM_SETLIMITTEXT,120,0);
        EnsureEditResources();
        SendMessageW(taskEdit,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        SendMessageW(taskEdit,EM_SETSEL,draftStart,draftEnd);
        PositionInputs();
    }
    void EnsureEditResources() {
        if (!editFont) editFont=CreateFontW(-static_cast<int>(std::lround(13*textScale*dpi/96)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        if (!editBackground) editBackground=CreateSolidBrush(RGB((theme::CardHover>>16)&255,(theme::CardHover>>8)&255,theme::CardHover&255));
    }
    void EnsureAppSearch() {
        if (appSearch || renderMode||capturingPane||layoutOnly) return;
        appSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(401),nullptr,nullptr);
        LabelInput(appSearch,L"Search installed apps");
        SendMessageW(appSearch,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search installed apps…"));
        SendMessageW(appSearch,EM_SETLIMITTEXT,120,0); EnsureEditResources();
        SendMessageW(appSearch,WM_SETFONT,reinterpret_cast<WPARAM>(editFont),TRUE);
        PositionInputs();
        if (!appQuery.empty()) SetWindowTextW(appSearch,appQuery.c_str());
    }
    bool SaveTasks() {
        if (renderMode||previewMode) return true;
        std::vector<std::pair<std::wstring,bool>> data;
        for (const auto& task:tasks) data.emplace_back(task.title,task.done);
        if (!taskStore.Save(data)){note=L"Could not save tasks. Your draft is kept; try again.";return false;}return true;
    }
    void OpenApp(const AppEntry& app) {
        PIDLIST_ABSOLUTE item=nullptr;
        if (FAILED(SHParseDisplayName(app.parsingName.c_str(),nullptr,&item,0,nullptr))) { note=L"This application is no longer available."; return; }
        SHELLEXECUTEINFOW action{sizeof(action)};
        action.fMask=SEE_MASK_IDLIST|SEE_MASK_NOASYNC; action.hwnd=window; action.lpVerb=L"open"; action.lpIDList=item; action.nShow=SW_SHOWNORMAL;
        if (!ShellExecuteExW(&action)) note=L"Windows could not open this application.";
        else{auto key=AppKey(app);auto& recent=appPreferences.recent;recent.erase(std::remove(recent.begin(),recent.end(),key),recent.end());recent.insert(recent.begin(),key);if(recent.size()>16)recent.resize(16);if(!previewMode&&!appPreferences.Save())note=L"Opened app; could not save recent launches.";FilterApps(false);}
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
        wallpapersRequested=true;wallpaperLoading=true; catalog->DiscoverWallpapers(path); CoTaskMemFree(path);
        note=L"Loading pictures from your folder…";
    }
    void PowerAction(int id) {
        if(renderMode||previewMode){note=L"Power actions are disabled in preview.";return;}
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
        if(id>=FlyoutRefresh&&id<=FlyoutRemove&&id!=FlyoutSettings&&(flyout==Flyout::Wifi?connections.wifiBusy||connections.wifiConnecting:connections.bluetoothBusy))return;
        if(id==1903){InspectDetails();return;}
        if(id==1900||id==1901){OpenCompactMenu(id==1900);return;}
        if(id==1902){ModalGuard guard(modalOpen);MessageBoxW(window,note.empty()?L"No pending status.":note.c_str(),L"Dako status",MB_OK);return;}
        note.clear();
        if(id==400||id==401||id==402||id==403||id==Volume){FocusAt(IndexForKey(StableKey(id)));return;}
        if(id>=1400&&id<1410&&id-1400<static_cast<int>(railApps.size())){OpenApp(apps[railApps[id-1400]]);RebuildLayout();return;}
        if(id>=220&&id<225&&id-220<static_cast<int>(tasks.size())){auto saved=tasks;tasks.erase(tasks.begin()+id-220);if(!SaveTasks())tasks=std::move(saved);RebuildLayout();ArmMotion();InvalidateRect(window,nullptr,FALSE);return;}
        if(id>=900&&id<1000&&AppIndex(id-900)<apps.size()){
            auto key=AppKey(apps[AppIndex(id-900)]);auto& favorites=appPreferences.favorites;auto found=std::find(favorites.begin(),favorites.end(),key);if(found==favorites.end())favorites.push_back(key);else favorites.erase(found);if(!renderMode&&!previewMode&&!appPreferences.Save())note=L"Could not save starred apps. Try again.";FilterApps(false);RebuildLayout();ArmMotion();InvalidateRect(window,nullptr,FALSE);return;
        }
        if(id>=1000&&id<1128&&id-1000<static_cast<int>(connections.networks.size())){
            selectedWifi=id-1000;connectionPage=static_cast<size_t>(selectedWifi)/ConnectionSlots();if(wifiPassword)SetWindowTextW(wifiPassword,L"");
            PositionInputs();InvalidateRect(window,nullptr,FALSE);return;
        }
        if(id>=1200&&id<1328&&id-1200<static_cast<int>(connections.devices.size())){
            selectedBluetooth=id-1200;connectionPage=static_cast<size_t>(selectedBluetooth)/ConnectionSlots();InvalidateRect(window,nullptr,FALSE);return;
        }
        if (id>=100 && id<107) { SetTab(static_cast<Tab>(id-100)); return; }
        if (id>=200 && id<205 && id-200<static_cast<int>(tasks.size())) { tasks[id-200].done=!tasks[id-200].done; if(!SaveTasks())tasks[id-200].done=!tasks[id-200].done; }
        else if (id>=300 && id<400 && AppIndex(id-300)<apps.size()) OpenApp(apps[AppIndex(id-300)]);
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
            else if (wcslen(text)) { tasks.push_back({text,false,nextTask++}); if(SaveTasks()){SetWindowTextW(taskEdit,L"");taskDraft.clear();}else tasks.pop_back(); }
            else if (taskEdit) SetFocus(taskEdit);
            break;
        }
        case ClipboardRefresh: ReadClipboard(); break;
        case ClipboardClear:clipboard.clear();clipboardState=L"Preview cleared";if(clipboardEdit)SetWindowTextW(clipboardEdit,L"");break;
        case WallpaperSettings: Open(L"ms-settings:personalization-background"); break;
        case PowerSettings: Open(L"ms-settings:powersleep"); break;
        case AppsPrevious: if (appPage>0) --appPage; break;
        case AppsNext: if (appPage+1<appPages.size()) ++appPage; break;
        case WallpaperFolder: ChooseWallpaperFolder(); break;
        case WallpaperCollection:
            if (!catalog) catalog=std::make_unique<DesktopCatalog>(window);
            customWallpaperFolder=false;selectedWallpaper=2;wallpaperFocus.Snap(2);
            wallpapersRequested=true;wallpaperLoading=true; catalog->DiscoverWallpapers(); break;
        case WallpaperPrevious: if (selectedWallpaper>0) --selectedWallpaper; break;
        case WallpaperNext: if (selectedWallpaper+1<wallpapers.size()) ++selectedWallpaper; break;
        case WallpaperApply:
            if(selectedWallpaper<wallpapers.size()&&!wallpaperApplying&&!renderMode&&!previewMode){wallpaperApplying=true;pendingWallpaper=wallpapers[selectedWallpaper].path;wallpaperReveal.Freeze(reducedMotion);if(!catalog)catalog=std::make_unique<DesktopCatalog>(window);catalog->ApplyWallpaperAsync(wallpapers[selectedWallpaper].path);}
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
        case Lock: if(!renderMode&&!previewMode){if(!LockWorkStation())note=L"Windows could not lock this session.";}else note=L"Power actions are disabled in preview."; break;
        case Quit: PostMessageW(window,WM_CLOSE,0,0); break;
        }
        wallpaperFocus.goal=static_cast<float>(selectedWallpaper);if(reducedMotion||renderMode)wallpaperFocus.Snap(wallpaperFocus.goal);else if(wallpaperFocus.Moving())ArmMotion();
        RebuildLayout();RequestVisibleIcons();RequestWallpaperDetail();if(!reducedMotion&&!renderMode)ArmMotion();
        NotifyWinEvent(EVENT_OBJECT_STATECHANGE,window,OBJID_CLIENT,CHILDID_SELF);
        InvalidateRect(window,nullptr,FALSE);
    }
    void FocusAt(int index){
        if(index<0||index>=static_cast<int>(hits.size())||!hits[index].enabled)return;keyboardOpen=true;focusedKey=hits[index].key;
        auto h=hits[index];auto b=Body();if(h.clip.top==b.top&&h.clip.bottom==b.bottom){if(h.rect.top<b.top)scroll=std::max(0.0f,scroll+h.rect.top-b.top);else if(h.rect.bottom>b.bottom)scroll+=h.rect.bottom-b.bottom;RebuildLayout();index=IndexForKey(focusedKey);}
        focused=index;
        if(index>=0&&hits[index].id>=300&&hits[index].id<400&&AppIndex(hits[index].id-300)<apps.size())selectedAppKey=AppKey(apps[AppIndex(hits[index].id-300)]);
        if(index>=0){auto& hit=hits[index];if(hit.id==400)EnsureTaskEdit();if(hit.id==401)EnsureAppSearch();if(hit.id==402)EnsurePassword();if(hit.id==403)EnsureClipboardEdit();RebuildLayout();PositionInputs();index=IndexForKey(focusedKey);if(index>=0&&hits[index].input&&IsWindowVisible(hits[index].input))SetFocus(hits[index].input);else SetFocus(window);NotifyWinEvent(EVENT_OBJECT_FOCUS,window,OBJID_CLIENT,h.child);}
        if(index>=0&&hits[index].id>=1400&&hits[index].id<1410){auto rect=hits[index].rect;railPointer.goal=(rect.left+rect.right)/2;railStrength.goal=1;ArmMotion();}
        InvalidateRect(window,nullptr,FALSE);
    }
    void Key(WPARAM key){
        if(key==VK_F2){InspectDetails();return;}
        if((GetKeyState(VK_CONTROL)&0x8000)&&(key==VK_NEXT||key==VK_PRIOR)){SetTab(static_cast<Tab>((static_cast<int>(tab)+(key==VK_NEXT?1:6))%7));return;}
        RebuildLayout();if(key==VK_ESCAPE){if(flyout!=Flyout::None)SetTab(Tab::Controls);else SetExpanded(false);return;}
        int native=GetFocus()==taskEdit?400:GetFocus()==appSearch?401:GetFocus()==wifiPassword?402:GetFocus()==clipboardEdit?403:0;
        if(native)focusedKey=StableKey(native);focused=IndexForKey(focusedKey);
        if(key==VK_TAB&&!hits.empty()){
            int direction=(GetKeyState(VK_SHIFT)&0x8000)?-1:1,index=focused;if(index<0)index=direction>0?-1:0;
            for(size_t i=0;i<hits.size();++i){index=(index+direction+static_cast<int>(hits.size()))%static_cast<int>(hits.size());if(hits[index].enabled){FocusAt(index);break;}}return;
        }
        if(key==VK_NEXT||key==VK_PRIOR){scroll+=key==VK_NEXT?Body().bottom-Body().top:Body().top-Body().bottom;scroll=std::clamp(scroll,0.0f,std::max(0.0f,Layout().contentHeight-Layout().body.Height()));RebuildLayout();PositionInputs();InvalidateRect(window,nullptr,FALSE);return;}
        if(focused<0)return;int id=hits[focused].id;if(!Actionable()&&!(id==Expand&&!expanded))return;
        if(key==VK_DELETE&&id>=200&&id<205){Invoke(id+20);return;}
        if(id>=100&&id<107&&(key==VK_LEFT||key==VK_RIGHT)){int next=(id-100+(key==VK_RIGHT?1:6))%7;SetTab(static_cast<Tab>(next));focusedKey=StableKey(100+next);return;}
        if(tab==Tab::Wallpaper&&flyout==Flyout::None&&(key==VK_LEFT||key==VK_RIGHT)){Invoke(key==VK_LEFT?WallpaperPrevious:WallpaperNext);return;}
        if(id==Volume){if(key==VK_LEFT||key==VK_DOWN)system.SetVolume(system.volume-0.02f);if(key==VK_RIGHT||key==VK_UP)system.SetVolume(system.volume+0.02f);if(key==VK_SPACE)system.ToggleMute();NotifyWinEvent(EVENT_OBJECT_VALUECHANGE,window,OBJID_CLIENT,hits[focused].child);volumeMotion.goal=system.volume;ArmMotion();InvalidateRect(window,nullptr,FALSE);}
        else if(key==VK_RETURN||key==VK_SPACE){auto& f=feedback[focusedKey];f.value=1;f.goal=0;Invoke(id);}
        else if((key==VK_DOWN||key==VK_UP)&&tab==Tab::Apps){int direction=key==VK_DOWN?1:-1;for(int i=focused+direction;i>=0&&i<static_cast<int>(hits.size());i+=direction)if(hits[i].id>=300&&hits[i].id<400){FocusAt(i);break;}}
    }
    void InspectDetails(){
        RebuildLayout();std::wstring detail;
        int index=IndexForKey(focusedKey);if(index<0)index=IndexForKey(hoveredKey);
        if(index>=0)detail=hits[index].name;
        for(const auto& hit:hits)if(hit.id>=2000&&hit.id<2100){if(!detail.empty())detail+=L"\n\n";detail+=hit.name;}
        if(!note.empty()){if(!detail.empty())detail+=L"\n\n";detail+=note;}
        if(detail.empty())detail=L"Use Tab to focus a control, then F2 to read its full name. Ctrl+Page Up/Down changes views.";
        ModalGuard guard(modalOpen);MessageBoxW(window,detail.c_str(),L"Dako — full details",MB_OK|MB_ICONINFORMATION);
    }
    void OpenCompactMenu(bool views){
        ModalGuard guard(modalOpen);HMENU menu=CreatePopupMenu();
        auto add=[&](int id,const std::wstring& label,bool enabled=true){AppendMenuW(menu,MF_STRING|(enabled?MF_ENABLED:MF_GRAYED),static_cast<UINT_PTR>(id),label.c_str());};
        if(views){for(int i=0;i<7;++i){constexpr const wchar_t* names[]{L"Controls",L"Today",L"Stats",L"Clipboard",L"Apps",L"Wallpaper",L"Power"};add(100+i,names[i]);}}
        else if(flyout!=Flyout::None){bool wifi=flyout==Flyout::Wifi,busy=wifi?connections.wifiBusy||connections.wifiConnecting:connections.bluetoothBusy;
            add(FlyoutRefresh,wifi?L"Refresh networks":L"Find devices",!busy);add(FlyoutToggle,L"Toggle radio",!busy);add(FlyoutSettings,L"More settings");add(FlyoutPrevious,L"Previous devices",connectionPage>0);add(FlyoutNext,L"Next devices",(connectionPage+1)*ConnectionSlots()<(wifi?connections.networks.size():connections.devices.size()));
            if(wifi&&selectedWifi>=0){const auto& network=connections.networks[selectedWifi];add(network.connected?FlyoutDisconnect:FlyoutConnect,network.connected?L"Disconnect":L"Connect",!busy);}
            if(!wifi&&selectedBluetooth>=0){const auto& device=connections.devices[selectedBluetooth];add(device.connected?FlyoutDisconnect:device.paired?FlyoutConnect:FlyoutPair,device.connected?L"Disconnect":device.paired?L"Connect":L"Pair",!busy);if(device.paired)add(FlyoutRemove,L"Remove device",!busy);}
        }else switch(tab){
            case Tab::Controls:add(Wifi,L"Wi-Fi");add(Bluetooth,L"Bluetooth");add(Volume,L"Volume");add(Audio,L"Audio settings");break;
            case Tab::Today:add(400,L"New task",tasks.size()<5);add(AddTask,L"Add task",tasks.size()<5);add(FocusStart,focusRunning?L"Pause focus":L"Start focus");add(FocusReset,L"Reset focus");break;
            case Tab::Stats:break;
            case Tab::Clipboard:add(403,L"Read current text");add(ClipboardRefresh,L"Refresh");add(ClipboardClear,L"Clear preview");break;
            case Tab::Apps:add(401,L"Search installed apps");add(AppsPrevious,L"Previous apps",appPage>0);add(AppsNext,L"Next apps",appPage+1<appPages.size());break;
            case Tab::Wallpaper:add(WallpaperPrevious,L"Previous wallpaper",selectedWallpaper>0);add(WallpaperNext,L"Next wallpaper",selectedWallpaper+1<wallpapers.size());add(WallpaperApply,wallpaperApplying?L"Changing…":L"Set wallpaper",!wallpaperApplying&&!wallpapers.empty());add(WallpaperCollection,L"Collection");add(WallpaperFolder,L"Choose folder");break;
            case Tab::Power:add(Lock,L"Lock");add(SleepNow,L"Sleep");add(RestartNow,L"Restart");add(ShutdownNow,L"Shut down");break;
        }
        if(!views){add(1903,L"Full details…\tF2");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add(1902,L"Read status…",!note.empty());add(Quit,L"Exit and restore Windows");}
        RECT bounds{};GetWindowRect(window,&bounds);float scale=dpi/96;int x=bounds.left+static_cast<int>((PanelX()+Layout().header.left*textScale)*scale),y=bounds.top+static_cast<int>((PanelY()+Layout().headerHeight*textScale)*scale);
        SetForegroundWindow(window);int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,x,y,0,window,nullptr);DestroyMenu(menu);if(command)Invoke(command);
    }
    void ContextMenu() {
        ModalGuard modal(modalOpen);
        HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,18,L"Full details…\tF2");AppendMenuW(menu,MF_STRING,1,expanded?L"Hide dock":L"Show dock");
        AppendMenuW(menu,MF_STRING,4,shell.Active()?L"Restore Windows taskbar\tCtrl+Alt+Shift+T":L"Hide Windows taskbar\tCtrl+Alt+Shift+T");
        AppendMenuW(menu,MF_STRING,5,miniMode?L"Full controls":L"Compact media player");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(motionOverride?MF_CHECKED:0),10,L"Reduced motion");
        AppendMenuW(menu,MF_STRING|(opaqueMaterial?MF_CHECKED:0),11,L"Opaque material");
        AppendMenuW(menu,MF_STRING|(systemAppearance?MF_CHECKED:0),12,L"Follow system appearance");
        AppendMenuW(menu,MF_STRING|(!systemAppearance&&lightAppearance?MF_CHECKED:0),13,L"Light appearance");
        AppendMenuW(menu,MF_STRING|(!systemAppearance&&!lightAppearance?MF_CHECKED:0),14,L"Dark appearance");
        AppendMenuW(menu,MF_STRING|(shell.MaximizeNew()?MF_CHECKED:0),15,L"Maximize new windows in desktop mode");
        AppendMenuW(menu,MF_STRING,16,(L"Browser clearance: "+Number(browserClearance)+L" DIP (cycle)").c_str());
        AppendMenuW(menu,MF_STRING|(fixedAccent?MF_CHECKED:0),17,L"Portfolio orange accent");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,2,L"Exit and restore Windows\tCtrl+Alt+Shift+Q");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu); if(command==18)InspectDetails();else if(command==1){SetExpanded(!expanded);keyboardOpen=expanded;}else if(command==2)PostMessageW(window,WM_CLOSE,0,0);
        else if(command==4){if(previewMode){note=L"Desktop policy changes are disabled in preview.";return;}if(shell.Active())shell.Stop();else if(!shell.Start())MessageBoxW(window,L"The recovery process could not start. The Windows taskbar remains available.",L"Dako",MB_OK|MB_ICONERROR);}
        else if(command>=10&&command<=17){
            if(command==10){motionOverride=!motionOverride;reducedMotion=systemReduced||motionOverride;if(reducedMotion)SnapMotion();}
            if(command==11){opaqueMaterial=!opaqueMaterial;ReleaseGraphics();}
            if(command==12)systemAppearance=true;
            if(command==13){systemAppearance=false;lightAppearance=true;}
            if(command==14){systemAppearance=false;lightAppearance=false;}
            if(command==15)shell.SetMaximizeNew(!shell.MaximizeNew());
            if(command==16)browserClearance=browserClearance>=192?112:browserClearance+40;
            if(command==17)fixedAccent=!fixedAccent;
            if(!SaveSettings())note=L"Could not save settings. Changes apply to this session.";ConfigureAppearance();RefreshGeometry();
        }
        else if(command==5){miniMode=!miniMode;tab=Tab::Controls;flyout=Flyout::None;SetExpanded(true);keyboardOpen=true;}
    }
    // A deterministic render target for visual QA of our own UI, without capturing the desktop.
    void SaveRender(const std::wstring& path) {
        ReleaseGraphics();
        ComPtr<IWICImagingFactory> images;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
        ComPtr<IWICBitmap> bitmap;
        UINT imageWidth=static_cast<UINT>(std::lround((renderMode?width:envelopeWidth)*dpi/96)),imageHeight=static_cast<UINT>(std::lround((renderMode?height:envelopeHeight)*dpi/96));
        Check(images->CreateBitmap(imageWidth,imageHeight,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        Check(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),dpi,dpi),&target));
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
    void AssertScene(){
        RebuildLayout();auto l=Layout();
        if(SurfaceWidth()>envelopeWidth+0.1f||SurfaceHeight()>envelopeHeight-PanelY()+0.1f)winrt::throw_hresult(E_UNEXPECTED);
        if(expanded&&!miniMode&&l.emergency){bool actions=false;for(const auto& h:hits)if(h.id==1901&&VisibleHit(h))actions=true;if(!actions)winrt::throw_hresult(E_UNEXPECTED);}
        for(const auto& h:hits){if(h.key.empty()||h.child<=0)winrt::throw_hresult(E_UNEXPECTED);if(h.clip.left<0||h.clip.top<0||h.clip.right>LayoutWidth()+0.1f||h.clip.bottom>LayoutHeight()+0.1f)winrt::throw_hresult(E_UNEXPECTED);}
        for(size_t i=0;i<hits.size();++i)for(size_t j=i+1;j<hits.size();++j){if(hits[i].key==hits[j].key)winrt::throw_hresult(E_UNEXPECTED);}
        if(tab==Tab::Apps&&!filteredApps.empty()&&flyout==Flyout::None&&!Layout().emergency){auto first=std::find_if(hits.begin(),hits.end(),[](const Hit& h){return h.id==300;});if(first==hits.end()||first->rect.bottom>Body().bottom+0.1f)winrt::throw_hresult(E_UNEXPECTED);}
        if(flyout==Flyout::None&&!miniMode&&expanded&&!Layout().emergency){for(int i=0;i<7;++i){auto rect=TabRect(i);int index=HitAt((rect.left+rect.right)/2*textScale,(rect.top+rect.bottom)/2*textScale);if(index<0||hits[index].id!=100+i)winrt::throw_hresult(E_UNEXPECTED);}}
    }
    void RenderSuite(const std::wstring& directory){
        fixtureGeometry=true;reducedMotion=true;dpi=96;envelopeWidth=900;envelopeHeight=760;systemAppearance=false;lightAppearance=true;ConfigureAppearance();
        hardwareState.cpu=L"Fixture processor";hardwareState.gpu=L"Fixture graphics";hardwareState.gpuValid=true;hardwareState.gpuPercent=12;hardwareState.vramGiB=8;hardwareState.totalGiB=32;hardwareState.processes=229;hardwareState.top={{L"Browser",1400,101},{L"Browser",820,102},{L"Music",410,103},{L"Terminal",300,104}};system.cpuValid=system.memoryValid=system.diskValid=system.networkValid=true;system.totalCpu=37;system.memoryLoad=61;system.availableGiB=12.5;system.diskRoot=L"C:\\";system.diskTotalGiB=512;system.diskUsedGiB=256;system.volume=0.42f;system.wifi=L"Fixture network";system.wifiConnected=true;system.bluetooth=L"Fixture headphones";system.audioName=L"Fixture audio output";
        wallpapers=DesktopCatalog::LoadWallpapers(DesktopCatalog::BundledWallpaperRoot());if(wallpapers.size()!=18)winrt::throw_hresult(E_UNEXPECTED);auto bundled=wallpapers;
        for(int i=0;i<5;++i)tasks.push_back({i==0?L"A long task title with mixed scripts · 日本語 · emoji ✨ that must trim safely":L"Task "+std::to_wstring(i+1),i==1,nextTask++});
        for(int i=0;i<108;++i)apps.emplace_back(L"Application "+std::to_wstring(i+1),L"fixture:"+std::to_wstring(i));appPreferences.favorites={L"fixture:0",L"fixture:4",L"fixture:7"};appPreferences.recent={L"fixture:6",L"fixture:5",L"fixture:2"};FilterApps();
        tab=Tab::Apps;expanded=true;appQuery=L"Application 108";FilterApps();if(dockHeight.goal!=SurfaceHeight())winrt::throw_hresult(E_UNEXPECTED);appQuery.clear();FilterApps();if(dockHeight.goal!=SurfaceHeight())winrt::throw_hresult(E_UNEXPECTED);expanded=false;
        clipboard=L"Current text preview\r\nMultiple lines, mixed scripts 日本語 and emoji ✨.\r\nThis reader has native scrolling and selection. No history is collected.";clipboardState=L"Current text · select or scroll to read";
        constexpr const wchar_t* names[]{L"controls",L"today",L"stats",L"clipboard",L"apps",L"wallpaper",L"power"};size_t captures=0;
        std::wofstream report(std::filesystem::path(directory)/L"verification.txt",std::ios::trunc);
        auto capture=[&](const std::wstring& name){report<<"render "<<name<<std::endl;width=SurfaceWidth();height=SurfaceHeight();dockWidth.Snap(width);dockHeight.Snap(height);dockReveal.Snap(1);expanded=true;transitioning=false;paneBlend.Snap(1);SaveRender(directory+L"\\"+name+L".png");AssertScene();++captures;};
        for(int i=0;i<7;++i){tab=static_cast<Tab>(i);scroll=0;capture(names[i]);}
        tab=Tab::Wallpaper;selectedWallpaper=wallpapers.size()-1;wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));capture(L"wallpaper-last");
        note=L"Could not apply wallpaper. Choose another image.";capture(L"wallpaper-error");note.clear();
        auto sixtyFour=bundled;while(sixtyFour.size()<64){auto entry=bundled[sixtyFour.size()%bundled.size()];entry.path=L"fixture:"+std::to_wstring(sixtyFour.size());sixtyFour.push_back(entry);}ReplaceWallpapers(sixtyFour);selectedWallpaper=63;wallpaperFocus.Snap(63);capture(L"wallpaper-64");ReplaceWallpapers({bundled.front()});if(selectedWallpaper!=0||wallpaperFocus.value!=0)winrt::throw_hresult(E_UNEXPECTED);capture(L"wallpaper-one");ReplaceWallpapers({});if(selectedWallpaper!=0||wallpaperFocus.value!=0)winrt::throw_hresult(E_UNEXPECTED);note=L"No pictures found. Choose another folder.";capture(L"wallpaper-empty");note.clear();ReplaceWallpapers(bundled);
        connections.wifiAvailable=connections.wifiOn=connections.bluetoothAvailable=connections.bluetoothOn=true;
        WifiNetwork home;home.name=L"Fixture secured network with a long visible name";home.secure=home.connectable=true;home.quality=92;connections.networks={home};flyout=Flyout::Wifi;selectedWifi=0;capture(L"wifi-password");connections.networks[0].connected=true;connections.networks[0].profile=L"Fixture";capture(L"wifi");connections.wifiMessage=L"Credentials rejected. Try again or choose More settings.";capture(L"wifi-error");connections.wifiMessage.clear();
        BluetoothEntry device;device.name=L"Fixture headphones";device.paired=device.connected=true;device.native.Address.ullLong=1;connections.devices={device};flyout=Flyout::Bluetooth;selectedBluetooth=0;capture(L"bluetooth");connections.bluetoothBusy=true;connections.bluetoothMessage=L"Searching for devices…";capture(L"bluetooth-busy");connections.bluetoothBusy=false;connections.bluetoothMessage.clear();flyout=Flyout::None;
        miniMode=true;tab=Tab::Controls;currentMedia.available=currentMedia.canPrevious=currentMedia.canPlay=currentMedia.canNext=true;currentMedia.playing=true;currentMedia.title=L"Fixture music";currentMedia.artist=L"Fixture artist";currentMedia.position=42;currentMedia.duration=180;capture(L"media-mini");miniMode=false;
        fixedAccent=false;SetAccent(0x83B8DE);capture(L"theme-blue");SetAccent(0xD898BC);capture(L"theme-pink");fixedAccent=true;ConfigureAppearance();capture(L"theme-light");lightAppearance=false;ConfigureAppearance();capture(L"theme-dark");ConfigureAppearance(true);capture(L"theme-high-contrast");lightAppearance=true;ConfigureAppearance();
        tab=Tab::Controls;system.volume=0;capture(L"controls-volume-zero");system.volume=.01f;capture(L"controls-volume-one");system.volume=1;capture(L"controls-volume-full");system.muted=true;capture(L"controls-muted");system.muted=false;fixtureAudioAvailable=false;system.volume=0;system.audioName=L"No audio device";capture(L"controls-no-audio");fixtureAudioAvailable=true;system.audioName=L"Fixture audio output";system.volume=.42f;
        // Pairwise edge states use inert data, never the user's clipboard or devices.
        auto savedConnections=connections;
        flyout=Flyout::Wifi;selectedWifi=-1;capture(L"wifi-unselected");connections.networks.clear();connections.wifiOn=false;connections.wifiMessage=L"Wi-Fi is switched off. Use Actions or More settings.";capture(L"wifi-radio-off");connections.wifiAvailable=false;connections.wifiMessage=L"No adapter available. Use More settings.";capture(L"wifi-no-adapter");connections.wifiAvailable=true;connections.wifiMessage=L"Windows denied access. Choose More settings to review permissions.";capture(L"wifi-permission-error");connections=savedConnections;
        flyout=Flyout::Bluetooth;selectedBluetooth=0;connections.devices[0].paired=connections.devices[0].connected=false;capture(L"bluetooth-unpaired");connections.bluetoothMessage=L"Pairing canceled. Try again or choose More settings.";capture(L"bluetooth-error");connections.devices.clear();selectedBluetooth=-1;connections.bluetoothMessage=L"No results. Try Refresh.";capture(L"bluetooth-empty");connections.bluetoothOn=false;connections.bluetoothMessage=L"Bluetooth is switched off.";capture(L"bluetooth-radio-off");connections.bluetoothAvailable=false;connections.bluetoothMessage=L"No adapter available. Use More settings.";capture(L"bluetooth-no-adapter");connections=savedConnections;flyout=Flyout::None;
        auto savedTasks=tasks;tasks.clear();tab=Tab::Today;capture(L"today-empty");tasks=savedTasks;
        tab=Tab::Apps;appQuery=L"No matching fixture application";FilterApps();capture(L"apps-empty");appQuery.clear();FilterApps();
        tab=Tab::Clipboard;auto savedClipboard=clipboard;auto savedClipboardState=clipboardState;clipboard.clear();clipboardState=L"Clipboard empty";capture(L"clipboard-empty");clipboard=std::wstring(8192,L'x');clipboardState=L"Showing first 8,192 UTF-16 units. Scroll or select to read.";capture(L"clipboard-long");clipboard=savedClipboard;clipboardState=savedClipboardState;
        tab=Tab::Wallpaper;auto savedWallpapers=wallpapers;ReplaceWallpapers({});wallpaperLoading=true;capture(L"wallpaper-loading");wallpaperLoading=false;ReplaceWallpapers(savedWallpapers);wallpaperApplying=true;capture(L"wallpaper-applying");wallpaperApplying=false;
        tab=Tab::Controls;miniMode=true;auto savedMedia=currentMedia;currentMedia=MediaState{};capture(L"media-unavailable");currentMedia=savedMedia;currentMedia.duration=0;capture(L"media-live-stream");currentMedia.duration=180;currentMedia.playing=false;capture(L"media-paused");currentMedia.title=L"Long media title with mixed scripts \u65e5\u672c\u8a9e and emoji \U0001F3B5 that remains available in full details";capture(L"media-long");currentMedia=savedMedia;miniMode=false;
        for(float scale:{1.0f,1.25f,1.5f,1.75f,2.0f}){dpi=96*scale;envelopeWidth=std::min(900.0f,1366/scale);envelopeHeight=std::min(760.0f,768/scale);for(int i=0;i<7;++i){tab=static_cast<Tab>(i);scroll=0;note=i==5?L"Fixture error stays below the action footer.":L"";capture(std::wstring(L"matrix-")+names[i]+L"-"+std::to_wstring(static_cast<int>(scale*100)));}tab=Tab::Today;scroll=10000;capture(L"matrix-today-scrolled-"+std::to_wstring(static_cast<int>(scale*100)));}
        dpi=192;envelopeWidth=683;envelopeHeight=272;scroll=0;note.clear();tab=Tab::Apps;capture(L"browser-apps-200");
        flyout=Flyout::Wifi;selectedWifi=0;connections.networks[0].connected=false;connections.networks[0].profile.clear();capture(L"browser-wifi-200");flyout=Flyout::None;
        dpi=96;envelopeWidth=900;envelopeHeight=760;scroll=0;note.clear();tab=Tab::Controls;capture(L"controls-final");
        // Input identity must survive order changes without activating the replacement.
        tab=Tab::Apps;RebuildLayout();auto first=std::find_if(hits.begin(),hits.end(),[](const Hit& h){return h.id==300;});if(first==hits.end())winrt::throw_hresult(E_UNEXPECTED);pressedKey=first->key;pressedId=300;appQuery=L"Application 108";FilterApps();RebuildLayout();if(!pressedKey.empty())winrt::throw_hresult(E_UNEXPECTED);appQuery.clear();FilterApps();
        for(float text:{1.25f,1.5f,2.0f,2.25f}){
            textScale=text;dpi=192;envelopeWidth=683;envelopeHeight=272;scroll=0;flyout=Flyout::None;note=L"Could not complete this operation. Open Actions and read status to retry.";
            for(int i=0;i<7;++i){tab=static_cast<Tab>(i);scroll=0;capture(L"text-"+std::to_wstring(static_cast<int>(text*100))+L"-"+names[i]);}
            flyout=Flyout::Wifi;selectedWifi=0;scroll=0;capture(L"text-"+std::to_wstring(static_cast<int>(text*100))+L"-wifi");flyout=Flyout::None;
            miniMode=true;capture(L"text-"+std::to_wstring(static_cast<int>(text*100))+L"-mini");miniMode=false;
        }
        textScale=1;dpi=96;envelopeWidth=900;envelopeHeight=760;note.clear();
        // Hidden native EDIT verification is separate from software pixels.
        tab=Tab::Today;renderMode=false;previewMode=true;dpi=96;scroll=0;RebuildLayout();EnsureTaskEdit();
        wchar_t cue[64]{};if(!SendMessageW(taskEdit,EM_GETCUEBANNER,reinterpret_cast<WPARAM>(cue),64)||std::wstring(cue)!=L"Add a task…")winrt::throw_hresult(E_UNEXPECTED);
        SetWindowTextW(taskEdit,L"V2 native draft — 日本語");SendMessageW(taskEdit,EM_SETSEL,3,8);RECT suggested{};GetWindowRect(window,&suggested);
        SendMessageW(window,WM_DPICHANGED,MAKELONG(144,144),reinterpret_cast<LPARAM>(&suggested));
        wchar_t draft[121]{};GetWindowTextW(taskEdit,draft,121);DWORD start=0,end=0;SendMessageW(taskEdit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        if(std::wstring(draft)!=L"V2 native draft — 日本語"||start!=3||end!=8)winrt::throw_hresult(E_UNEXPECTED);
        SendMessageW(window,WM_DPICHANGED,MAKELONG(96,96),reinterpret_cast<LPARAM>(&suggested));DestroyWindow(taskEdit);taskEdit=nullptr;taskDraft.clear();renderMode=true;previewMode=false;
        TaskStore store((std::filesystem::path(directory)/L"tasks-fixture.ini").wstring());std::vector<std::pair<std::wstring,bool>> saved{{L"Mixed scripts 日本語 ✨",false},{L"Completed",true}};
        if(!store.Save(saved)||store.Load()!=saved)winrt::throw_hresult(E_UNEXPECTED);
        TaskStore failure((std::filesystem::path(directory)/L"verification.txt"/L"tasks.ini").wstring());if(failure.Save(saved))winrt::throw_hresult(E_UNEXPECTED);
        report<<"PASS native EDIT draft/caret/selection across synthetic DPI transaction; atomic task round trip and injected save failure."<<std::endl;
        report<<"PASS captures="<<captures<<"; geometry, stable identity, catalog replacement, transformed scene and 5 DPI tiers. Software fixtures; no desktop/hardware sign-off."<<std::endl;
        tab=Tab::Controls;SetExpanded(false);SaveRender(directory+L"\\compact.png");
    }

};

// Standard MSAA children let Windows' UI Automation bridge expose the custom-drawn
// controls as named, focusable buttons and a slider instead of an inaccessible canvas.
class IslandAccessible final : public IAccessible,public IEnumVARIANT {
    ULONG cursor_=0;
    std::atomic<ULONG> references_{1};
    HWND window_;
    Island* App() const {
        wchar_t className[64]{};
        if (!IsWindow(window_) || !GetClassNameW(window_,className,64) || wcscmp(className,WindowClass)) return nullptr;
        return reinterpret_cast<Island*>(GetWindowLongPtrW(window_,GWLP_USERDATA));
    }
    int Child(VARIANT value,Island* app) const {
        return app&&value.vt==VT_I4&&value.lVal>0?app->IndexForChild(value.lVal):-1;
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
        if(id==IID_IEnumVARIANT){*output=static_cast<IEnumVARIANT*>(this);AddRef();return S_OK;}
        *output=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count,VARIANT* values,ULONG* fetched)override{
        if(!values||(!fetched&&count!=1))return E_POINTER;auto* app=App();ULONG n=0;
        while(app&&n<count&&cursor_<app->hits.size()){VariantInit(&values[n]);values[n].vt=VT_I4;values[n].lVal=app->hits[cursor_++].child;++n;}if(fetched)*fetched=n;return n==count?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count)override{auto* app=App();ULONG size=app?static_cast<ULONG>(app->hits.size()):0,next=std::min(size,cursor_+count);bool full=next-cursor_==count;cursor_=next;return full?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset()override{cursor_=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT** output)override{if(!output)return E_POINTER;auto* copy=new IslandAccessible(window_);copy->cursor_=cursor_;*output=static_cast<IEnumVARIANT*>(copy);return S_OK;}
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
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT value,IDispatch** child)override{
        if(!child)return E_POINTER;*child=nullptr;auto* app=App();int i=Child(value,app);
        if(i>=0&&app->hits[i].input)return AccessibleObjectFromWindow(app->hits[i].input,static_cast<DWORD>(OBJID_CLIENT),IID_IDispatch,reinterpret_cast<void**>(child));return S_FALSE;
    }
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
        if (index>=0 && app->hits[index].id==Volume) return String(description,L"System volume, minimum 0, maximum 100 percent. Left and right arrows adjust by two percent. Space mutes.");
        if(index>=0)return String(description,L"F2 reads full details. Tab moves between controls. Page Down/Up scrolls content.");
        return String(description,(app?app->note:L"")+L" Use Tab to move between controls, Enter or Space to activate, and Escape to collapse. Ctrl+Page Down/Up changes views.");
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child,VARIANT* role) override {
        if (!role) return E_POINTER; VariantInit(role); role->vt=VT_I4;
        auto* app=App(); int index=Child(child,app);
        int id=index>=0?app->hits[index].id:0;
        role->lVal=index<0?ROLE_SYSTEM_PANE:(id==1800||(id>=2000&&id<2100))?ROLE_SYSTEM_STATICTEXT:id>=100&&id<107?ROLE_SYSTEM_PAGETAB:id==Volume?ROLE_SYSTEM_SLIDER:(id>=200&&id<205)||(id>=900&&id<1000)||id==FlyoutToggle?ROLE_SYSTEM_CHECKBUTTON:id>=400&&id<=403?ROLE_SYSTEM_TEXT:(id>=500&&id<564)||(id>=300&&id<400)||(id>=1000&&id<1328)?ROLE_SYSTEM_LISTITEM:ROLE_SYSTEM_PUSHBUTTON;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child,VARIANT* state) override {
        if (!state) return E_POINTER; VariantInit(state); state->vt=VT_I4; state->lVal=0;
        auto* app=App(); int index=Child(child,app);
        if (index>=0) {
            int hitId=app->hits[index].id;bool reading=hitId==1800||(hitId>=2000&&hitId<2100);state->lVal=reading?STATE_SYSTEM_READONLY:STATE_SYSTEM_FOCUSABLE;
            if(!app->VisibleHit(app->hits[index])||(!app->Actionable()&&!(app->hits[index].id==Expand&&!app->expanded&&!app->YieldResting())))state->lVal|=STATE_SYSTEM_OFFSCREEN;
            if (!app->hits[index].enabled&&!reading) state->lVal|=STATE_SYSTEM_UNAVAILABLE;
            if (app->focused==index && GetFocus()==window_) state->lVal|=STATE_SYSTEM_FOCUSED;
            int id=app->hits[index].id;
            if (id>=100 && id<107 && id-100==static_cast<int>(app->tab)) state->lVal|=STATE_SYSTEM_SELECTED;
            if (id>=200 && id<205 && id-200<static_cast<int>(app->tasks.size()) && app->tasks[id-200].done) state->lVal|=STATE_SYSTEM_CHECKED;
            if(id>=900&&id<1000&&app->AppIndex(id-900)<app->apps.size()&&app->Favorite(app->apps[app->AppIndex(id-900)]))state->lVal|=STATE_SYSTEM_CHECKED;
            if(id==FlyoutToggle&&(app->flyout==Flyout::Wifi?app->connections.wifiOn:app->connections.bluetoothOn))state->lVal|=STATE_SYSTEM_CHECKED;
            if(id>=500&&id<564&&id-500==static_cast<int>(app->selectedWallpaper))state->lVal|=STATE_SYSTEM_SELECTED;
            if(id>=300&&id<400&&app->AppIndex(id-300)<app->apps.size()&&app->AppKey(app->apps[app->AppIndex(id-300)])==app->selectedAppKey)state->lVal|=STATE_SYSTEM_SELECTED;
            if((id>=1000&&id<1128&&id-1000==app->selectedWifi)||(id>=1200&&id<1328&&id-1200==app->selectedBluetooth))state->lVal|=STATE_SYSTEM_SELECTED;
            if(id==402)state->lVal|=STATE_SYSTEM_PROTECTED;
            if(id==403)state->lVal|=STATE_SYSTEM_READONLY;
            if((id==WallpaperApply&&app->wallpaperApplying)||(id==FlyoutConnect&&app->connections.wifiConnecting))state->lVal|=STATE_SYSTEM_BUSY;
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
        if (GetFocus()==window_) { focus->vt=VT_I4; focus->lVal=app && app->focused>=0?app->hits[app->focused].child:CHILDID_SELF; }
        else if(app)for(const auto& hit:app->hits)if(hit.input&&hit.input==GetFocus()){focus->vt=VT_I4;focus->lVal=hit.child;break;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection)override{
        if(!selection)return E_POINTER;VariantInit(selection);auto* app=App();if(!app)return S_FALSE;
        int id=app->flyout==Flyout::Wifi?1000+app->selectedWifi:app->flyout==Flyout::Bluetooth?1200+app->selectedBluetooth:app->tab==Tab::Wallpaper?500+static_cast<int>(app->selectedWallpaper):100+static_cast<int>(app->tab);
        if(app->tab==Tab::Apps&&app->flyout==Flyout::None)for(const auto& hit:app->hits)if(hit.id>=300&&hit.id<400&&app->AppIndex(hit.id-300)<app->apps.size()&&app->AppKey(app->apps[app->AppIndex(hit.id-300)])==app->selectedAppKey){id=hit.id;break;}
        for(const auto& hit:app->hits)if(hit.id==id){selection->vt=VT_I4;selection->lVal=hit.child;return S_OK;}return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child,BSTR* action) override {
        if (!action) return E_POINTER; *action=nullptr; auto* app=App(); int index=Child(child,app);
        return index>=0&&app->hits[index].enabled?String(action,app->hits[index].id==Volume?L"Adjust":L"Press"):S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags,VARIANT child) override {
        auto* app=App(); int index=Child(child,app);
        if (index<0 || !app->hits[index].enabled || !(flags&SELFLAG_TAKEFOCUS)) return E_INVALIDARG;
        PostMessageW(window_,FocusControl,app->hits[index].child,0); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accLocation(LONG* left,LONG* top,LONG* width,LONG* height,VARIANT child) override {
        if (!left || !top || !width || !height) return E_POINTER;
        auto* app=App(); int index=Child(child,app); RECT bounds{}; GetWindowRect(window_,&bounds);
        if (index>=0) {
            auto r=app->hits[index].rect;const auto& h=app->hits[index];float extra=std::abs(h.skew)*(r.bottom-r.top)/2;r.left-=extra;r.right+=extra;
            auto clipped=layout::Intersection({r.left,r.top,r.right,r.bottom},{h.clip.left,h.clip.top,h.clip.right,h.clip.bottom});r=Island::R(clipped);
            float scale=app->dpi/96.0f,logicalScale=app->expanded?app->textScale:1.0f;
            float originX=app->expanded?app->PanelX():app->renderMode?0:(app->envelopeWidth-theme::DockWidth)/2;
            float originY=app->expanded?app->PanelY():app->HandleY();
            *left=bounds.left+static_cast<LONG>((originX+r.left*logicalScale)*scale); *top=bounds.top+static_cast<LONG>((originY+r.top*logicalScale)*scale);
            *width=static_cast<LONG>((r.right-r.left)*logicalScale*scale); *height=static_cast<LONG>((r.bottom-r.top)*logicalScale*scale);
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
        if (destination<0) return S_FALSE; end->vt=VT_I4; end->lVal=app->hits[destination].child; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x,LONG y,VARIANT* child) override {
        if (!child) return E_POINTER; VariantInit(child); auto* app=App(); if (!app) return S_FALSE;
        POINT point{x,y}; ScreenToClient(window_,&point);
        int index=app->HitAt(point.x*96.0f/app->dpi,point.y*96.0f/app->dpi,true);
        child->vt=VT_I4; child->lVal=index>=0?app->hits[index].child:CHILDID_SELF; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        auto* app=App(); int index=Child(child,app);
        if (index<0 || !app->hits[index].enabled||!app->CanInvoke(app->hits[index])) return E_INVALIDARG;
        PostMessageW(window_,InvokeControl,app->hits[index].child,0); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT,BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT,BSTR) override { return E_NOTIMPL; }
};

void VerifyAccessibility(Island& app){
    app.accessible.Attach(new IslandAccessible(app.window));app.tab=Tab::Controls;app.SetExpanded(true);app.RebuildLayout();
    auto verify=[&](int action,LONG role,LONG required){
        auto found=std::find_if(app.hits.begin(),app.hits.end(),[&](const Hit& h){return h.id==action;});if(found==app.hits.end())winrt::throw_hresult(E_UNEXPECTED);
        VARIANT child{},actual{},state{};child.vt=VT_I4;child.lVal=found->child;Check(app.accessible->get_accRole(child,&actual));Check(app.accessible->get_accState(child,&state));if(actual.lVal!=role||(state.lVal&required)!=required)winrt::throw_hresult(E_UNEXPECTED);
    };
    verify(100,ROLE_SYSTEM_PAGETAB,STATE_SYSTEM_SELECTED);verify(Volume,ROLE_SYSTEM_SLIDER,STATE_SYSTEM_FOCUSABLE);
    app.tab=Tab::Today;app.RebuildLayout();verify(2001,ROLE_SYSTEM_STATICTEXT,STATE_SYSTEM_READONLY);verify(2002,ROLE_SYSTEM_STATICTEXT,STATE_SYSTEM_READONLY);verify(201,ROLE_SYSTEM_CHECKBUTTON,STATE_SYSTEM_CHECKED);verify(400,ROLE_SYSTEM_TEXT,STATE_SYSTEM_FOCUSABLE);
    app.tab=Tab::Stats;app.RebuildLayout();verify(2020,ROLE_SYSTEM_STATICTEXT,STATE_SYSTEM_READONLY);verify(2024,ROLE_SYSTEM_STATICTEXT,STATE_SYSTEM_READONLY);
    app.tab=Tab::Apps;app.RebuildLayout();verify(900,ROLE_SYSTEM_CHECKBUTTON,STATE_SYSTEM_CHECKED);
    ComPtr<IEnumVARIANT> enumeration;Check(app.accessible.As(&enumeration));ULONG fetched=0;VARIANT value{};Check(enumeration->Next(1,&value,&fetched));if(fetched!=1||app.IndexForChild(value.lVal)<0)winrt::throw_hresult(E_UNEXPECTED);
    app.SetExpanded(false);app.RebuildLayout();
}

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
            if(app->composition.Active()&&FAILED(app->composition.Resize(LOWORD(lparam),HIWORD(lparam),app->dpi)))app->ReleaseGraphics();
            if (app->hostTarget) app->hostTarget->Resize(D2D1::SizeU(LOWORD(lparam),HIWORD(lparam)));
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_DPICHANGED:{
            app->dpi=static_cast<float>(HIWORD(wparam));app->ReleaseGraphics();app->wallpaperReveal.Cancel();
            if(app->editFont){DeleteObject(app->editFont);app->editFont=nullptr;}app->EnsureEditResources();
            for(HWND input:{app->taskEdit,app->appSearch,app->wifiPassword,app->clipboardEdit})if(input)SendMessageW(input,WM_SETFONT,reinterpret_cast<WPARAM>(app->editFont),TRUE);
            // Keep EDIT HWNDs: draft, selection, caret, and IME state survive this transaction.
            if(app->wifiPassword)SetWindowTextW(app->wifiPassword,L"");
            const RECT* suggested=reinterpret_cast<RECT*>(lparam);SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
            app->RefreshGeometry();return 0;
        }
        case WM_DISPLAYCHANGE:app->placementMonitor=nullptr;app->RefreshGeometry();return 0;
        case WM_DWMCOMPOSITIONCHANGED:app->ReleaseGraphics();app->ConfigureMaterial();app->RefreshGeometry();return 0;
        case WM_SETTINGCHANGE:{BOOL enabled=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0);app->systemReduced=!enabled;app->reducedMotion=app->systemReduced||app->motionOverride;app->ReadTextScale();app->ConfigureAppearance();if(app->reducedMotion)app->SnapMotion();app->RefreshGeometry();return 0;}
        case WM_POWERBROADCAST:if(wparam==PBT_APMRESUMEAUTOMATIC){if(app->media)app->media->Rebase();if(app->tab==Tab::Stats&&app->expanded)app->SampleStats();app->SnapMotion();}return TRUE;
        case MotionFrame:if(app->frameClock.Accepts(wparam)&&app->scheduler.active){app->frameTickReady=true;app->Transition();}return 0;
        case WM_TIMER:
            if(wparam==desktop::ShellTimer)app->shell.Reconcile();
            else if (wparam==TransitionTimer) app->Transition();
            else if (wparam==StatsTimer) { app->SampleStats(); InvalidateRect(window,nullptr,FALSE); }
            else if (wparam==MediaTimer) { if(app->media)app->currentMedia=app->media->Snapshot(); InvalidateRect(window,nullptr,FALSE); }
            else if(wparam==ProximityTimer)app->ProximityTick();
            else if(wparam==FocusTimer){if(app->focusRunning){ULONGLONG now=GetTickCount64();app->focusSeconds=static_cast<int>((app->focusDeadline>now?app->focusDeadline-now:0)+999)/1000;
                if(!app->focusSeconds){app->focusRunning=false;app->note=L"Focus session complete.";app->UpdateTimers();}}
                if(app->expanded&&app->tab==Tab::Today){app->RebuildLayout();app->ArmMotion();InvalidateRect(window,nullptr,FALSE);}}
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
            app->system.RefreshVolume();app->volumeMotion.goal=app->system.volume;
            if(app->media){auto next=app->media->Snapshot();if(next.title!=app->currentMedia.title||next.artist!=app->currentMedia.artist){app->contentBlend.Snap(app->reducedMotion?1.0f:0.0f);app->contentBlend.goal=1;}if(!next.transportError.empty())app->note=next.transportError;app->currentMedia=std::move(next);}if(app->expanded)app->ArmMotion();
            app->UpdateTimers();if(app->expanded)InvalidateRect(window,nullptr,FALSE);return 0;
        case AudioDeviceChanged:
            if(app->fixtureGeometry)return 0;app->system.BindAudio(window);app->volumeMotion.goal=app->system.volume;if(app->expanded)app->ArmMotion(); InvalidateRect(window,nullptr,FALSE); return 0;
        case CatalogChanged:
            if(app->catalog){
                std::vector<AppEntry> apps;std::vector<WallpaperEntry> wallpapers;std::wstring error;app->catalog->Snapshot(apps,wallpapers,error,static_cast<unsigned>(wparam));
                if(wparam==1){app->appsLoading=false;if(!error.empty())app->note=error;app->apps=std::move(apps);app->FilterApps(false);app->RequestVisibleIcons();}
                else if(wparam==2){app->wallpaperLoading=false;app->ReplaceWallpapers(std::move(wallpapers));app->note=std::move(error);app->RequestWallpaperDetail();}
                else if(wparam==3){for(size_t i=0;i<app->wallpapers.size();++i)for(const auto& entry:wallpapers)if(app->wallpapers[i].path==entry.path){if(app->wallpapers[i].detailPixels!=entry.detailPixels&&i<app->thumbnails.size())app->thumbnails[i].Reset();app->wallpapers[i].detailPixels=entry.detailPixels;app->wallpapers[i].detailWidth=entry.detailWidth;app->wallpapers[i].detailHeight=entry.detailHeight;}}

                app->RebuildLayout();app->PositionInputs();NotifyWinEvent(EVENT_OBJECT_REORDER,window,OBJID_CLIENT,CHILDID_SELF);InvalidateRect(window,nullptr,FALSE);
            }return 0;
        case ThemeChanged:app->SetAccent(static_cast<unsigned>(wparam));return 0;
        case HardwareChanged:if(app->hardware)app->hardwareState=app->hardware->Snapshot();if(app->expanded&&app->tab==Tab::Stats){app->RebuildLayout();app->ArmMotion();InvalidateRect(window,nullptr,FALSE);}return 0;
        case WallpaperApplied:app->wallpaperApplying=false;if(wparam){app->appliedWallpaper=app->pendingWallpaper;app->wallpaperReveal.Begin();app->note=L"Wallpaper changed.";}else{app->wallpaperReveal.Cancel();app->note=L"Could not apply wallpaper. Choose another image.";}app->pendingWallpaper.clear();app->RebuildLayout();NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED,window,OBJID_CLIENT,CHILDID_SELF);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_COMMAND:
            if((HIWORD(wparam)==EN_SETFOCUS||HIWORD(wparam)==EN_KILLFOCUS)&&reinterpret_cast<HWND>(lparam)==app->taskEdit){app->composerFocus.goal=HIWORD(wparam)==EN_SETFOCUS?1.0f:0.0f;if(HIWORD(wparam)==EN_SETFOCUS)app->focusedKey=app->StableKey(400);app->ArmMotion();InvalidateRect(window,nullptr,FALSE);}
            if(reinterpret_cast<HWND>(lparam)==app->taskEdit&&HIWORD(wparam)==EN_CHANGE){wchar_t draft[121]{};GetWindowTextW(app->taskEdit,draft,121);app->taskDraft=draft;SendMessageW(app->taskEdit,EM_GETSEL,reinterpret_cast<WPARAM>(&app->draftStart),reinterpret_cast<LPARAM>(&app->draftEnd));}
            if (reinterpret_cast<HWND>(lparam)==app->appSearch && HIWORD(wparam)==EN_CHANGE) {
                int length=GetWindowTextLengthW(app->appSearch);
                std::wstring query(static_cast<size_t>(length)+1,L'\0');
                GetWindowTextW(app->appSearch,query.data(),length+1); query.resize(length);
                app->appQuery=std::move(query); app->FilterApps();app->RebuildLayout();app->RequestVisibleIcons();app->ArmMotion(); InvalidateRect(window,nullptr,FALSE);
            }
            if(reinterpret_cast<HWND>(lparam)==app->wifiPassword&&HIWORD(wparam)==EN_CHANGE){app->RebuildLayout();InvalidateRect(window,nullptr,FALSE);}
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:{
            auto dc=reinterpret_cast<HDC>(wparam);auto input=reinterpret_cast<HWND>(lparam);
            unsigned background=input==app->appSearch?theme::Card:input==app->clipboardEdit?theme::Inset:input==app->taskEdit?dock::Mix(theme::CardHover,theme::AccentSoft,app->composerFocus.value):theme::CardHover;
            COLORREF fill=RGB((background>>16)&255,(background>>8)&255,background&255);
            SetTextColor(dc,RGB((theme::Text>>16)&255,(theme::Text>>8)&255,theme::Text&255));SetBkColor(dc,fill);SetDCBrushColor(dc,fill);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));}
        case InvokeControl:{app->RebuildLayout();int index=app->IndexForChild(static_cast<LONG>(wparam));if(index>=0&&app->CanInvoke(app->hits[index]))app->Invoke(app->hits[index].id);return 0;}
        case FocusControl:app->RebuildLayout();app->FocusAt(app->IndexForChild(static_cast<LONG>(wparam)));return 0;
        case WM_MOUSEMOVE: {
            if (app->draggingVolume) { app->SetVolumeAt(GET_X_LPARAM(lparam)); return 0; }
            app->keyboardOpen=false;app->launchPreview=false;
            if (!app->tracking) { TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track); app->tracking=true; }
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            app->SetHover(hit);
            if(app->tab==Tab::Wallpaper&&app->flyout==Flyout::None&&!app->reducedMotion){auto stage=app->Layout().stage;float px=(GET_X_LPARAM(lparam)*96.0f/app->dpi-app->PanelX())/app->textScale;app->wallpaperParallax.goal=std::clamp((px-stage.CenterX())/std::max(1.0f,stage.Width()/2),-1.0f,1.0f);if(app->wallpaperParallax.Moving())app->ArmMotion();}
            if(app->tab==Tab::Apps&&app->flyout==Flyout::None){auto b=app->Body();float x=(GET_X_LPARAM(lparam)*96.0f/app->dpi-app->PanelX())/app->textScale,y=(GET_Y_LPARAM(lparam)*96.0f/app->dpi-app->PanelY())/app->textScale;bool rail=!app->railApps.empty()&&y>=b.top+56&&y<=b.top+156;if(rail&&app->railStrength.value==0)app->railPointer.Snap(x);app->railPointer.goal=x;app->railStrength.goal=rail?1.0f:0.0f;if(!app->reducedMotion&&(rail||app->railStrength.Moving()))app->ArmMotion();}
            return 0;
        }
        case WM_MOUSELEAVE: app->tracking=false; app->SetHover(-1);app->railStrength.goal=0;if(app->railStrength.Moving())app->ArmMotion();return 0;
        case WM_MOUSEACTIVATE:return app->expanded?MA_ACTIVATE:MA_NOACTIVATE;
        case WM_NCHITTEST:{POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ScreenToClient(window,&point);
            float x=point.x*96.0f/app->dpi,y=point.y*96.0f/app->dpi;
            bool handle=!app->YieldResting()&&std::abs(x-app->envelopeWidth/2)<=theme::DockWidth/2&&y>=0&&y<=theme::DockHeight;
            if(!app->renderMode&&!(handle||(app->expanded&&app->PresentedContains(x,y))))return HTTRANSPARENT;break;}
        case WM_ACTIVATE:
            if(LOWORD(wparam)==WA_INACTIVE&&app->expanded&&!app->modalOpen&&!app->previewMode){HWND next=reinterpret_cast<HWND>(lparam);if(!next||GetWindow(next,GW_OWNER)!=window)app->SetExpanded(false);}return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            app->focused=hit;app->focusedKey=hit>=0?app->hits[hit].key:L"";app->pressedKey=app->focusedKey;app->pressedId=hit>=0?app->hits[hit].id:0;if(hit>=0)SetCapture(window);InvalidateRect(window,nullptr,FALSE);
            if (hit>=0 && app->hits[hit].id==Volume) { app->draggingVolume=true; SetCapture(window); app->SetVolumeAt(GET_X_LPARAM(lparam)); }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (app->draggingVolume) { app->draggingVolume=false; ReleaseCapture(); return 0; }
            int hit=app->HitAt(GET_X_LPARAM(lparam)*96.0f/app->dpi,GET_Y_LPARAM(lparam)*96.0f/app->dpi);
            std::wstring pressed=app->pressedKey;app->pressedKey.clear();ReleaseCapture();
            if(hit>=0&&app->hits[hit].key==pressed&&app->hits[hit].id==app->pressedId){auto& pulse=app->feedback[pressed];pulse.value=1;pulse.goal=0;app->Invoke(app->hits[hit].id);}app->pressedId=0;app->ArmMotion();
            return 0;
        }
        case WM_CAPTURECHANGED: app->draggingVolume=false;app->pressedKey.clear();InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_MOUSEWHEEL:{
            if(!app->expanded){if(GET_WHEEL_DELTA_WPARAM(wparam)<0)app->SetExpanded(true);return 0;}
            app->scroll-=GET_WHEEL_DELTA_WPARAM(wparam)/120.0f*48;app->scroll=std::clamp(app->scroll,0.0f,std::max(0.0f,app->Layout().contentHeight-app->Layout().body.Height()));app->RebuildLayout();app->PositionInputs();InvalidateRect(window,nullptr,FALSE);return 0;
        }
        case WM_KEYDOWN: app->Key(wparam); return 0;
        case WM_HOTKEY: if (wparam==1){app->SetExpanded(!app->expanded);if(app->expanded){app->keyboardOpen=true;SetForegroundWindow(window);SetFocus(window);}}else if(wparam==2)DestroyWindow(window);else if(wparam==4&&!app->previewMode){if(app->shell.Active())app->shell.Stop();else app->shell.Start();}return 0;
        case WM_CONTEXTMENU: app->ContextMenu(); return 0;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,app->hovered>=0?IDC_HAND:IDC_ARROW)); return TRUE;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            if(!app->diagnosticsPath.empty()){
                std::ofstream metrics(app->diagnosticsPath,std::ios::trunc);
                auto quantile=[](std::vector<double> values,double q){if(values.empty())return -1.0;std::sort(values.begin(),values.end());return values[static_cast<size_t>(q*(values.size()-1))];};
                metrics<<"frame_clock_active="<<app->frameClock.Active()<<"\nframe_readiness_available="<<(app->composition.FrameReady()!=nullptr)<<"\n";
                metrics<<"system_transparency="<<app->systemTransparency<<"\nbackdrop_hresult="<<static_cast<unsigned long>(app->backdropResult)<<"\ntext_scale="<<app->textScale<<"\n";
                metrics<<"animation_timer_active="<<app->scheduler.active<<"\nbackdrop_api_available="<<app->backdropAvailable<<"\ncomposition_active="<<app->composition.Active()<<"\ndpi="<<app->dpi<<"\ntimer_period_ms="<<app->motionInterval<<"\n";
                metrics<<"animation_ticks="<<app->frameIntervals.size()<<"\ntick_p50_ms="<<quantile(app->frameIntervals,0.5)<<"\ntick_p95_ms="<<quantile(app->frameIntervals,0.95)<<"\npaint_p95_ms="<<quantile(app->paintTimes,0.95)<<"\nThese are message/CPU paint timings, not presented-frame or GPU measurements.\n";
            }
            if(app->accessibleProperties){MSAAPROPID property=PROPID_ACC_NAME;for(HWND input:{app->taskEdit,app->appSearch,app->wifiPassword,app->clipboardEdit})if(input)app->accessibleProperties->ClearHwndProps(input,static_cast<DWORD>(OBJID_CLIENT),CHILDID_SELF,&property,1);}
            app->frameClock.Stop();app->wallpaperReveal.Cancel();if(app->backdropWindow){DestroyWindow(app->backdropWindow);app->backdropWindow=nullptr;}
            app->shell.Stop();app->shell.RemoveTray();
            KillTimer(window,TransitionTimer); KillTimer(window,StatsTimer); KillTimer(window,MediaTimer);
            KillTimer(window,ProximityTimer);KillTimer(window,FocusTimer);KillTimer(window,ConnectivityTimer);
            if (app->toggleHotkey) UnregisterHotKey(window,1); if (app->quitHotkey) UnregisterHotKey(window,2);
            if(app->taskbarHotkey)UnregisterHotKey(window,4);
            PostQuitMessage(0); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); break;
        }
    } catch (const winrt::hresult_error& error) {
        app->PreviewTrace("native-hresult-error");OutputDebugStringW(error.message().c_str());
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
    if(std::wstring_view(command).starts_with(L"--exit-preview=")){
        wchar_t* end=nullptr;ULONGLONG requested=_wcstoui64(command+15,&end,10);if(!requested||requested>MAXDWORD||!end||*end)return 2;
        struct Target{DWORD process;HWND window=nullptr;} selected{static_cast<DWORD>(requested)};
        EnumWindows([](HWND handle,LPARAM context)->BOOL{auto* target=reinterpret_cast<Target*>(context);DWORD process=0;GetWindowThreadProcessId(handle,&process);if(process!=target->process||!GetPropW(handle,L"Dako.IsolatedPreview"))return TRUE;wchar_t name[64]{};GetClassNameW(handle,name,64);if(wcscmp(name,WindowClass))return TRUE;target->window=handle;return FALSE;},reinterpret_cast<LPARAM>(&selected));
        if(!selected.window)return 0;HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,selected.process);PostMessageW(selected.window,WM_CLOSE,0,0);DWORD wait=process?WaitForSingleObject(process,15000):WAIT_FAILED;if(process)CloseHandle(process);return wait==WAIT_OBJECT_0?0:6;
    }
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
                if(arg.starts_with(L"--diagnostics="))app.diagnosticsPath=arg.substr(14);
            }
            LocalFree(args);
        }
        wchar_t runningExecutable[32768]{};GetModuleFileNameW(nullptr,runningExecutable,32768);
        app.renderMode=!renderDirectory.empty();app.previewMode=wcsstr(command,L"--preview")!=nullptr||CompareStringOrdinal(std::filesystem::path(runningExecutable).filename().c_str(),-1,L"Dako.preview.exe",-1,TRUE)==CSTR_EQUAL;
        app.PreviewTrace("startup");
        HANDLE singleton=nullptr;
        if (!app.renderMode&&!app.previewMode) {
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
        HWND window=CreateWindowExW(WS_EX_TOPMOST|(app.previewMode?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW),WindowClass,app.previewMode?L"Dako v2.0 — Isolated Preview":L"Dako — Desktop Dock",WS_POPUP|WS_CLIPCHILDREN,
            0,0,300,100,nullptr,nullptr,instance,&app);
        if (!window) { MessageBoxW(nullptr,L"Dako could not create its native window.",L"Dako",MB_OK|MB_ICONERROR); result=1; }
        else if (app.renderMode) {
            try {
                app.RenderSuite(renderDirectory);VerifyAccessibility(app);
                app.accessible.Attach(new IslandAccessible(window));
                LONG children=0; Check(app.accessible->get_accChildCount(&children));
                VARIANT child{}; child.vt=VT_I4; child.lVal=app.hits.empty()?0:app.hits.front().child; BSTR name=nullptr;
                Check(app.accessible->get_accName(child,&name));
                bool valid=children==1 && name && std::wstring(name)==L"Reveal dock";
                SysFreeString(name); if (!valid) winrt::throw_hresult(E_UNEXPECTED);
            }
            catch (const winrt::hresult_error& error) { OutputDebugStringW(error.message().c_str()); result=2; }
            DestroyWindow(window);
        } else {
            app.PreviewTrace("window-created");app.shell.Initialize(window);
            if(!app.previewMode&&wcsstr(command,L"--desktop-mode")&&!app.shell.Start())MessageBoxW(window,L"Dako could not start taskbar recovery. The Windows taskbar remains available.",L"Dako",MB_OK|MB_ICONERROR);
            if (wcsstr(command,L"--tab=stats")){app.SetTab(Tab::Stats);app.launchPreview=true;}
            else if ((wcsstr(command,L"--expanded")||inspectMode||app.previewMode)&&!wcsstr(command,L"--resting")){app.SetExpanded(true);app.launchPreview=true;}
            app.PreviewTrace("show");ShowWindow(window,app.previewMode?SW_SHOW:SW_SHOWNOACTIVATE); UpdateWindow(window);app.PreviewTrace("message-loop");
            MSG message{}; BOOL status=0;
            while ((status=GetMessageW(&message,nullptr,0,0))>0) {
                if(message.message==WM_KEYDOWN&&(message.wParam==VK_NEXT||message.wParam==VK_PRIOR)&&(GetKeyState(VK_CONTROL)&0x8000)&&GetAncestor(message.hwnd,GA_ROOT)==window){app.Key(message.wParam);continue;}
                if ((message.hwnd==app.taskEdit || message.hwnd==app.appSearch||message.hwnd==app.wifiPassword||message.hwnd==app.clipboardEdit) && message.message==WM_KEYDOWN) {
                    if(message.wParam==VK_RETURN&&message.hwnd!=app.clipboardEdit){if(message.hwnd==app.appSearch){for(size_t slot=0;slot<app.AppSlots();++slot){size_t index=app.AppIndex(slot);if(index<app.apps.size()&&app.AppKey(app.apps[index])==app.selectedAppKey){app.Invoke(300+static_cast<int>(slot));break;}}}else app.Invoke(message.hwnd==app.taskEdit?AddTask:FlyoutConnect);continue;}
                    if(message.hwnd==app.appSearch&&(message.wParam==VK_DOWN||message.wParam==VK_UP)){app.RebuildLayout();for(size_t i=0;i<app.hits.size();++i)if(app.hits[i].id==300){app.FocusAt(static_cast<int>(i));break;}continue;}
                    if (message.wParam==VK_ESCAPE) { SetFocus(window); app.Key(VK_ESCAPE); continue; }
                    if(message.wParam==VK_F2){app.Key(VK_F2);continue;}
                    if (message.wParam==VK_TAB) { app.Key(VK_TAB); continue; }
                }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            result=status<0?1:static_cast<int>(message.wParam);
        }
        if (singleton) CloseHandle(singleton);
    }
    CoUninitialize(); return result;
}
