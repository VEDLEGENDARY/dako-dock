#pragma once
#include "../UiTypography.h"
#include "../DockMotion.h"
#include <chrono>

namespace settingsui {
using Microsoft::WRL::ComPtr;
struct Metrics {
    static constexpr int Sidebar = 200, Content = 560, SectionGap = 24, Padding = 20;
    static constexpr int Row = 168, Card = 144, Input = 40, Button = 32, Fade = 16;
    static constexpr float SwitchWidth = 28, SwitchHeight = 16, Knob = 12;
};
inline D2D1_COLOR_F Color(unsigned rgb, float alpha = 1) { return D2D1::ColorF(rgb, alpha); }
inline COLORREF Gdi(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
struct Visuals {
    palette::ThemeSet theme = palette::Midnight;
    unsigned inset = palette::Tokens::Inset, edge = palette::Tokens::White;
    bool highContrast = false, reducedMotion = false;
    float scale = 1;
    std::shared_ptr<type::Library> typography;
    ComPtr<ID2D1Factory> factory;
    void Configure(bool paper, bool reduced) {
        HIGHCONTRASTW hc{sizeof(hc)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
        highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        BOOL animation = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
        reducedMotion = reduced || !animation || highContrast;
        theme = paper ? palette::Paper : palette::Midnight;
        inset = paper ? palette::Paper.surface : palette::Tokens::Inset;
        edge = paper ? palette::Tokens::Black : palette::Tokens::White;
        if (highContrast) {
            auto system = [](int index) -> unsigned {
                auto c = GetSysColor(index);
                return (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c);
            };
            theme = {system(COLOR_WINDOW),     system(COLOR_WINDOW),     system(COLOR_BTNFACE),
                     system(COLOR_WINDOWTEXT), system(COLOR_WINDOWTEXT), system(COLOR_WINDOWTEXT),
                     system(COLOR_HIGHLIGHT)};
            inset = theme.surface;
            edge = theme.text;
        }
    }
};
// Preserve the native BUTTON's check state, keyboard behavior and accessibility.
// Painting and bounded motion are shared with the island's tokens and springs.
struct Button {
    Visuals &visuals;
    bool toggle, hover = false, pressed = false;
    dock::Spring knob{0}, highlight{0}, press{0};
    std::chrono::steady_clock::time_point last;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    explicit Button(Visuals &v, bool isToggle) : visuals(v), toggle(isToggle) {}
    void Motion(HWND window) {
        knob.goal = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1.0f : 0.0f;
        highlight.goal = hover ? 1.0f : 0.0f;
        press.goal = pressed ? 1.0f : 0.0f;
        if (visuals.reducedMotion) {
            for (auto *value : {&knob, &highlight, &press})
                value->Snap(value->goal);
            KillTimer(window, 1);
        } else {
            last = std::chrono::steady_clock::now();
            SetTimer(window, 1, 8, nullptr);
        }
        InvalidateRect(window, nullptr, FALSE);
    }
    void Paint(HWND window) {
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        float dpi = GetDpiForWindow(window) * visuals.scale;
        if (!target) {
            visuals.factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi),
                D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(r.right, r.bottom)), &target);
            if (target)
                target->CreateSolidColorBrush(Color(visuals.theme.text), &brush);
        }
        if (target) {
            auto &t = visuals.theme;
            auto size = target->GetSize();
            target->BeginDraw();
            target->Clear(Color(toggle ? t.card : t.surface));
            auto rounded = [&](D2D1_RECT_F rect, float radius, unsigned color) {
                brush->SetColor(Color(color));
                target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
                if (visuals.highContrast) {
                    brush->SetColor(Color(t.text));
                    target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get(), 2);
                }
            };
            D2D1_RECT_F bounds{1, 1, size.width - 1, size.height - 1};
            if (toggle) {
                float x = (size.width - Metrics::SwitchWidth) / 2, y = (size.height - Metrics::SwitchHeight) / 2;
                rounded({x, y, x + Metrics::SwitchWidth, y + Metrics::SwitchHeight}, Metrics::SwitchHeight / 2,
                        dock::Mix(dock::Mix(t.hover, t.accent, knob.value), t.text, highlight.value * .08f));
                float cx = x + Metrics::SwitchHeight / 2 + (Metrics::SwitchWidth - Metrics::SwitchHeight) * knob.value;
                auto hcInk = GetSysColor(COLOR_HIGHLIGHTTEXT);
                unsigned ink = visuals.highContrast
                                   ? ((GetRValue(hcInk) << 16) | (GetGValue(hcInk) << 8) | GetBValue(hcInk))
                               : palette::ToHsl(t.accent).l > .5f ? palette::Tokens::Black
                                                                  : palette::Tokens::White;
                brush->SetColor(Color(knob.value > .5f ? ink : t.text));
                target->FillEllipse(D2D1::Ellipse({cx, y + Metrics::SwitchHeight / 2},
                                                  Metrics::Knob / 2 * (1 - .03f * press.value), Metrics::Knob / 2),
                                    brush.Get());
            } else {
                rounded(bounds, layout::Metrics::ControlRadius, dock::Mix(t.card, t.hover, highlight.value));
                wchar_t text[128]{};
                GetWindowTextW(window, text, 128);
                bool clear = GetDlgCtrlID(window) == 20;
                if (clear)
                    wcscpy_s(text, L"\xE711");
                brush->SetColor(Color(IsWindowEnabled(window) ? t.text : t.muted));
                auto format = visuals.typography->formats[clear ? type::Icon : type::Label];
                auto old = format->GetTextAlignment();
                format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                target->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format.Get(), bounds, brush.Get());
                format->SetTextAlignment(old);
            }
            if (GetFocus() == window || visuals.highContrast) {
                brush->SetColor(Color(GetFocus() == window ? t.accent : t.text));
                target->DrawRoundedRectangle(
                    D2D1::RoundedRect(bounds, layout::Metrics::ControlRadius, layout::Metrics::ControlRadius),
                    brush.Get(), visuals.highContrast ? 2.0f : 1.5f);
            }
            if (target->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                target.Reset();
            }
        }
        EndPaint(window, &ps);
    }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                      DWORD_PTR context) {
        auto *self = reinterpret_cast<Button *>(context);
        if (message == WM_NCDESTROY) {
            KillTimer(window, 1);
            RemoveWindowSubclass(window, Procedure, 1);
            delete self;
            return DefSubclassProc(window, message, wparam, lparam);
        }
        if (message == WM_PAINT) {
            self->Paint(window);
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_SIZE || message == WM_DPICHANGED_AFTERPARENT || message == WM_SETFONT) {
            self->brush.Reset();
            self->target.Reset();
        }
        if (message == WM_MOUSEMOVE && !self->hover) {
            self->hover = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            self->Motion(window);
        }
        if (message == WM_MOUSELEAVE) {
            self->hover = false;
            self->Motion(window);
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_CAPTURECHANGED) {
            self->pressed = message == WM_LBUTTONDOWN;
            self->Motion(window);
        }
        if (message == WM_TIMER && wparam == 1) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - self->last).count();
            self->last = now;
            bool moving = false;
            for (auto *value : {&self->knob, &self->highlight, &self->press}) {
                value->Step(elapsed, dock::motion::Snappy);
                moving |= value->Moving();
            }
            if (!moving)
                KillTimer(window, 1);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        auto result = DefSubclassProc(window, message, wparam, lparam);
        if (message == BM_SETCHECK || message == BM_CLICK || message == WM_KEYUP || message == WM_LBUTTONUP ||
            message == WM_SETFOCUS || message == WM_KILLFOCUS)
            self->Motion(window);
        return result;
    }
    static void Attach(HWND window, Visuals &visuals, bool toggle = false) {
        auto *button = new Button(visuals, toggle);
        SetWindowSubclass(window, Procedure, 1, reinterpret_cast<DWORD_PTR>(button));
        button->knob.Snap(SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1.0f : 0.0f);
    }
};
} // namespace settingsui
