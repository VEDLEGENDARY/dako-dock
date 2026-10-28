#pragma once
#include "Anchor.h"
#include <windows.h>
#include <vector>
#include <array>

namespace graphics {
// A cached Gaussian nine-slice. No worker, timer or per-frame bitmap allocation.
class AmbientShadow {
    HWND window_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::array<int, 4> key_{};
    BYTE alpha_ = 0;
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    void ReleaseBitmap() {
        if (memory_) {
            SelectObject(memory_, previous_);
            DeleteDC(memory_);
            memory_ = nullptr;
        }
        if (bitmap_) {
            DeleteObject(bitmap_);
            bitmap_ = nullptr;
        }
    }
    static void Blur(std::vector<float> &pixels, int size, int radius) {
        std::vector<float> scratch(pixels.size());
        for (int pass = 0; pass < 3; ++pass) {
            for (int y = 0; y < size; ++y) {
                float sum = 0;
                for (int x = -radius; x <= radius; ++x)
                    if (x >= 0 && x < size)
                        sum += pixels[y * size + x];
                for (int x = 0; x < size; ++x) {
                    scratch[y * size + x] = sum / (radius * 2 + 1);
                    if (x - radius >= 0)
                        sum -= pixels[y * size + x - radius];
                    if (x + radius + 1 < size)
                        sum += pixels[y * size + x + radius + 1];
                }
            }
            for (int x = 0; x < size; ++x) {
                float sum = 0;
                for (int y = -radius; y <= radius; ++y)
                    if (y >= 0 && y < size)
                        sum += scratch[y * size + x];
                for (int y = 0; y < size; ++y) {
                    pixels[y * size + x] = sum / (radius * 2 + 1);
                    if (y - radius >= 0)
                        sum -= scratch[(y - radius) * size + x];
                    if (y + radius + 1 < size)
                        sum += scratch[(y + radius + 1) * size + x];
                }
            }
        }
    }

  public:
    static std::vector<unsigned> Slice(float dpi, float radius, float sigma, float inset, float opacity, int &tile,
                                       int &cut) {
        float scale = dpi / 96;
        int margin = static_cast<int>(std::lround(inset * scale));
        int corner = static_cast<int>(std::ceil(radius * scale));
        cut = margin + corner;
        tile = cut * 2 + 8;
        int right = tile - margin;
        std::vector<float> slice(static_cast<size_t>(tile) * tile);
        for (int y = margin; y < right; ++y)
            for (int x = margin; x < right; ++x) {
                int cx = std::clamp(x, cut, right - corner), cy = std::clamp(y, cut, right - corner);
                float distance = static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy));
                slice[y * tile + x] = distance <= corner * corner ? 1.0f : 0.0f;
            }
        // Three box filters approximate sigma=20 DIP without a runtime image dependency.
        Blur(slice, tile, std::max(1, static_cast<int>(std::lround(sigma * scale))));
        std::vector<unsigned> pixels(slice.size());
        for (size_t i = 0; i < slice.size(); ++i)
            pixels[i] = static_cast<unsigned>(std::lround(std::clamp(slice[i] * opacity, 0.0f, 1.0f) * 255)) << 24;
        return pixels;
    }

  private:
    bool Build(int width, int height, float dpi, float radius) {
        ReleaseBitmap();
        int tile = 0, cut = 0;
        auto slice = Slice(dpi, radius, 20, 48, .38f, tile, cut);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        void *raw = nullptr;
        bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        memory_ = CreateCompatibleDC(nullptr);
        if (!bitmap_ || !memory_ || !raw) {
            ReleaseBitmap();
            return false;
        }
        previous_ = SelectObject(memory_, bitmap_);
        auto *pixels = static_cast<unsigned *>(raw);
        auto source = [=](int n, int extent) {
            return n < cut ? n : n >= extent - cut ? tile - (extent - n) : cut + 4;
        };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                pixels[static_cast<size_t>(y) * width + x] = slice[source(y, height) * tile + source(x, width)];
            }
        return true;
    }

  public:
    ~AmbientShadow() {
        if (window_)
            DestroyWindow(window_);
        ReleaseBitmap();
    }
    void Hide() {
        if (window_)
            ShowWindow(window_, SW_HIDE);
    }
    void Position(HWND above, RECT panel, float dpi, float radius, anchor::Edge edge, float reveal, bool visible) {
        if (!visible || reveal <= 0) {
            Hide();
            return;
        }
        int margin = static_cast<int>(std::lround(48 * dpi / 96));
        int w = panel.right - panel.left + margin * 2, h = panel.bottom - panel.top + margin * 2;
        if (w <= margin * 2 || h <= margin * 2)
            return;
        if (!window_) {
            WNDCLASSW type{};
            type.lpfnWndProc = Procedure;
            type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = L"Dako.AmbientShadow";
            RegisterClassW(&type);
            window_ = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                type.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, type.hInstance, nullptr);
        }
        if (!window_)
            return;
        std::array<int, 4> key{w, h, static_cast<int>(dpi), static_cast<int>(std::lround(radius * 2))};
        bool rebuilt = key != key_ || !bitmap_;
        if (rebuilt) {
            if (!Build(w, h, dpi, radius))
                return;
            key_ = key;
        }
        POINT destination{panel.left - margin,
                          panel.top - margin + static_cast<LONG>(std::lround(10 * dpi / 96 * anchor::Grow(edge)))};
        BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(reveal, 0.0f, 1.0f) * 255));
        BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        SIZE size{w, h};
        POINT origin{};
        if (rebuilt || alpha != alpha_) {
            UpdateLayeredWindow(window_, nullptr, &destination, &size, memory_, &origin, 0, &blend, ULW_ALPHA);
            alpha_ = alpha;
        }
        SetWindowPos(window_, above, destination.x, destination.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
};
} // namespace graphics
