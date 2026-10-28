#include "EffectsJournal.h"
#include "ControlsService.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <winrt/base.h>

namespace effects {
using Microsoft::WRL::ComPtr;
namespace {
std::wstring Get(const Values &values, const wchar_t *key) {
    auto found = values.find(key);
    return found == values.end() ? std::wstring{} : found->second;
}
int Number(const Values &values, const wchar_t *key) { return std::stoi(Get(values, key)); }
bool RestoreOne(const Values &values) {
    try {
        auto kind = Get(values, L"Kind");
        if (kind == L"brightness" || kind == L"radio")
            return controls::RestoreEffect(values);
        if (kind == L"theme") {
            privacy::Write audit(L"recovery", L"registry-write",
                                 L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                 L"restore-theme");
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
                return false;
            bool ok = true;
            const wchar_t *names[]{L"AppsUseLightTheme", L"SystemUsesLightTheme"};
            const wchar_t *fields[]{L"App", L"System"};
            const wchar_t *exists[]{L"AppExists", L"SystemExists"};
            for (int i = 0; i < 2; ++i) {
                if (Get(values, exists[i]) == L"1") {
                    DWORD value = static_cast<DWORD>(std::stoul(Get(values, fields[i])));
                    ok &= RegSetValueExW(key, names[i], 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                                         sizeof(value)) == ERROR_SUCCESS;
                } else {
                    auto status = RegDeleteValueW(key, names[i]);
                    ok &= status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
                }
            }
            RegCloseKey(key);
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                                SMTO_ABORTIFHUNG, 500, &ignored);
            return audit.Complete(ok);
        }
        if (kind == L"audio") {
            ComPtr<IMMDeviceEnumerator> enumerator;
            ComPtr<IMMDevice> device;
            ComPtr<IAudioEndpointVolume> volume;
            if (FAILED(
                    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
                FAILED(enumerator->GetDevice(Get(values, L"Identity").c_str(), &device)) ||
                FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume)))
                return false;
            float scalar = std::clamp(std::stof(Get(values, L"Volume")), 0.0f, 1.0f);
            return SUCCEEDED(volume->SetMasterVolumeLevelScalar(scalar, nullptr)) &&
                   SUCCEEDED(volume->SetMute(Get(values, L"Mute") == L"1", nullptr));
        }
        if (kind == L"wallpaper") {
            ComPtr<IDesktopWallpaper> desktop;
            if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop))))
                return false;
            bool ok = true;
            int count = std::clamp(Number(values, L"Count"), 0, 16);
            for (int i = 0; i < count; ++i) {
                auto id = Get(values, (L"Monitor" + std::to_wstring(i)).c_str());
                auto path = Get(values, (L"Path" + std::to_wstring(i)).c_str());
                RECT bounds{};
                if (FAILED(desktop->GetMonitorRECT(id.c_str(), &bounds))) {
                    ok = false;
                    continue;
                }
                ok &= SUCCEEDED(desktop->SetWallpaper(id.c_str(), path.empty() ? nullptr : path.c_str()));
            }
            ok &= SUCCEEDED(desktop->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(Number(values, L"Fit"))));
            ok &= SUCCEEDED(desktop->SetBackgroundColor(static_cast<COLORREF>(std::stoul(Get(values, L"Color")))));
            int slides = std::clamp(Number(values, L"SlideCount"), 0, 64);
            if (slides) {
                std::vector<PIDLIST_ABSOLUTE> owned;
                std::vector<PCIDLIST_ABSOLUTE> ids;
                for (int i = 0; i < slides; ++i) {
                    PIDLIST_ABSOLUTE id = nullptr;
                    if (SUCCEEDED(SHParseDisplayName(Get(values, (L"Slide" + std::to_wstring(i)).c_str()).c_str(),
                                                     nullptr, &id, 0, nullptr))) {
                        owned.push_back(id);
                        ids.push_back(id);
                    } else
                        ok = false;
                }
                ComPtr<IShellItemArray> items;
                if (ids.size() == static_cast<size_t>(slides) &&
                    SUCCEEDED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()), ids.data(), &items))) {
                    ok &= SUCCEEDED(desktop->SetSlideshow(items.Get()));
                    ok &= SUCCEEDED(desktop->SetSlideshowOptions(
                        static_cast<DESKTOP_SLIDESHOW_OPTIONS>(Number(values, L"SlideOptions")),
                        static_cast<UINT>(std::stoul(Get(values, L"SlideTick")))));
                } else
                    ok = false;
                for (auto id : owned)
                    CoTaskMemFree(id);
            }
            return ok;
        }
    } catch (...) {
    }
    return false;
}
} // namespace
std::wstring Create() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        return {};
    std::filesystem::path folder = std::filesystem::path(local) / L"Dako" / L"Recovery";
    CoTaskMemFree(local);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
        return {};
    auto path = (folder / (L"session-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()) + L".ini"))
                    .wstring();
    SettingsStore store(path);
    return store.Update(L"Recovery", {{L"Count", L"0"}, {L"Owner", std::to_wstring(GetCurrentProcessId())}}) ? path
                                                                                                             : L"";
}
void Restore(const std::wstring &path) {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return;
    bool complete = RestoreEntries(path, RestoreOne);
    if (complete) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
}
void Register() {
    createHook = Create;
    restoreHook = Restore;
}
bool Retry(const std::wstring &requested) {
    try {
        PWSTR local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
            return false;
        auto folder = std::filesystem::path(local) / L"Dako" / L"Recovery";
        CoTaskMemFree(local);
        auto file = std::filesystem::weakly_canonical(requested);
        if (std::filesystem::weakly_canonical(folder) != file.parent_path() || file.extension() != L".ini" ||
            !file.filename().wstring().starts_with(L"session-") || !std::filesystem::is_regular_file(file))
            return false;
        SettingsStore store(file.wstring());
        auto metadata = store.Get(L"Recovery");
        auto owner = static_cast<DWORD>(std::stoul(metadata[L"Owner"]));
        if (!owner)
            return false;
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, owner);
        if (process) {
            bool active = WaitForSingleObject(process, 0) != WAIT_OBJECT_0;
            CloseHandle(process);
            if (active)
                return false;
        } else if (GetLastError() != ERROR_INVALID_PARAMETER)
            return false;
        Restore(file.wstring());
        return !std::filesystem::exists(file);
    } catch (...) {
        return false;
    }
}
bool RememberWallpaper(IUnknown *object) {
    ComPtr<IDesktopWallpaper> desktop;
    if (FAILED(object->QueryInterface(IID_PPV_ARGS(&desktop))))
        return false;
    UINT count = 0;
    DESKTOP_WALLPAPER_POSITION fit;
    COLORREF color = 0;
    if (FAILED(desktop->GetMonitorDevicePathCount(&count)) || count > 16 || FAILED(desktop->GetPosition(&fit)) ||
        FAILED(desktop->GetBackgroundColor(&color)))
        return false;
    Values values{{L"Count", std::to_wstring(count)},
                  {L"Fit", std::to_wstring(fit)},
                  {L"Color", std::to_wstring(color)},
                  {L"SlideCount", L"0"}};
    for (UINT i = 0; i < count; ++i) {
        PWSTR id = nullptr, path = nullptr;
        if (FAILED(desktop->GetMonitorDevicePathAt(i, &id)))
            return false;
        HRESULT result = desktop->GetWallpaper(id, &path);
        if (FAILED(result)) {
            CoTaskMemFree(id);
            CoTaskMemFree(path);
            return false;
        }
        values[L"Monitor" + std::to_wstring(i)] = id ? id : L"";
        values[L"Path" + std::to_wstring(i)] = path ? path : L"";
        CoTaskMemFree(id);
        CoTaskMemFree(path);
    }
    DESKTOP_SLIDESHOW_STATE status{};
    desktop->GetStatus(&status);
    if (status & DSS_SLIDESHOW) {
        ComPtr<IShellItemArray> items;
        DWORD slides = 0;
        if (FAILED(desktop->GetSlideshow(&items)) || !items || FAILED(items->GetCount(&slides)) || slides > 64)
            return false;
        values[L"SlideCount"] = std::to_wstring(slides);
        for (DWORD i = 0; i < slides; ++i) {
            ComPtr<IShellItem> item;
            PWSTR name = nullptr;
            if (FAILED(items->GetItemAt(i, &item)) || FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name)))
                return false;
            values[L"Slide" + std::to_wstring(i)] = name ? name : L"";
            CoTaskMemFree(name);
        }
        DESKTOP_SLIDESHOW_OPTIONS options{};
        UINT tick = 0;
        if (FAILED(desktop->GetSlideshowOptions(&options, &tick)))
            return false;
        values[L"SlideOptions"] = std::to_wstring(options);
        values[L"SlideTick"] = std::to_wstring(tick);
    }
    Transaction transaction;
    return transaction.Remember(L"wallpaper", L"Windows", std::move(values));
}
} // namespace effects
