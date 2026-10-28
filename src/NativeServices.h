#pragma once
struct MediaState {
    bool available = false, playing = false, paused = false, canPrevious = false, canPlay = false, canNext = false;
    std::wstring source;
    std::shared_ptr<std::vector<BYTE>> artwork;
    std::wstring transportError;
    std::wstring title = L"Nothing playing", artist = L"Play music in a supported app";
    double position = 0, duration = 0;
    steady_clock::time_point sampled = steady_clock::now();
};

// All asynchronous media work lives in one MTA, never on the window thread.
class MediaService {
    struct CallbackGate {
        std::mutex mutex;
        MediaService *owner;
        explicit CallbackGate(MediaService *value) : owner(value) {}
    };
    std::shared_ptr<CallbackGate> callbacks_ = std::make_shared<CallbackGate>(this);
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
    template <class Async> auto Await(Async operation) {
        return AwaitNative(std::move(operation), [this] { return cancel_.load(); });
    }
    std::atomic<bool> visible_{true}, refreshPending_{false}, propertiesPending_{false};
    std::thread worker_;

    void Queue(std::function<void()> job) {
        {
            std::scoped_lock lock(mutex_);
            if (stopping_)
                return;
            jobs_.push_back(std::move(job));
        }
        ready_.notify_one();
    }
    void RequestRefresh(bool properties) {
        if (properties)
            propertiesPending_ = true;
        if (refreshPending_.exchange(true))
            return;
        Queue([this] {
            bool read = propertiesPending_.exchange(false);
            refreshPending_ = false;
            Refresh(read);
        });
    }
    void DetachSession() {
        if (!session_)
            return;
        session_.MediaPropertiesChanged(propertiesToken_);
        session_.PlaybackInfoChanged(playbackToken_);
        session_.TimelinePropertiesChanged(timelineToken_);
        session_ = nullptr;
    }
    void Refresh(bool readProperties = true) {
        MediaState next;
        if (!readProperties) {
            std::scoped_lock lock(stateMutex_);
            next = state_;
        }
        try {
            if (session_) {
                next.source = session_.SourceAppUserModelId();
                if (readProperties) {
                    auto metadata = Await(session_.TryGetMediaPropertiesAsync());
                    next.title = metadata.Title().empty() ? L"Untitled media" : std::wstring(metadata.Title());
                    next.artist = metadata.Artist().empty() ? std::wstring(session_.SourceAppUserModelId())
                                                            : std::wstring(metadata.Artist());
                    next.artwork.reset();
                    if (metadata.Thumbnail()) {
                        auto stream = Await(metadata.Thumbnail().OpenReadAsync());
                        ComPtr<IStream> input;
                        ComPtr<IWICImagingFactory> images;
                        ComPtr<IWICBitmapDecoder> decoder;
                        ComPtr<IWICBitmapFrameDecode> frame;
                        ComPtr<IWICBitmapScaler> scaler;
                        ComPtr<IWICFormatConverter> converter;
                        if (SUCCEEDED(
                                CreateStreamOverRandomAccessStream(winrt::get_unknown(stream), IID_PPV_ARGS(&input))) &&
                            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                       IID_PPV_ARGS(&images))) &&
                            SUCCEEDED(images->CreateDecoderFromStream(input.Get(), nullptr,
                                                                      WICDecodeMetadataCacheOnLoad, &decoder)) &&
                            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(images->CreateBitmapScaler(&scaler)) &&
                            SUCCEEDED(scaler->Initialize(frame.Get(), 64, 64, WICBitmapInterpolationModeFant)) &&
                            SUCCEEDED(images->CreateFormatConverter(&converter)) &&
                            SUCCEEDED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                            WICBitmapDitherTypeNone, nullptr, 0,
                                                            WICBitmapPaletteTypeCustom))) {
                            auto pixels = std::make_shared<std::vector<BYTE>>(64 * 64 * 4);
                            if (SUCCEEDED(converter->CopyPixels(nullptr, 64 * 4, static_cast<UINT>(pixels->size()),
                                                                pixels->data())))
                                next.artwork = std::move(pixels);
                        }
                    }
                }
                auto playback = session_.GetPlaybackInfo();
                auto timeline = session_.GetTimelineProperties();
                next.available = true;
                next.playing =
                    playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                next.paused =
                    playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused;
                auto controls = playback.Controls();
                next.canPrevious = controls.IsPreviousEnabled();
                next.canNext = controls.IsNextEnabled();
                next.canPlay = controls.IsPlayPauseToggleEnabled() ||
                               (next.playing ? controls.IsPauseEnabled() : controls.IsPlayEnabled());
                next.position = duration<double>(timeline.Position() - timeline.StartTime()).count();
                next.sampled = steady_clock::now();
                next.duration = duration<double>(timeline.EndTime() - timeline.StartTime()).count();
            } else
                next = MediaState{};
        } catch (const winrt::hresult_error &) {
            next.available = next.playing = next.canPrevious = next.canPlay = next.canNext = false;
            next.artist = L"Media session unavailable";
        }
        {
            std::scoped_lock lock(stateMutex_);
            state_ = std::move(next);
        }
        PostMessageW(window_, StateChanged, 0, 0);
    }
    void AttachSession() {
        DetachSession();
        session_ = manager_.GetCurrentSession();
        if (session_) {
            auto shared = callbacks_;
            propertiesToken_ = session_.MediaPropertiesChanged([shared](auto &&, auto &&) {
                std::scoped_lock lock(shared->mutex);
                if (auto *owner = shared->owner; owner)
                    owner->RequestRefresh(true);
            });
            playbackToken_ = session_.PlaybackInfoChanged([shared](auto &&, auto &&) {
                std::scoped_lock lock(shared->mutex);
                if (auto *owner = shared->owner; owner)
                    owner->RequestRefresh(false);
            });
            timelineToken_ = session_.TimelinePropertiesChanged([shared](auto &&, auto &&) {
                std::scoped_lock lock(shared->mutex);
                if (auto *owner = shared->owner; owner && owner->visible_)
                    owner->RequestRefresh(false);
            });
        }
        Refresh();
    }
    void Run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            manager_ = Await(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            auto shared = callbacks_;
            currentToken_ = manager_.CurrentSessionChanged([shared](auto &&, auto &&) {
                std::scoped_lock lock(shared->mutex);
                if (auto *owner = shared->owner)
                    owner->Queue([owner] { owner->AttachSession(); });
            });
            AttachSession();
        } catch (const winrt::hresult_error &) {
            {
                std::scoped_lock lock(stateMutex_);
                state_.artist = L"Windows media access unavailable";
            }
            PostMessageW(window_, StateChanged, 0, 0);
        }
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_)
                    break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try {
                job();
            } catch (const winrt::hresult_error &) {
                Refresh();
            }
        }
        try {
            DetachSession();
            if (manager_)
                manager_.CurrentSessionChanged(currentToken_);
        } catch (const winrt::hresult_error &) {
        }
        manager_ = nullptr;
        winrt::uninit_apartment();
    }

  public:
    explicit MediaService(HWND window) : window_(window), worker_([this] { Run(); }) {}
    ~MediaService() {
        {
            std::scoped_lock lock(callbacks_->mutex);
            callbacks_->owner = nullptr;
        }
        cancel_ = true;
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }
    MediaState Snapshot() {
        std::scoped_lock lock(stateMutex_);
        auto result = state_;
        if (result.playing) {
            result.position += duration<double>(steady_clock::now() - result.sampled).count();
            if (result.duration > 0)
                result.position = std::min(result.position, result.duration);
        }
        return result;
    }
    void Rebase() {
        if (visible_)
            RequestRefresh(true);
    }
    void SetVisible(bool value) {
        bool previous = visible_.exchange(value);
        if (value && !previous)
            Queue([this] { Refresh(); });
    }
    void Seek(double seconds) {
        Queue([this, seconds] {
            try {
                if (session_ && session_.GetPlaybackInfo().Controls().IsPlaybackPositionEnabled())
                    Await(session_.TryChangePlaybackPositionAsync(static_cast<int64_t>(seconds * 10000000)));
            } catch (const winrt::hresult_error &) {
            }
        });
    }
    void Transport(int action) {
        Queue([this, action] {
            try {
                if (!session_)
                    return;
                auto playback = session_.GetPlaybackInfo();
                auto controls = playback.Controls();
                bool succeeded = false;
                if (action == 0 && controls.IsPreviousEnabled())
                    succeeded = Await(session_.TrySkipPreviousAsync());
                else if (action == 2 && controls.IsNextEnabled())
                    succeeded = Await(session_.TrySkipNextAsync());
                else if (action == 1) {
                    if (controls.IsPlayPauseToggleEnabled())
                        succeeded = Await(session_.TryTogglePlayPauseAsync());
                    else if (playback.PlaybackStatus() ==
                                 GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing &&
                             controls.IsPauseEnabled())
                        succeeded = Await(session_.TryPauseAsync());
                    else if (controls.IsPlayEnabled())
                        succeeded = Await(session_.TryPlayAsync());
                }
                Refresh();
                if (!succeeded) {
                    {
                        std::scoped_lock lock(stateMutex_);
                        state_.transportError = L"The media app declined this action.";
                    }
                    PostMessageW(window_, StateChanged, 0, 0);
                }
            } catch (const winrt::hresult_error &) {
                if (!cancel_) {
                    {
                        std::scoped_lock lock(stateMutex_);
                        state_.transportError =
                            L"Windows could not complete this media action. Try again or use the media app.";
                    }
                    PostMessageW(window_, StateChanged, 0, 0);
                }
            }
        });
    }
};

class VolumeSink : public IAudioEndpointVolumeCallback {
    std::atomic<ULONG> references_{1};
    HWND window_;

  public:
    explicit VolumeSink(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override {
        if (!object)
            return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(IAudioEndpointVolumeCallback)) {
            *object = static_cast<IAudioEndpointVolumeCallback *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        PostMessageW(window_, StateChanged, 0, 0);
        return S_OK;
    }
};

class AudioDeviceSink final : public IMMNotificationClient {
    std::atomic<ULONG> references_{1};
    HWND window_;

  public:
    explicit AudioDeviceSink(HWND window) : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override {
        if (!object)
            return E_POINTER;
        if (id == IID_IUnknown || id == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole)
            PostMessageW(window_, AudioDeviceChanged, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

struct SystemState {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IAudioEndpointVolume> audio;
    VolumeSink *sink = nullptr;
    AudioDeviceSink *deviceSink = nullptr;
    float volume = 0;
    bool muted = false, wifiConnected = false;
    std::wstring wifi = L"Not connected", bluetooth = L"Not available";
    std::wstring audioId;
    std::wstring audioName = L"No audio device", diskRoot;
    bool memoryValid = false, cpuValid = false, diskValid = false, networkValid = false;
    DWORD memoryLoad = 0;
    double availableGiB = 0, workingMiB = 0, privateMiB = 0, processCpu = 0;
    double totalCpu = 0, diskUsedGiB = 0, diskTotalGiB = 0, diskPercent = 0, receiveRate = 0, sendRate = 0;
    ULONGLONG systemIdle = 0, systemKernel = 0, systemUser = 0, receivedBytes = 0, sentBytes = 0, networkTime = 0;
    ULONGLONG previousCpu = 0, previousTime = 0;

    void Initialize(HWND window) {
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            deviceSink = new AudioDeviceSink(window);
            enumerator->RegisterEndpointNotificationCallback(deviceSink);
            BindAudio(window);
        }
        RefreshVolume();
        RefreshConnections();
        Sample();
    }
    void BindAudio(HWND window) {
        if (audio && sink)
            audio->UnregisterControlChangeNotify(sink);
        if (sink) {
            sink->Release();
            sink = nullptr;
        }
        audio.Reset();
        volume = 0;
        muted = false;
        audioName = L"No audio device";
        ComPtr<IMMDevice> device;
        if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &audio))) {
            LPWSTR id = nullptr;
            device->GetId(&id);
            audioId = id ? id : L"";
            CoTaskMemFree(id);
            sink = new VolumeSink(window);
            audio->RegisterControlChangeNotify(sink);
            ComPtr<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
                PROPVARIANT name{};
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR &&
                    name.pwszVal)
                    audioName = name.pwszVal;
                PropVariantClear(&name);
            }
        }
        RefreshVolume();
    }
    ~SystemState() {
        if (enumerator && deviceSink)
            enumerator->UnregisterEndpointNotificationCallback(deviceSink);
        if (deviceSink)
            deviceSink->Release();
        if (audio && sink)
            audio->UnregisterControlChangeNotify(sink);
        if (sink)
            sink->Release();
    }
    void RefreshVolume() {
        if (audio) {
            BOOL value = FALSE;
            audio->GetMasterVolumeLevelScalar(&volume);
            audio->GetMute(&value);
            muted = value != FALSE;
        }
    }
    void SetVolume(float value) {
        if (audio) {
            effects::Transaction transaction;
            RefreshVolume();
            if (!transaction.Remember(L"audio", audioId,
                                      {{L"Volume", std::to_wstring(volume)}, {L"Mute", muted ? L"1" : L"0"}}))
                return;
            audio->SetMasterVolumeLevelScalar(std::clamp(value, 0.0f, 1.0f), nullptr);
            RefreshVolume();
        }
    }
    void ToggleMute() {
        if (audio) {
            effects::Transaction transaction;
            RefreshVolume();
            if (!transaction.Remember(L"audio", audioId,
                                      {{L"Volume", std::to_wstring(volume)}, {L"Mute", muted ? L"1" : L"0"}}))
                return;
            audio->SetMute(!muted, nullptr);
            RefreshVolume();
        }
    }
    void RefreshConnections() {
        wifi = L"Not connected";
        wifiConnected = false;
        HANDLE handle = nullptr;
        DWORD negotiated = 0;
        if (WlanOpenHandle(2, nullptr, &negotiated, &handle) == ERROR_SUCCESS) {
            PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
            if (WlanEnumInterfaces(handle, nullptr, &interfaces) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
                    auto &item = interfaces->InterfaceInfo[i];
                    if (item.isState != wlan_interface_state_connected)
                        continue;
                    wifiConnected = true;
                    DWORD size = 0;
                    WLAN_OPCODE_VALUE_TYPE type{};
                    PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
                    if (WlanQueryInterface(handle, &item.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr,
                                           &size, reinterpret_cast<void **>(&connection), &type) == ERROR_SUCCESS) {
                        auto &ssid = connection->wlanAssociationAttributes.dot11Ssid;
                        wifi = Wide(reinterpret_cast<const char *>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
                        if (wifi.empty())
                            wifi = L"Connected";
                        WlanFreeMemory(connection);
                        break;
                    }
                    wifi = L"Connected";
                }
                WlanFreeMemory(interfaces);
            }
            WlanCloseHandle(handle, nullptr);
        }
        BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};
        HANDLE radio = nullptr;
        HBLUETOOTH_RADIO_FIND found = BluetoothFindFirstRadio(&params, &radio);
        bluetooth = found ? L"Radio available" : L"Not available";
        if (found)
            BluetoothFindRadioClose(found);
        if (radio)
            CloseHandle(radio);
    }
    void Sample() {
        memoryValid = cpuValid = diskValid = networkValid = false;
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory)) {
            memoryValid = true;
            memoryLoad = memory.dwMemoryLoad;
            availableGiB = memory.ullAvailPhys / 1073741824.0;
        }
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                 sizeof(counters))) {
            workingMiB = counters.WorkingSetSize / 1048576.0;
            privateMiB = counters.PrivateUsage / 1048576.0;
        }
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            ULONGLONG cpu = (static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                            (static_cast<ULONGLONG>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
            ULONGLONG now = GetTickCount64();
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            if (previousTime && now > previousTime)
                processCpu = 100.0 * static_cast<double>(cpu - previousCpu) /
                             (10000.0 * static_cast<double>(now - previousTime) * info.dwNumberOfProcessors);
            previousCpu = cpu;
            previousTime = now;
        }
        auto value = [](FILETIME time) {
            return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        FILETIME idle{}, totalKernel{}, totalUser{};
        if (GetSystemTimes(&idle, &totalKernel, &totalUser)) {
            ULONGLONG k = value(totalKernel), u = value(totalUser), i = value(idle),
                      total = (k - systemKernel) + (u - systemUser);
            cpuValid = systemKernel && total;
            if (cpuValid)
                totalCpu = std::clamp(100.0 * (1.0 - static_cast<double>(i - systemIdle) / total), 0.0, 100.0);
            systemIdle = i;
            systemKernel = k;
            systemUser = u;
        }
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        std::wstring root = std::wstring(windows).substr(0, 3);
        diskRoot = root;
        ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
        if (GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, &totalFree) && totalBytes.QuadPart) {
            diskValid = true;
            diskTotalGiB = totalBytes.QuadPart / 1073741824.0;
            diskUsedGiB = (totalBytes.QuadPart - totalFree.QuadPart) / 1073741824.0;
            diskPercent = 100 * diskUsedGiB / diskTotalGiB;
        }
        PMIB_IF_TABLE2 interfaces = nullptr;
        if (GetIfTable2(&interfaces) == NO_ERROR) {
            ULONGLONG received = 0, sent = 0;
            for (ULONG i = 0; i < interfaces->NumEntries; ++i) {
                const auto &row = interfaces->Table[i];
                if (row.OperStatus == IfOperStatusUp && row.InterfaceAndOperStatusFlags.HardwareInterface) {
                    received += row.InOctets;
                    sent += row.OutOctets;
                }
            }
            ULONGLONG now = GetTickCount64();
            if (networkTime && now > networkTime && received >= receivedBytes && sent >= sentBytes) {
                networkValid = true;
                receiveRate = (received - receivedBytes) * 1000.0 / (now - networkTime);
                sendRate = (sent - sentBytes) * 1000.0 / (now - networkTime);
            }
            receivedBytes = received;
            sentBytes = sent;
            networkTime = now;
            FreeMibTable(interfaces);
        }
    }
};
