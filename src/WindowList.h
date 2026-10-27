#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "PrivacyRuntime.h"

namespace desktop {
inline constexpr UINT WindowEventMessage = WM_APP + 46, WindowListChanged = WM_APP + 47;
inline bool EligibleWindow(bool visible, bool cloaked, bool owner, LONG_PTR extendedStyle) {
    return visible && !cloaked && !(extendedStyle & WS_EX_TOOLWINDOW) && (!owner || (extendedStyle & WS_EX_APPWINDOW));
}
inline std::wstring AppIdentity(std::wstring value) {
    auto brace = value.find(L"}\\");
    if (brace != std::wstring::npos && value.starts_with(L"::{"))
        value.erase(0, brace + 2);
    constexpr std::wstring_view prefix = L"shell:AppsFolder\\";
    if (value.starts_with(prefix))
        value.erase(0, prefix.size());
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
}
struct WindowIcon {
    HICON handle = nullptr;
    explicit WindowIcon(HICON value) : handle(value) {}
    ~WindowIcon() {
        if (handle)
            DestroyIcon(handle);
    }
    WindowIcon(const WindowIcon &) = delete;
    WindowIcon &operator=(const WindowIcon &) = delete;
};
struct WindowEntry {
    HWND window = nullptr;
    DWORD process = 0;
    HMONITOR monitor = nullptr;
    std::wstring title, executable, appId, identity;
    std::shared_ptr<WindowIcon> icon;
    bool active = false, minimized = false;
};
class WindowList {
    HWND owner_ = nullptr;
    inline static HWND callbackWindow_ = nullptr;
    std::vector<HWINEVENTHOOK> hooks_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_ = false, requested_ = false;
    std::vector<WindowEntry> snapshot_;
    std::thread worker_;
    static void CALLBACK Event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void Run();

  public:
    WindowList() = default;
    ~WindowList() { Stop(); }
    WindowList(const WindowList &) = delete;
    WindowList &operator=(const WindowList &) = delete;
    bool Start(HWND owner);
    void Stop();
    void Request();
    std::vector<WindowEntry> Snapshot();
    static std::optional<WindowEntry> Inspect(HWND window, bool includeOwn = false);
    static bool Activate(HWND window);
    static bool Close(HWND window);
};
}
