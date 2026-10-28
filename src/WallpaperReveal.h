#pragma once
#include <windows.h>
#include <dwmapi.h>
#include "ThemePalette.h"

// A single ephemeral desktop snapshot is uploaded once. Region changes reveal the
// live desktop underneath; no screenshots are saved, transmitted or retained afterward.
class WallpaperReveal {
    HWND window_ = nullptr, foreground_ = nullptr;
    RECT bounds_{};
    ULONGLONG started_ = 0;
    bool active_ = false;
    HRGN desktopMask_ = nullptr;
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto *self = reinterpret_cast<WallpaperReveal *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<WallpaperReveal *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_TIMER) {
            if (wparam == 1 || GetForegroundWindow() != self->foreground_ || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
                (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
                self->Cancel();
            else if (wparam == 2) {
                float progress = (GetTickCount64() - self->started_) / 680.0f;
                if (progress >= 1)
                    self->Cancel();
                else
                    self->SetProgress(progress);
            }
            return 0;
        }
        if (message == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        return DefWindowProcW(window, message, wparam, lparam);
    }
    bool Create() {
        WNDCLASSW type{};
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpfnWndProc = Procedure;
        type.lpszClassName = L"Dako.Wallpaper.Reveal";
        RegisterClassW(&type);
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                  type.lpszClassName, L"Dako wallpaper transition", WS_POPUP, bounds_.left, bounds_.top,
                                  bounds_.right - bounds_.left, bounds_.bottom - bounds_.top, nullptr, nullptr,
                                  type.hInstance, this);
        return window_ != nullptr;
    }

  public:
    ~WallpaperReveal() { Cancel(); }
    bool Running() const { return window_ != nullptr; }
    bool Freeze(bool reducedMotion, bool desktopOnly = true) {
        Cancel();
        if (reducedMotion)
            return false;
        bounds_ = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        bounds_.right = bounds_.left + width;
        bounds_.bottom = bounds_.top + height;
        if (width <= 0 || height <= 0 || static_cast<unsigned long long>(width) * height > 2073600)
            return false;
        foreground_ = GetForegroundWindow();
        if (desktopOnly) {
            desktopMask_ = CreateRectRgn(0, 0, width, height);
            EnumWindows(
                [](HWND window, LPARAM context) -> BOOL {
                    auto *self = reinterpret_cast<WallpaperReveal *>(context);
                    if (!IsWindowVisible(window) || IsIconic(window))
                        return TRUE;
                    DWORD cloaked = 0;
                    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
                        return TRUE;
                    wchar_t name[80]{};
                    GetClassNameW(window, name, 80);
                    if (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW") || !wcscmp(name, L"Shell_TrayWnd") ||
                        !wcscmp(name, L"Shell_SecondaryTrayWnd"))
                        return TRUE;
                    RECT outer{};
                    GetWindowRect(window, &outer);
                    if (outer.right <= outer.left || outer.bottom <= outer.top)
                        return TRUE;
                    HRGN region = CreateRectRgn(0, 0, 0, 0);
                    if (GetWindowRgn(window, region) == ERROR) {
                        RECT visible = outer;
                        DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible));
                        SetRectRgn(region, visible.left, visible.top, visible.right, visible.bottom);
                    } else
                        OffsetRgn(region, outer.left, outer.top);
                    OffsetRgn(region, -self->bounds_.left, -self->bounds_.top);
                    CombineRgn(self->desktopMask_, self->desktopMask_, region, RGN_DIFF);
                    DeleteObject(region);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(this));
            RECT visible{};
            if (GetRgnBox(desktopMask_, &visible) == NULLREGION) {
                Cancel();
                return false;
            }
        }
        HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
        auto original = SelectObject(memory, bitmap);
        bool captured =
            BitBlt(memory, 0, 0, width, height, screen, bounds_.left, bounds_.top, SRCCOPY | CAPTUREBLT) != FALSE;
        bool uploaded = false;
        if (captured && Create()) {
            POINT origin{bounds_.left, bounds_.top}, source{};
            SIZE size{width, height};
            uploaded =
                UpdateLayeredWindow(window_, screen, &origin, &size, memory, &source, 0, nullptr, ULW_OPAQUE) != FALSE;
        }
        SelectObject(memory, original);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (!uploaded) {
            Cancel();
            return false;
        }
        SetProgress(0);
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(window_, 1, 5000, nullptr);
        SetTimer(window_, 3, 50, nullptr);
        return true;
    }
    void Begin() {
        if (!window_)
            return;
        KillTimer(window_, 1);
        KillTimer(window_, 3);
        started_ = GetTickCount64();
        active_ = true;
        SetProgress(0);
        SetTimer(window_, 2, 16, nullptr);
    }
    void SetProgress(float progress) {
        if (!window_)
            return;
        int width = bounds_.right - bounds_.left, height = bounds_.bottom - bounds_.top;
        float radius = palette::RevealRadius(progress, static_cast<float>(width), static_cast<float>(height));
        int r = static_cast<int>(std::ceil(radius));
        HRGN remaining = CreateRectRgn(0, 0, width, height),
             hole = CreateEllipticRgn(width / 2 - r, height / 2 - r, width / 2 + r, height / 2 + r);
        if (desktopMask_)
            CombineRgn(remaining, desktopMask_, desktopMask_, RGN_COPY);
        CombineRgn(remaining, remaining, hole, RGN_DIFF);
        DeleteObject(hole);
        if (!SetWindowRgn(window_, remaining, FALSE))
            DeleteObject(remaining);
    }
    void Cancel() {
        active_ = false;
        if (window_) {
            KillTimer(window_, 1);
            KillTimer(window_, 2);
            KillTimer(window_, 3);
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (desktopMask_) {
            DeleteObject(desktopMask_);
            desktopMask_ = nullptr;
        }
    }
    bool VerifyGeometry() {
        Cancel();
        bounds_ = {0, 0, 320, 200};
        if (!Create())
            return false;
        desktopMask_ = CreateRectRgn(0, 0, 320, 200);
        HRGN app = CreateRectRgn(10, 10, 80, 60);
        CombineRgn(desktopMask_, desktopMask_, app, RGN_DIFF);
        DeleteObject(app);
        SetProgress(0);
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        GetWindowRgn(window_, region);
        bool valid = !PtInRegion(region, 20, 20) && PtInRegion(region, 300, 40);
        DeleteObject(region);
        SetProgress(0.25f);
        region = CreateRectRgn(0, 0, 0, 0);
        GetWindowRgn(window_, region);
        valid = valid && !PtInRegion(region, 160, 100) && PtInRegion(region, 0, 0);
        DeleteObject(region);
        SetProgress(1);
        region = CreateRectRgn(0, 0, 0, 0);
        GetWindowRgn(window_, region);
        RECT rect{};
        valid = valid && GetRgnBox(region, &rect) == NULLREGION;
        DeleteObject(region);
        Cancel();
        return valid;
    }
};
