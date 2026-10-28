#include "ControlsService.h"
#include "AsyncWait.h"
#include "EffectsJournal.h"
#include <winrt/Windows.Devices.Enumeration.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cwctype>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace controls {
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Devices::Radios;
namespace {
constexpr wchar_t ThemeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
struct PhysicalMonitors {
    std::vector<PHYSICAL_MONITOR> values;
    explicit PhysicalMonitors(HMONITOR monitor) {
        DWORD count = 0;
        if (GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) && count && count <= 16) {
            values.resize(count);
            if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, values.data()))
                values.clear();
        }
    }
    ~PhysicalMonitors() {
        if (!values.empty())
            DestroyPhysicalMonitors(static_cast<DWORD>(values.size()), values.data());
    }
};
std::wstring Pnp(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    std::replace(value.begin(), value.end(), L'#', L'\\');
    if (value.starts_with(L"\\\\?\\"))
        value.erase(0, 4);
    auto guid = value.find(L"\\{");
    if (guid != std::wstring::npos)
        value.resize(guid);
    if (value.ends_with(L"_0"))
        value.resize(value.size() - 2);
    return value;
}
std::wstring String(IWbemClassObject *object, const wchar_t *key) {
    VARIANT value{};
    object->Get(key, 0, &value, nullptr, nullptr);
    std::wstring result = value.vt == VT_BSTR && value.bstrVal ? value.bstrVal : L"";
    VariantClear(&value);
    return result;
}
class Wmi {
    ComPtr<IWbemServices> service_;

  public:
    Wmi() {
        ComPtr<IWbemLocator> locator;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator))))
            return;
        BSTR space = SysAllocString(L"ROOT\\WMI");
        HRESULT result = locator->ConnectServer(space, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &service_);
        SysFreeString(space);
        if (FAILED(result)) {
            service_.Reset();
            return;
        }
        if (FAILED(CoSetProxyBlanket(service_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                                     RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE)))
            service_.Reset();
    }
    std::vector<ComPtr<IWbemClassObject>> Query(const wchar_t *query) {
        std::vector<ComPtr<IWbemClassObject>> objects;
        if (!service_)
            return objects;
        BSTR language = SysAllocString(L"WQL"), text = SysAllocString(query);
        ComPtr<IEnumWbemClassObject> items;
        HRESULT result =
            service_->ExecQuery(language, text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &items);
        SysFreeString(language);
        SysFreeString(text);
        if (FAILED(result))
            return objects;
        for (int i = 0; i < 16; ++i) {
            ComPtr<IWbemClassObject> object;
            ULONG count = 0;
            if (items->Next(500, 1, &object, &count) != S_OK || !count)
                break;
            objects.push_back(std::move(object));
        }
        return objects;
    }
    bool Set(const std::wstring &path, int percent) {
        if (!service_)
            return false;
        ComPtr<IWbemClassObject> definition, input, args, output;
        BSTR type = SysAllocString(L"WmiMonitorBrightnessMethods");
        HRESULT result = service_->GetObject(type, 0, nullptr, &definition, nullptr);
        SysFreeString(type);
        if (FAILED(result) || FAILED(definition->GetMethod(L"WmiSetBrightness", 0, &input, nullptr)) ||
            FAILED(input->SpawnInstance(0, &args)))
            return false;
        VARIANT timeout{};
        timeout.vt = VT_I4;
        timeout.lVal = 0;
        result = args->Put(L"Timeout", 0, &timeout, 0);
        VariantClear(&timeout);
        VARIANT brightness{};
        brightness.vt = VT_UI1;
        brightness.bVal = static_cast<BYTE>(percent);
        if (FAILED(result) || FAILED(args->Put(L"Brightness", 0, &brightness, 0)))
            return false;
        BSTR target = SysAllocString(path.c_str()), method = SysAllocString(L"WmiSetBrightness");
        result = service_->ExecMethod(target, method, 0, nullptr, args.Get(), &output, nullptr);
        SysFreeString(target);
        SysFreeString(method);
        VARIANT status{};
        bool ok = SUCCEEDED(result) && output && SUCCEEDED(output->Get(L"ReturnValue", 0, &status, nullptr, nullptr)) &&
                  (status.vt == VT_I4 || status.vt == VT_UI4) && status.ulVal == 0;
        VariantClear(&status);
        return ok;
    }
};
bool Brightness(HMONITOR monitor, int &percent, bool write) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;
    PhysicalMonitors physical(monitor);
    bool found = false, success = true;
    for (const auto &item : physical.values) {
        DWORD minimum = 0, current = 0, maximum = 0, caps = 0, temperatures = 0;
        if (!GetMonitorCapabilities(item.hPhysicalMonitor, &caps, &temperatures) || !(caps & MC_CAPS_BRIGHTNESS) ||
            !GetMonitorBrightness(item.hPhysicalMonitor, &minimum, &current, &maximum) || maximum <= minimum ||
            current < minimum || current > maximum)
            continue;
        found = true;
        if (write)
            success &= SetMonitorBrightness(item.hPhysicalMonitor,
                                            minimum + static_cast<DWORD>((maximum - minimum) *
                                                                         std::clamp(percent, 0, 100) / 100.0)) != FALSE;
        else
            percent = static_cast<int>(100.0 * (current - minimum) / (maximum - minimum));
    }
    if (found)
        return success;
    DISPLAY_DEVICEW device{sizeof(device)};
    EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME);
    auto id = Pnp(device.DeviceID);
    Wmi wmi;
    auto panels = wmi.Query(L"SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE");
    int monitorCount = 0;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT, LPARAM data) -> BOOL {
            ++*reinterpret_cast<int *>(data);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitorCount));
    for (const auto &panel : panels) {
        auto instance = String(panel.Get(), L"InstanceName");
        if (Pnp(instance) != id && !(monitorCount == 1 && panels.size() == 1))
            continue;
        if (!write) {
            VARIANT current{};
            panel->Get(L"CurrentBrightness", 0, &current, nullptr, nullptr);
            bool valid = current.vt == VT_UI1 || current.vt == VT_I4;
            if (valid)
                percent = std::clamp(current.vt == VT_UI1 ? static_cast<int>(current.bVal) : current.intVal, 0, 100);
            VariantClear(&current);
            return valid;
        }
        for (const auto &method : wmi.Query(L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE"))
            if (String(method.Get(), L"InstanceName") == instance)
                return wmi.Set(String(method.Get(), L"__PATH"), std::clamp(percent, 0, 100));
    }
    return false;
}
std::wstring MonitorIdentity(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DISPLAY_DEVICEW device{sizeof(device)};
    if (!GetMonitorInfoW(monitor, &info) ||
        !EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME))
        return {};
    return Pnp(device.DeviceID);
}
bool WriteTheme(bool dark) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ThemeKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    const wchar_t *names[]{L"AppsUseLightTheme", L"SystemUsesLightTheme"};
    DWORD old[2]{1, 1};
    bool existed[2]{};
    for (int i = 0; i < 2; ++i) {
        DWORD size = sizeof(DWORD), type = 0;
        existed[i] = RegQueryValueExW(key, names[i], nullptr, &type, reinterpret_cast<BYTE *>(&old[i]), &size) ==
                         ERROR_SUCCESS &&
                     type == REG_DWORD;
    }
    effects::Transaction transaction;
    if (!transaction.Remember(L"theme", L"Windows",
                              {{L"App", std::to_wstring(old[0])},
                               {L"System", std::to_wstring(old[1])},
                               {L"AppExists", existed[0] ? L"1" : L"0"},
                               {L"SystemExists", existed[1] ? L"1" : L"0"}})) {
        RegCloseKey(key);
        return false;
    }
    DWORD value = dark ? 0 : 1;
    privacy::Write audit(L"controls", L"registry-write",
                         L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"theme-toggle-and-rollback");
    bool ok = true;
    for (int i = 0; i < 2; ++i)
        if (RegSetValueExW(key, names[i], 0, REG_DWORD, reinterpret_cast<BYTE *>(&value), sizeof(value)) !=
            ERROR_SUCCESS)
            ok = false;
    if (!ok)
        for (int i = 0; i < 2; ++i) {
            if (existed[i])
                RegSetValueExW(key, names[i], 0, REG_DWORD, reinterpret_cast<BYTE *>(&old[i]), sizeof(DWORD));
            else
                RegDeleteValueW(key, names[i]);
        }
    RegCloseKey(key);
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                        SMTO_ABORTIFHUNG, 500, &ignored);
    return audit.Complete(ok);
}
} // namespace
struct Service::Impl {
    HWND window;
    struct Notify {
        std::atomic<HWND> window{nullptr};
    };
    std::shared_ptr<Notify> notify = std::make_shared<Notify>();
    std::mutex mutex, resultMutex;
    std::condition_variable wake;
    std::deque<std::pair<int, std::function<void()>>> jobs;
    std::atomic<bool> stop{false};
    Snapshot current, published;
    std::vector<std::pair<Radio, winrt::event_token>> radios;
    std::vector<std::pair<Radio, RadioState>> restore;
    std::thread thread;
    explicit Impl(HWND owner) : window(owner) {
        notify->window = owner;
        thread = std::thread([this] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stop || !jobs.empty(); });
                    if (stop)
                        break;
                    job = std::move(jobs.front().second);
                    jobs.pop_front();
                }
                try {
                    job();
                } catch (...) {
                    current.message = L"Windows could not complete this control request.";
                }
                {
                    std::scoped_lock lock(resultMutex);
                    published = current;
                }
                if (!stop)
                    PostMessageW(window, Changed, 0, 0);
            }
            Detach();
            restore.clear();
            winrt::uninit_apartment();
        });
    }
    ~Impl() {
        notify->window = nullptr;
        stop = true;
        {
            std::scoped_lock lock(mutex);
            jobs.clear();
        }
        wake.notify_one();
        thread.join();
    }
    void Queue(int kind, std::function<void()> job) {
        {
            std::scoped_lock lock(mutex);
            if (stop)
                return;
            std::erase_if(jobs, [kind](const auto &item) { return item.first == kind; });
            jobs.emplace_back(kind, std::move(job));
        }
        wake.notify_one();
    }
    template <class Async> auto Await(Async operation) {
        return AwaitNative(std::move(operation), [this] { return stop.load(); });
    }
    void Detach() {
        for (auto &item : radios)
            try {
                item.first.StateChanged(item.second);
            } catch (...) {
            }
        radios.clear();
    }
    void ReadRadios() {
        Detach();
        current.radiosAvailable = current.radiosOff = current.radioBlocked = false;
        bool anyOn = false;
        try {
            for (const auto &radio : Await(Radio::GetRadiosAsync())) {
                auto kind = radio.Kind();
                if (kind != RadioKind::WiFi && kind != RadioKind::Bluetooth && kind != RadioKind::MobileBroadband)
                    continue;
                current.radiosAvailable = true;
                anyOn |= radio.State() == RadioState::On;
                current.radioBlocked |= radio.State() == RadioState::Disabled;
                auto gate = notify;
                auto token = radio.StateChanged([gate](auto &&, auto &&) {
                    if (auto owner = gate->window.load())
                        PostMessageW(owner, Changed, 1, 0);
                });
                radios.emplace_back(radio, token);
            }
            current.radiosOff = current.radiosAvailable && !anyOn;
        } catch (const winrt::hresult_error &) {
            Detach();
            current.radiosAvailable = current.radiosOff = false;
        }
    }
    void ReadTheme() {
        DWORD app = 1, system = 1, size = sizeof(DWORD);
        current.darkKnown = RegGetValueW(HKEY_CURRENT_USER, ThemeKey, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                                         &app, &size) == ERROR_SUCCESS;
        size = sizeof(DWORD);
        current.darkKnown &= RegGetValueW(HKEY_CURRENT_USER, ThemeKey, L"SystemUsesLightTheme", RRF_RT_REG_DWORD,
                                          nullptr, &system, &size) == ERROR_SUCCESS;
        current.dark = app == 0 && system == 0;
    }
    void ReadOutputs() {
        current.outputs.clear();
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
            return;
        ComPtr<IMMDevice> standard;
        LPWSTR defaultId = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &standard)))
            standard->GetId(&defaultId);
        std::wstring selected = defaultId ? defaultId : L"";
        CoTaskMemFree(defaultId);
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)))
            return;
        devices->GetCount(&count);
        for (UINT i = 0; i < std::min(count, 32U); ++i) {
            ComPtr<IMMDevice> device;
            ComPtr<IPropertyStore> properties;
            if (FAILED(devices->Item(i, &device)))
                continue;
            LPWSTR id = nullptr;
            device->GetId(&id);
            Output output{id ? id : L"", L"Audio output", false};
            CoTaskMemFree(id);
            output.isDefault = output.id == selected;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
                PROPVARIANT name{};
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR &&
                    name.pwszVal)
                    output.name = name.pwszVal;
                PropVariantClear(&name);
            }
            current.outputs.push_back(std::move(output));
        }
    }
};
Service::Service(HWND window) : impl_(std::make_unique<Impl>(window)) {}
Service::~Service() = default;
void Service::Refresh(HMONITOR monitor) {
    impl_->Queue(0, [this, monitor] {
        auto &self = *impl_;
        self.current.monitor = monitor;
        self.ReadTheme();
        self.ReadOutputs();
        self.current.brightnessSupported = Brightness(monitor, self.current.brightness, false);
        self.ReadRadios();
    });
}
void Service::SetDark(bool dark) {
    impl_->Queue(1, [this, dark] {
        auto &self = *impl_;
        self.current.message = WriteTheme(dark) ? L"" : L"Windows could not change both theme settings.";
        self.ReadTheme();
    });
}
void Service::SetBrightness(HMONITOR monitor, int percent) {
    impl_->Queue(2, [this, monitor, percent] {
        auto &self = *impl_;
        int value = std::clamp(percent, 0, 100), original = 0;
        auto identity = MonitorIdentity(monitor);
        effects::Transaction transaction;
        if (identity.empty() || !Brightness(monitor, original, false) ||
            !transaction.Remember(L"brightness", identity, {{L"Value", std::to_wstring(original)}})) {
            self.current.message = L"Previous brightness could not be recorded. Windows was left unchanged.";
            return;
        }
        self.current.message =
            Brightness(monitor, value, true) ? L"" : L"This display did not accept the brightness change.";
        self.current.monitor = monitor;
        self.current.brightnessSupported = Brightness(monitor, self.current.brightness, false);
    });
}
void Service::SetAirplane(bool off, winrt::Windows::Foundation::IAsyncOperation<RadioAccessStatus> permission,
                          RadioKind kind) {
    impl_->Queue(3, [this, off, permission, kind] {
        auto &self = *impl_;
        self.current.message.clear();
        if (self.Await(permission) != RadioAccessStatus::Allowed) {
            self.current.message = L"Windows denied radio control. Open Airplane mode settings to change it.";
            return;
        }
        self.ReadRadios();
        effects::Transaction transaction;
        using winrt::Windows::Devices::Enumeration::DeviceInformation;
        std::vector<std::pair<Radio, RadioState>> changes;
        auto devices = self.Await(DeviceInformation::FindAllAsync(Radio::GetDeviceSelector()));
        std::vector<std::pair<Radio, RadioState>> original;
        for (const auto &device : devices) {
            auto radio = self.Await(Radio::FromIdAsync(device.Id()));
            if (!radio)
                continue;
            if (kind != RadioKind::Other && radio.Kind() != kind)
                continue;
            auto state = radio.State();
            if (state != RadioState::On && state != RadioState::Off)
                continue;
            if (!transaction.Remember(L"radio", std::wstring(device.Id()),
                                      {{L"Value", std::to_wstring(static_cast<int>(state))}})) {
                self.current.message = L"Previous radio state could not be recorded. Windows was left unchanged.";
                return;
            }
            original.emplace_back(radio, state);
        }
        if (off && kind == RadioKind::Other)
            self.restore = original;
        if (!off && kind == RadioKind::Other && !self.restore.empty())
            changes = self.restore;
        else
            for (const auto &item : original)
                changes.emplace_back(item.first, off ? RadioState::Off : RadioState::On);
        for (const auto &item : changes) {
            try {
                if (item.first.State() == item.second)
                    continue;
                if (self.Await(item.first.SetStateAsync(item.second)) != RadioAccessStatus::Allowed)
                    self.current.message = L"Windows blocked one or more radios. Their actual state is shown.";
            } catch (const winrt::hresult_error &) {
                self.current.message = L"A radio is unavailable. Its recovery record is retained.";
            }
        }
        if (!off && kind == RadioKind::Other)
            self.restore.clear();
        self.ReadRadios();
    });
}
bool RestoreEffect(const effects::Values &values) {
    auto get = [&](const wchar_t *key) {
        auto found = values.find(key);
        return found == values.end() ? std::wstring{} : found->second;
    };
    try {
        if (get(L"Kind") == L"brightness") {
            struct Find {
                std::wstring identity;
                HMONITOR monitor = nullptr;
            } find{get(L"Identity")};
            EnumDisplayMonitors(
                nullptr, nullptr,
                [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                    auto *data = reinterpret_cast<Find *>(context);
                    if (MonitorIdentity(monitor) == data->identity) {
                        data->monitor = monitor;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&find));
            int value = std::stoi(get(L"Value"));
            return find.monitor && Brightness(find.monitor, value, true);
        }
        if (get(L"Kind") == L"radio") {
            auto radio = AwaitNative(Radio::FromIdAsync(get(L"Identity")), [] { return false; });
            if (!radio)
                return false;
            auto state = static_cast<RadioState>(std::stoi(get(L"Value")));
            return radio.State() == state ||
                   AwaitNative(radio.SetStateAsync(state), [] { return false; }) == RadioAccessStatus::Allowed;
        }
    } catch (...) {
    }
    return false;
}
Snapshot Service::Read() {
    std::scoped_lock lock(impl_->resultMutex);
    return impl_->published;
}
} // namespace controls
