#pragma once
#include <windows.h>
#include "PrivacyRuntime.h"
#include "Anchor.h"

class NotchHotZone {
    HWND window_ = nullptr;
    HWINEVENTHOOK foreground_ = nullptr;
    HWINEVENTHOOK location_ = nullptr;
    inline static HWND owner_ = nullptr;
    static void CALLBACK Foreground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        if (owner_) PostMessageW(owner_, ContextChanged, 0, 0);
    }
    static void CALLBACK Location(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG, DWORD, DWORD) {
        if (owner_ && object == OBJID_WINDOW && window == GetForegroundWindow())
            PostMessageW(owner_, ContextChanged, 0, 0);
    }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
            if (owner_) PostMessageW(owner_, PointerNear, 0, 0);
            return 0;
        }
        if (message == WM_MOUSELEAVE) { if (owner_) PostMessageW(owner_, PointerNear, 2, 0); return 0; }
        if (message == WM_LBUTTONUP) { if (owner_) PostMessageW(owner_, PointerNear, 1, 0); return 0; }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(window, message, wparam, lparam);
    }
public:
    static constexpr UINT PointerNear = WM_APP + 42, ContextChanged = WM_APP + 43;
    ~NotchHotZone() { Stop(); }
    void Stop() {
        owner_ = nullptr;
        if (foreground_ && UnhookWinEvent(foreground_))
            privacy::Policy().Unwatch(L"window-event-hook", reinterpret_cast<uintptr_t>(foreground_));
        if (location_ && UnhookWinEvent(location_))
            privacy::Policy().Unwatch(L"window-event-hook", reinterpret_cast<uintptr_t>(location_));
        if (window_) DestroyWindow(window_);
        foreground_ = location_ = nullptr;
        window_ = nullptr;
    }
    void Initialize(HWND owner) {
        owner_ = owner;
        WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = Procedure; type.lpszClassName = L"Dako.Notch.HotZone";
        RegisterClassW(&type);
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            type.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, type.hInstance, nullptr);
        if (window_) SetLayeredWindowAttributes(window_, 0, 1, LWA_ALPHA);
        foreground_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            Foreground, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        location_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
            Location, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        privacy::Policy().Watch(L"window-event-hook", reinterpret_cast<uintptr_t>(foreground_), L"notch",
                                L"foreground-context");
        privacy::Policy().Watch(L"window-event-hook", reinterpret_cast<uintptr_t>(location_), L"notch",
                                L"foreground-location");
    }
    void Place(HWND owner, bool visible, UINT dpi, anchor::Edge edge = anchor::Edge::Bottom) {
        if (!window_) return;
        RECT bounds{}; GetWindowRect(owner, &bounds);
        int width = MulDiv(252, static_cast<int>(dpi), 96), height = MulDiv(2, static_cast<int>(dpi), 96);
        int center = (bounds.left + bounds.right) / 2;
        SetWindowPos(window_, HWND_TOPMOST, center - width / 2, edge == anchor::Edge::Bottom ? bounds.bottom - height : bounds.top, width, height,
            SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
};
