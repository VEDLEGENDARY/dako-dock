#pragma once
#include <windows.h>
#include <functional>

namespace nativechrome {
inline constexpr wchar_t Property[] = L"Dako.NativeChrome.Host";
struct Host {
    DWORD thread = GetCurrentThreadId();
    std::function<int(HWND, const wchar_t *, const wchar_t *, UINT)> message;
};
inline int Message(HWND owner, const wchar_t *text, const wchar_t *title, UINT flags) {
    auto *host = reinterpret_cast<Host *>(GetPropW(owner, Property));
    if (host && host->message)
        return host->thread == GetCurrentThreadId() ? host->message(owner, text, title, flags) : IDCANCEL;
    return MessageBoxW(owner, text, title, flags);
}
} // namespace nativechrome
