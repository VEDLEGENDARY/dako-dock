#include "WindowList.h"
#include <appmodel.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <initguid.h>
#include <propkey.h>
#include <wrl/client.h>
#include <map>

namespace desktop {
namespace {
void ProcessIdentity(DWORD id, WindowEntry &entry) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process)
        return;
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        entry.executable.assign(path, size);
    UINT32 length = 0;
    if (GetApplicationUserModelId(process, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER && length < 32768) {
        std::wstring appId(length, L'\0');
        if (GetApplicationUserModelId(process, &length, appId.data()) == ERROR_SUCCESS)
            entry.appId = appId.c_str();
    }
    CloseHandle(process);
}
std::shared_ptr<WindowIcon> ReadIcon(HWND window) {
    DWORD_PTR value = 0;
    SendMessageTimeoutW(window, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &value);
    HICON icon = reinterpret_cast<HICON>(value);
    if (!icon)
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    if (!icon)
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    if (!icon)
        return {};
    HICON copy = CopyIcon(icon);
    return copy ? std::make_shared<WindowIcon>(copy) : nullptr;
}
} // namespace
std::optional<WindowEntry> WindowList::Inspect(HWND window, bool includeOwn) {
    if (!IsWindow(window) || GetAncestor(window, GA_ROOT) != window)
        return {};
    DWORD id = 0;
    GetWindowThreadProcessId(window, &id);
    if (!includeOwn && id == GetCurrentProcessId())
        return {};
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (!EligibleWindow(IsWindowVisible(window) != FALSE, cloaked != 0, GetWindow(window, GW_OWNER) != nullptr,
                        GetWindowLongPtrW(window, GWL_EXSTYLE)))
        return {};
    wchar_t name[80]{};
    GetClassNameW(window, name, static_cast<int>(std::size(name)));
    if (!wcscmp(name, L"Shell_TrayWnd") || !wcscmp(name, L"Shell_SecondaryTrayWnd") || !wcscmp(name, L"Progman") ||
        !wcscmp(name, L"WorkerW"))
        return {};
    WindowEntry entry;
    entry.window = window;
    entry.process = id;
    wchar_t title[1024]{};
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    entry.title = title;
    if (entry.title.empty())
        return {};
    ProcessIdentity(id, entry);
    if (!wcscmp(name, L"ApplicationFrameWindow")) {
        struct Child {
            DWORD host;
            DWORD application = 0;
        } child{id};
        EnumChildWindows(
            window,
            [](HWND handle, LPARAM context) -> BOOL {
                auto &data = *reinterpret_cast<Child *>(context);
                DWORD process = 0;
                GetWindowThreadProcessId(handle, &process);
                if (process && process != data.host) {
                    data.application = process;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&child));
        if (child.application)
            ProcessIdentity(child.application, entry);
    }
    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&properties)))) {
        PROPVARIANT value{};
        if (SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            entry.appId = value.pwszVal;
        PropVariantClear(&value);
    }
    entry.identity = AppIdentity(entry.appId.empty() ? entry.executable : entry.appId);
    if (entry.identity.empty())
        entry.identity = L"window:" + std::to_wstring(reinterpret_cast<UINT_PTR>(window));
    entry.monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    entry.active = GetForegroundWindow() == window;
    entry.minimized = IsIconic(window) != FALSE;
    return entry;
}
void CALLBACK WindowList::Event(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (callbackWindow_ && window && object == OBJID_WINDOW && child == CHILDID_SELF)
        PostMessageW(callbackWindow_, WindowEventMessage, event, reinterpret_cast<LPARAM>(window));
}
bool WindowList::Start(HWND owner) {
    Stop();
    owner_ = owner;
    callbackWindow_ = owner;
    stopping_ = false;
    for (auto range : {std::pair{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
                       std::pair{EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE},
                       std::pair{EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_NAMECHANGE},
                       std::pair{EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}}) {
        auto hook = SetWinEventHook(range.first, range.second, nullptr, Event, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook) {
            hooks_.push_back(hook);
            privacy::Policy().Watch(L"window-event-hook", reinterpret_cast<uintptr_t>(hook), L"dock",
                                    L"running-window-events");
        }
    }
    if (hooks_.size() != 4) {
        Stop();
        return false;
    }
    worker_ = std::thread([this] { Run(); });
    Request();
    return true;
}
void WindowList::Stop() {
    callbackWindow_ = nullptr;
    for (auto hook : hooks_)
        if (UnhookWinEvent(hook))
            privacy::Policy().Unwatch(L"window-event-hook", reinterpret_cast<uintptr_t>(hook));
    hooks_.clear();
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_one();
    if (worker_.joinable())
        worker_.join();
    {
        std::scoped_lock lock(mutex_);
        snapshot_.clear();
        requested_ = false;
    }
    owner_ = nullptr;
}
void WindowList::Request() {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
            return;
        requested_ = true;
    }
    ready_.notify_one();
}
std::vector<WindowEntry> WindowList::Snapshot() {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}
void WindowList::Run() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return;
    std::map<HWND, std::pair<DWORD, std::shared_ptr<WindowIcon>>> seen;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || requested_; });
            if (stopping_)
                break;
            requested_ = false;
        }
        std::vector<WindowEntry> entries;
        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                auto &list = *reinterpret_cast<std::vector<WindowEntry> *>(context);
                if (auto entry = Inspect(window))
                    list.push_back(std::move(*entry));
                return list.size() < 256;
            },
            reinterpret_cast<LPARAM>(&entries));
        std::map<HWND, std::pair<DWORD, std::shared_ptr<WindowIcon>>> next;
        for (auto &entry : entries) {
            auto found = seen.find(entry.window);
            entry.icon = found != seen.end() && found->second.first == entry.process ? found->second.second
                                                                                     : ReadIcon(entry.window);
            next[entry.window] = {entry.process, entry.icon};
        }
        seen = std::move(next);
        {
            std::scoped_lock lock(mutex_);
            if (stopping_)
                break;
            snapshot_ = std::move(entries);
        }
        PostMessageW(owner_, WindowListChanged, 0, 0);
    }
    CoUninitialize();
}
bool WindowList::Activate(HWND window) {
    if (!IsWindow(window))
        return false;
    if (IsIconic(window))
        ShowWindowAsync(window, SW_RESTORE);
    return SetForegroundWindow(window) != FALSE;
}
bool WindowList::Close(HWND window) { return IsWindow(window) && PostMessageW(window, WM_CLOSE, 0, 0); }
} // namespace desktop
