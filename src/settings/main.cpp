#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <fstream>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include "../SettingsCore.h"
#include "../SettingsIdentity.h"
#include "../CompositionSurface.h"
#include "Visuals.h"

using Microsoft::WRL::ComPtr;
namespace {
struct App {
    HWND window = nullptr, search = nullptr, navigation = nullptr;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    settingsui::Visuals visuals;
    int bodyTop = 80, footerTop = 620, contentLeft = 244, contentWidth = 560, viewport = 500;
    bool discard = false;
    int observedScroll = 0;
    ULONGLONG scrollDeadline = 0, scrollFadeStart = 0;
    HWND restoreFocus = nullptr;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1RenderTarget> target;
    CompositionSurface composition;
    ComPtr<IDWriteFactory> writer;
    ComPtr<IDWriteTextFormat> format;
    ComPtr<IAccPropServices> accessible;
    std::wstring storePath;
    v4::Values values;
    struct Row {
        const v4::Setting *setting;
        HWND control;
    };
    std::vector<Row> rows;
    int page = 0, scroll = 0;
    bool rebuilding = false, dirty = false;
    SIZE shapeSize{};
    UINT shapeDpi = 0;
    std::wstring status;
    const std::vector<std::wstring> pages{L"General", L"Island",    L"Behavior",  L"Overlay", L"Appearance", L"Apps",
                                          L"Content", L"Clipboard", L"Wallpaper", L"Privacy", L"Advanced"};
    explicit App(std::wstring path = {}) : storePath(std::move(path)), values(v4::ReadValues(storePath)) {
        if (values.Bool(L"Offline"))
            status = L"Offline is on. Privacy: set a city for weather; allow speed tests, then turn Offline off.";
    }
    ~App() {
        if (font)
            DeleteObject(font);
        DeleteObject(background);
    }
    static std::wstring Text(HWND control) {
        int n = GetWindowTextLengthW(control);
        std::wstring value(n + 1, L'\0');
        GetWindowTextW(control, value.data(), n + 1);
        value.resize(n);
        return value;
    }
    void Appearance() {
        DWORD percent = 100, bytes = sizeof(percent), light = 0;
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Accessibility)", L"TextScaleFactor", RRF_RT_REG_DWORD,
                     nullptr, &percent, &bytes);
        bytes = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
        visuals.scale = std::clamp(percent / 100.0f, 1.0f, 2.25f);
        bool paper = values.Text(L"Theme") == L"Paper" || (values.Text(L"Theme") == L"System" && light);
        visuals.Configure(paper, values.Bool(L"ReducedMotion"));
        if (!visuals.highContrast)
            visuals.theme.accent = static_cast<unsigned>(wcstoul(values.Text(L"Accent").c_str(), nullptr, 16));
        if (background)
            DeleteObject(background);
        background = CreateSolidBrush(settingsui::Gdi(visuals.inset));
        if (font)
            DeleteObject(font);
        font = CreateFontW(
            -static_cast<int>(std::lround(type::Sizes[type::Body] * GetDpiForWindow(window) / 96 * visuals.scale)), 0,
            0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Rubik");
        if (navigation)
            SendMessageW(navigation, LB_SETITEMHEIGHT, 0,
                         static_cast<LPARAM>(std::lround(settingsui::Metrics::Button * GetDpiForWindow(window) / 96.0f *
                                                         visuals.scale)));
        EnumChildWindows(
            window,
            [](HWND child, LPARAM context) -> BOOL {
                SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(context), TRUE);
                InvalidateRect(child, nullptr, TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font));
        BOOL dark = !paper && !visuals.highContrast;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        target.Reset();
        composition.Reset();
    }
    void Preview(const Row &row) {
        v4::Preview packet;
        wcscpy_s(packet.key, row.setting->key);
        wcscpy_s(packet.value, values.Text(row.setting->key).c_str());
        if (row.setting->live && storePath.empty())
            v4::SendPreview(packet);
    }
    bool Collect() {
        for (auto &row : rows) {
            auto &s = *row.setting;
            auto value = s.kind == v4::Kind::Boolean
                             ? (SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0")
                         : s.kind == v4::Kind::Integer ? std::to_wstring(SendMessageW(row.control, TBM_GETPOS, 0, 0))
                                                       : Text(row.control);
            if (!v4::Valid(s, value)) {
                status = std::wstring(L"Check ") + s.label;
                InvalidateRect(window, nullptr, FALSE);
                return false;
            }
            values.data[s.key] = value;
        }
        return true;
    }
    void Build() {
        rebuilding = true;
        for (auto &row : rows) {
            if (accessible) {
                MSAAPROPID properties[]{PROPID_ACC_NAME, PROPID_ACC_DESCRIPTION};
                accessible->ClearHwndProps(row.control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, properties, 2);
            }
            DestroyWindow(row.control);
        }
        rows.clear();
        std::wstring query = Text(search);
        for (auto &c : query)
            c = static_cast<wchar_t>(towlower(c));
        int index = 0;
        for (const auto &s : v4::Schema) {
            auto label = std::wstring(s.label) + L" " + s.description;
            for (auto &c : label)
                c = static_cast<wchar_t>(towlower(c));
            if (query.empty() ? pages[page] != s.page : label.find(query) == label.npos)
                continue;
            DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
            const wchar_t *type = L"EDIT";
            if (s.kind == v4::Kind::Boolean) {
                type = L"BUTTON";
                style |= BS_AUTOCHECKBOX;
            } else if (s.kind == v4::Kind::Choice) {
                type = L"COMBOBOX";
                style |= CBS_DROPDOWNLIST | WS_VSCROLL;
            } else if (s.kind == v4::Kind::Integer) {
                type = TRACKBAR_CLASSW;
                style |= TBS_HORZ | TBS_NOTICKS;
            } else
                style |= ES_AUTOHSCROLL;
            HWND control =
                CreateWindowExW(0, type, L"", style, 0, 0, 1, 1, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + index++)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (s.kind == v4::Kind::Boolean)
                SendMessageW(control, BM_SETCHECK, values.Bool(s.key) ? BST_CHECKED : BST_UNCHECKED, 0);
            else if (s.kind == v4::Kind::Choice) {
                int choice = 0;
                for (const auto &item : v4::Split(s.choices)) {
                    SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
                    if (item == values.Text(s.key))
                        SendMessageW(control, CB_SETCURSEL, choice, 0);
                    ++choice;
                }
            } else if (s.kind == v4::Kind::Integer) {
                SendMessageW(control, TBM_SETRANGE, TRUE, MAKELPARAM(s.minimum, s.maximum));
                SendMessageW(control, TBM_SETPOS, TRUE, values.Int(s.key));
            } else {
                SendMessageW(control, EM_SETLIMITTEXT, 1024, 0);
                SetWindowTextW(control, values.Text(s.key).c_str());
            }
            if (s.kind == v4::Kind::Boolean)
                settingsui::Button::Attach(control, visuals, true);
            if (accessible) {
                accessible->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME,
                                           s.label);
                accessible->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                           PROPID_ACC_DESCRIPTION, s.description);
            }
            rows.push_back({&s, control});
        }
        rebuilding = false;
        Layout();
        InvalidateRect(window, nullptr, FALSE);
    }
    void Layout() {
        RECT outer{};
        GetWindowRect(window, &outer);
        UINT outerDpi = GetDpiForWindow(window);
        SIZE nextShape{outer.right - outer.left, outer.bottom - outer.top};
        if (factory && (nextShape.cx != shapeSize.cx || nextShape.cy != shapeSize.cy || outerDpi != shapeDpi)) {
            shapeSize = nextShape;
            shapeDpi = outerDpi;
            float scale = outerDpi / 96.0f;
            auto *path =
                visuals.geometry.Rounded(factory.Get(), nextShape.cx / scale, nextShape.cy / scale, shape::Surface);
            HRGN region = shape::Region(path, D2D1::Matrix3x2F::Scale(scale, scale));
            if (!SetWindowRgn(window, region, TRUE))
                DeleteObject(region);
        }
        RECT raw{};
        GetClientRect(window, &raw);
        int dpi = static_cast<int>(std::lround(GetDpiForWindow(window) * visuals.scale));
        RECT r{0, 0, MulDiv(raw.right, 96, static_cast<int>(dpi)), MulDiv(raw.bottom, 96, static_cast<int>(dpi))};
        auto move = [&](HWND control, int x, int y, int width, int height, BOOL redraw) {
            MoveWindow(control, MulDiv(x, static_cast<int>(dpi), 96), MulDiv(y, static_cast<int>(dpi), 96),
                       MulDiv(width, static_cast<int>(dpi), 96), MulDiv(height, static_cast<int>(dpi), 96), redraw);
        };
        using M = settingsui::Metrics;
        bool narrow = r.right < 640;
        contentLeft = narrow ? M::Padding : M::Sidebar + M::SectionGap;
        contentWidth = std::max(32, std::min(M::Content, static_cast<int>(r.right) - contentLeft - M::Padding));
        int navHeight = narrow ? std::min(108L, std::max(32L, r.bottom / 6)) : std::max(32L, r.bottom - 100);
        bodyTop = narrow ? 88 + navHeight + 16 : 80;
        int columns = std::max(1, std::min(7, contentWidth / 88));
        footerTop = r.bottom - 48 - ((7 + columns - 1) / columns) * 36;
        viewport = std::max(1, footerTop - bodyTop - 8);
        scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(rows.size()) * M::Row - viewport));
        int searchWidth = narrow ? contentWidth : M::Sidebar - 40;
        move(search, M::Padding + 32, 28, std::max(32, searchWidth - 72), M::Button, TRUE);
        move(GetDlgItem(window, 20), M::Padding + searchWidth - 36, 28, M::Button, M::Button, TRUE);
        ShowWindow(GetDlgItem(window, 20), Text(search).empty() || discard ? SW_HIDE : SW_SHOW);
        move(navigation, M::Padding, 76, narrow ? contentWidth : M::Sidebar - 40, navHeight, TRUE);
        for (size_t i = 0; i < rows.size(); ++i) {
            int y = bodyTop + static_cast<int>(i) * M::Row - scroll;
            move(rows[i].control, contentLeft + 16, y + 88,
                 rows[i].setting->kind == v4::Kind::Boolean ? M::Button : std::max(32, contentWidth - 32),
                 rows[i].setting->kind == v4::Kind::Choice ? 240 : M::Input, TRUE);
            ShowWindow(rows[i].control,
                       !discard && y + 88 >= bodyTop && y + 128 <= bodyTop + viewport ? SW_SHOW : SW_HIDE);
        }
        const int ids[]{10, 11, 12, 13, 14, 15, 17};
        for (int i = 0; i < 7; ++i)
            move(GetDlgItem(window, ids[i]), contentLeft + i % columns * (contentWidth / columns),
                 footerTop + i / columns * 36, contentWidth / columns - 4, M::Button, TRUE);
        for (int id : {18, 19}) {
            move(GetDlgItem(window, id), contentLeft + 16 + (id - 18) * std::max(32, (contentWidth - 44) / 2),
                 bodyTop + 104, std::max(32, (contentWidth - 44) / 2 - 4), M::Button, TRUE);
            ShowWindow(GetDlgItem(window, id), discard ? SW_SHOW : SW_HIDE);
        }
        for (int id : ids)
            EnableWindow(GetDlgItem(window, id), !discard);
        EnableWindow(search, !discard);
        EnableWindow(navigation, !discard);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        std::max(0, static_cast<int>(rows.size()) * M::Row),
                        static_cast<UINT>(viewport),
                        scroll,
                        0};
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        if (scroll != observedScroll) {
            observedScroll = scroll;
            scrollDeadline = GetTickCount64() + 800;
            scrollFadeStart = 0;
            SetTimer(window, 8, 800, nullptr);
        }
    }
    void Paint() {
        RECT raw{};
        GetClientRect(window, &raw);
        UINT dpi = static_cast<UINT>(std::lround(GetDpiForWindow(window) * visuals.scale));
        RECT r{0, 0, MulDiv(raw.right, 96, static_cast<int>(dpi)), MulDiv(raw.bottom, 96, static_cast<int>(dpi))};
        if (!target) {
            if (FAILED(composition.Create(window, factory.Get(), static_cast<UINT>(raw.right),
                                          static_cast<UINT>(raw.bottom), static_cast<float>(dpi), &target))) {
                ComPtr<ID2D1HwndRenderTarget> fallback;
                factory->CreateHwndRenderTarget(
                    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
                                                 static_cast<float>(dpi), static_cast<float>(dpi)),
                    D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(raw.right, raw.bottom)), &fallback);
                target = fallback;
            }
        }
        if (!target)
            return;
        target->BeginDraw();
        const auto &theme = visuals.theme;
        target->Clear(settingsui::Color(theme.surface));
        ComPtr<ID2D1SolidColorBrush> brush;
        target->CreateSolidColorBrush(settingsui::Color(theme.text), &brush);
        auto text = [&](const std::wstring &value, D2D1_RECT_F rect, type::Role role, unsigned color) {
            brush->SetColor(settingsui::Color(color));
            target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), visuals.typography->formats[role].Get(),
                              rect, brush.Get());
        };
        float left = static_cast<float>(contentLeft), right = left + contentWidth, top = static_cast<float>(bodyTop);
        float searchRight = static_cast<float>(settingsui::Metrics::Padding +
                                               (r.right < 640 ? contentWidth : settingsui::Metrics::Sidebar - 40));
        brush->SetColor(settingsui::Color(visuals.inset));
        D2D1_RECT_F searchWell{20, 24, searchRight, 64};
        graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, searchWell, shape::Control, brush.Get());
        text(L"\xE721", {28, 24, 48, 64}, type::Icon, theme.muted);
        if (GetFocus() == search) {
            brush->SetColor(settingsui::Color(theme.accent));
            graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, searchWell, shape::Control, brush.Get(),
                              visuals.highContrast ? 2.0f : 1.5f);
        }
        if (r.right >= 640)
            text(L"Dako settings", {left, 24, right, 56}, type::Title, theme.text);
        target->PushAxisAlignedClip({left, top, right, top + viewport}, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < rows.size(); ++i) {
            float y = top + static_cast<float>(i) * settingsui::Metrics::Row - scroll;
            D2D1_RECT_F card{left, y, right, y + settingsui::Metrics::Card};
            brush->SetColor(settingsui::Color(theme.card));
            graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, card, shape::Card, brush.Get());
            brush->SetColor(settingsui::Color(visuals.edge, visuals.highContrast ? 1 : palette::Tokens::HairlineInner));
            if (visuals.highContrast)
                graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, card, shape::Card, brush.Get(), 2);
            else {
                float extent = shape::Extent({card.left, card.top, card.right, card.bottom}, shape::Card);
                target->DrawLine({left + extent, y + .5f}, {right - extent, y + .5f}, brush.Get(), 96.0f / dpi);
            }
            text(std::wstring(rows[i].setting->label) +
                     (rows[i].setting->kind == v4::Kind::Integer ? L" · " + values.Text(rows[i].setting->key) : L""),
                 {left + 16, y + 12, right - 16, y + 36}, type::Title, theme.text);
            text(rows[i].setting->description, {left + 16, y + 40, right - 16, y + 80}, type::Caption, theme.muted);
            if (rows[i].setting->kind != v4::Kind::Boolean) {
                brush->SetColor(settingsui::Color(visuals.inset));
                D2D1_RECT_F well{left + 12, y + 84, right - 12, y + 132};
                graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, well, shape::Control, brush.Get());
                if (GetFocus() == rows[i].control) {
                    brush->SetColor(settingsui::Color(theme.accent));
                    graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, well, shape::Control, brush.Get(),
                                      visuals.highContrast ? 2.0f : 1.5f);
                }
            }
        }
        if (discard) {
            brush->SetColor(settingsui::Color(theme.surface, visuals.highContrast ? 1.0f : .6f));
            target->FillRectangle({left, top, right, top + viewport}, brush.Get());
            brush->SetColor(settingsui::Color(theme.hover));
            graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, {left, top, right, top + 160},
                              shape::Surface, brush.Get());
            text(L"Discard unsaved settings?", {left + 16, top + 12, right - 16, top + 44}, type::Title, theme.text);
            text(L"Your saved settings will be kept.", {left + 16, top + 48, right - 16, top + 88}, type::Caption,
                 theme.muted);
        }
        target->PopAxisAlignedClip();
        float total = static_cast<float>(rows.size() * settingsui::Metrics::Row);
        if (!discard && total > viewport) {
            if (!visuals.highContrast) {
                D2D1_GRADIENT_STOP stops[]{{0, settingsui::Color(theme.surface)},
                                           {1, settingsui::Color(theme.surface, 0)}};
                ComPtr<ID2D1GradientStopCollection> collection;
                ComPtr<ID2D1LinearGradientBrush> fade;
                if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, &collection)) &&
                    SUCCEEDED(target->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties({0, top}, {0, top + 16}), collection.Get(), &fade))) {
                    if (scroll > 0)
                        target->FillRectangle({left, top, right, top + 16}, fade.Get());
                    if (scroll + viewport < total) {
                        fade->SetStartPoint({0, top + viewport});
                        fade->SetEndPoint({0, top + viewport - 16});
                        target->FillRectangle({left, top + viewport - 16, right, top + viewport}, fade.Get());
                    }
                }
            }
            float opacity = visuals.highContrast ? 1.0f
                            : scrollFadeStart
                                ? std::clamp(1 - (GetTickCount64() - scrollFadeStart) / 200.0f, 0.0f, 1.0f)
                            : scrollDeadline ? 1.0f
                                             : 0;
            float length = std::max(24.0f, viewport * viewport / total);
            float y = top + (viewport - length) * scroll / (total - viewport);
            brush->SetColor(settingsui::Color(theme.muted, opacity));
            graphics::Rounded(target.Get(), factory.Get(), visuals.geometry, {right - 3, y, right, y + length},
                              shape::Pill(3), brush.Get());
        }
        text(status, {left, static_cast<float>(r.bottom - 44), right, static_cast<float>(r.bottom - 4)}, type::Caption,
             theme.muted);
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) {
            target.Reset();
            composition.Reset();
        } else if (composition.Active())
            composition.Present();
    }
    void File(bool importing) {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window;
        dialog.lpstrFile = path;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"Dako settings\0*.ini\0";
        dialog.lpstrDefExt = L"ini";
        dialog.Flags = OFN_PATHMUSTEXIST | (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
        if (!(importing ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog)))
            return;
        bool ok = importing ? SettingsStore(storePath).Import(path) : SettingsStore(storePath).Export(path);
        status = ok ? L"Completed. Exports include private feed URLs and snippets."
                    : L"Could not transfer settings; the committed file is unchanged.";
        if (ok && importing) {
            values = v4::ReadValues(storePath);
            if (!v4::SaveValues(values, storePath))
                status = L"Imported preferences, but could not apply Windows startup. Check Startup and Apply again.";
            Appearance();
            Build();
        }
        InvalidateRect(window, nullptr, FALSE);
    }
};
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app->factory.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown **>(app->writer.GetAddressOf()));
        app->visuals.factory = app->factory;
        app->visuals.typography = type::Acquire(app->writer.Get());
        app->Appearance();
        CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&app->accessible));
        app->search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 1, 1,
                                      window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        SendMessageW(app->search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search settings"));
        if (app->accessible)
            app->accessible->SetHwndPropStr(app->search, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                            PROPID_ACC_NAME, L"Search settings");
        app->navigation = CreateWindowExW(
            0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            0, 0, 1, 1, window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        for (const auto &page : app->pages)
            SendMessageW(app->navigation, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(page.c_str()));
        SendMessageW(app->navigation, LB_SETCURSEL, 0, 0);
        const wchar_t *buttons[]{L"Apply", L"Reset page", L"Reset all", L"Export", L"Import", L"Close"};
        for (int i = 0; i < 6; ++i) {
            HWND control =
                CreateWindowExW(0, L"BUTTON", buttons[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(10 + i)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
            settingsui::Button::Attach(control, app->visuals);
        }
        HWND diagnostic = CreateWindowExW(0, L"BUTTON", L"Diagnostics", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
                                          window, reinterpret_cast<HMENU>(17), nullptr, nullptr);
        SendMessageW(diagnostic, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
        settingsui::Button::Attach(diagnostic, app->visuals);
        for (int id : {18, 19, 20}) {
            HWND control = CreateWindowExW(0, L"BUTTON",
                                           id == 18   ? L"Cancel"
                                           : id == 19 ? L"Discard"
                                                      : L"Clear",
                                           WS_CHILD | WS_TABSTOP, 0, 0, 1, 1, window,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
            settingsui::Button::Attach(control, app->visuals);
        }
        SendMessageW(app->navigation, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
        SendMessageW(app->navigation, LB_SETITEMHEIGHT, 0,
                     static_cast<LPARAM>(std::lround(settingsui::Metrics::Button * GetDpiForWindow(window) / 96.0f *
                                                     app->visuals.scale)));
        SendMessageW(app->search, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
        app->Build();
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int position = app->scroll;
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            position -= 48;
            break;
        case SB_LINEDOWN:
            position += 48;
            break;
        case SB_PAGEUP:
            position -= 300;
            break;
        case SB_PAGEDOWN:
            position += 300;
            break;
        case SB_THUMBTRACK:
            position = info.nTrackPos;
            break;
        }
        app->scroll = std::clamp(
            position, 0, std::max(0, static_cast<int>(app->rows.size()) * settingsui::Metrics::Row - app->viewport));
        app->Layout();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_DPICHANGED: {
        auto rect = reinterpret_cast<RECT *>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->Appearance();
        app->Layout();
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        app->Appearance();
        app->Layout();
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_SIZE:
        app->target.Reset();
        app->composition.Reset();
        app->Layout();
        return 0;
    case WM_TIMER:
        if (wparam == 8) {
            auto now = GetTickCount64();
            KillTimer(window, 8);
            if (now >= app->scrollDeadline) {
                if (!app->scrollFadeStart)
                    app->scrollFadeStart = now;
                if (now - app->scrollFadeStart < 200 && !app->visuals.reducedMotion)
                    SetTimer(window, 8, 16, nullptr);
                else
                    app->scrollDeadline = app->scrollFadeStart = 0;
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(lparam);
        float scale = GetDpiForWindow(window) / 96.0f * app->visuals.scale;
        info->ptMinTrackSize = {static_cast<LONG>(360 * scale), static_cast<LONG>(420 * scale)};
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        app->Paint();
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT *>(lparam)->itemHeight = static_cast<UINT>(
            std::lround(settingsui::Metrics::Button * GetDpiForWindow(window) / 96.0f * app->visuals.scale));
        return TRUE;
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<DRAWITEMSTRUCT *>(lparam);
        if (item->CtlID != 2 || item->itemID >= app->pages.size())
            break;
        const auto &theme = app->visuals.theme;
        unsigned fill = item->itemState & ODS_SELECTED
                            ? dock::Mix(theme.card, theme.accent, palette::Tokens::AccentSoft)
                            : app->visuals.inset;
        auto background = CreateSolidBrush(settingsui::Gdi(fill));
        FillRect(item->hDC, &item->rcItem, background);
        DeleteObject(background);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, settingsui::Gdi(theme.text));
        auto old = SelectObject(item->hDC, app->font);
        RECT text = item->rcItem;
        text.left += MulDiv(12, static_cast<int>(GetDpiForWindow(window)), 96);
        DrawTextW(item->hDC, app->pages[item->itemID].c_str(), -1, &text,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (item->itemState & ODS_FOCUS)
            DrawFocusRect(item->hDC, &item->rcItem);
        SelectObject(item->hDC, old);
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), settingsui::Gdi(app->visuals.theme.text));
        SetBkColor(reinterpret_cast<HDC>(wparam), settingsui::Gdi(app->visuals.inset));
        return reinterpret_cast<LRESULT>(app->background);
    case WM_MOUSEWHEEL:
        app->scroll =
            std::clamp(app->scroll - GET_WHEEL_DELTA_WPARAM(wparam) / 120 * 72, 0,
                       std::max(0, static_cast<int>(app->rows.size()) * settingsui::Metrics::Row - app->viewport));
        app->Layout();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_HSCROLL: {
        int id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
        if (id >= 100 && id < 100 + static_cast<int>(app->rows.size())) {
            auto &row = app->rows[id - 100];
            app->values.data[row.setting->key] = std::to_wstring(SendMessageW(row.control, TBM_GETPOS, 0, 0));
            app->dirty = true;
            app->Preview(row);
            if (LOWORD(wparam) == TB_ENDTRACK) {
                auto committed = v4::ReadValues(app->storePath);
                committed.data[row.setting->key] = app->values.Text(row.setting->key);
                if (v4::SaveValues(committed, app->storePath)) {
                    app->status = L"Slider saved.";
                    app->dirty = app->values.data != v4::ReadValues(app->storePath).data;
                } else
                    app->status = L"Could not save slider.";
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (app->rebuilding)
            return 0;
        int id = LOWORD(wparam), notification = HIWORD(wparam);
        if (id == 20 && !app->discard) {
            SetWindowTextW(app->search, L"");
            SetFocus(app->search);
            return 0;
        }
        if (app->discard) {
            if (id == 18) {
                app->discard = false;
                SetWindowTextW(window, L"Dako settings");
                app->Layout();
                if (IsWindow(app->restoreFocus))
                    SetFocus(app->restoreFocus);
                InvalidateRect(window, nullptr, FALSE);
            } else if (id == 19) {
                app->dirty = false;
                SendMessageW(window, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        if (id == 1 && notification == EN_CHANGE) {
            app->Collect();
            app->scroll = 0;
            app->Build();
            return 0;
        }
        if (id == 2 && notification == LBN_SELCHANGE) {
            if (!app->Collect())
                return 0;
            app->page = static_cast<int>(SendMessageW(app->navigation, LB_GETCURSEL, 0, 0));
            app->scroll = 0;
            app->Build();
            return 0;
        }
        if (id >= 100 && id < 100 + static_cast<int>(app->rows.size()) &&
            (notification == EN_CHANGE || notification == CBN_SELCHANGE || notification == BN_CLICKED)) {
            auto &row = app->rows[id - 100];
            auto value = row.setting->kind == v4::Kind::Boolean
                             ? (SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0")
                             : App::Text(row.control);
            if (v4::Valid(*row.setting, value)) {
                app->values.data[row.setting->key] = value;
                app->dirty = true;
                app->Preview(row);
                if (wcscmp(row.setting->key, L"Theme") == 0 || wcscmp(row.setting->key, L"ReducedMotion") == 0 ||
                    wcscmp(row.setting->key, L"Accent") == 0) {
                    app->Appearance();
                    app->Layout();
                }
            }
            return 0;
        }
        if (id == 10 && app->Collect()) {
            bool ok = v4::SaveValues(app->values, app->storePath);
            app->status = ok ? L"Settings saved." : L"Could not save settings. Previous settings remain.";
            if (ok)
                app->dirty = false;
            InvalidateRect(window, nullptr, FALSE);
        }
        if (id == 11 || id == 12) {
            for (const auto &s : v4::Schema)
                if (id == 12 || app->pages[app->page] == s.page)
                    app->values.data[s.key] = s.value;
            app->dirty = true;
            app->status = L"Reset staged. Choose Apply to save.";
            app->Appearance();
            app->Build();
        }
        if (id == 17) {
            wchar_t path[32768]{};
            OPENFILENAMEW dialog{sizeof(dialog)};
            dialog.hwndOwner = window;
            dialog.lpstrFile = path;
            dialog.nMaxFile = 32768;
            dialog.lpstrFilter = L"Text report\0*.txt\0";
            dialog.lpstrDefExt = L"txt";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&dialog)) {
                std::wofstream report{std::filesystem::path(path)};
                report << L"Dako 4.0 diagnostics\nUnsigned native portable build\n";
                for (const auto &setting : v4::Schema)
                    if (setting.kind != v4::Kind::Text)
                        report << setting.key << L"=" << app->values.Text(setting.key) << L"\n";
                report << L"Text fields, private URLs, clipboard contents, paths and app identities are omitted.\n";
                app->status = report ? L"Redacted diagnostics exported." : L"Could not write diagnostics.";
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        if (id == 13 || id == 14)
            app->File(id == 14);
        if (id == 15)
            SendMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_CLOSE:
        if (app->dirty) {
            app->restoreFocus = GetFocus();
            app->discard = true;
            SetWindowTextW(window, L"Discard unsaved settings? — Dako settings");
            NotifyWinEvent(EVENT_SYSTEM_ALERT, window, OBJID_CLIENT, CHILDID_SELF);
            app->Layout();
            SetFocus(GetDlgItem(window, 18));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        {
            v4::Preview rollback;
            rollback.command = 1;
            if (app->storePath.empty())
                v4::SendPreview(rollback);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    v4::UserSecurity security;
    HANDLE singleton = CreateMutexW(&security.attributes, TRUE,
                                    (L"Local\\Dako.SettingsApp." + v4::UserSid() +
                                     (std::wstring_view(command).starts_with(L"--settings-file=")
                                          ? L".test-" + std::to_wstring(GetCurrentProcessId())
                                          : L""))
                                        .c_str());
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleton)
            CloseHandle(singleton);
        return 0;
    }
    std::wstring path;
    if (std::wstring_view(command).starts_with(L"--settings-file="))
        path = command + 16;
    if (path.size() > 1 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);
    SettingsStore(path).Migrate();
    App app(path);
    WNDCLASSW type{};
    type.lpfnWndProc = Procedure;
    type.hInstance = instance;
    type.lpszClassName = L"Dako.Settings";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    HWND window =
        CreateWindowExW(0, type.lpszClassName, L"Dako settings", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
                        CW_USEDEFAULT, CW_USEDEFAULT, 1040, 760, nullptr, nullptr, instance, &app);
    if (window) {
        ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (app.discard && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE)
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(18, BN_CLICKED), 0);
            else if (app.discard && message.message == WM_KEYDOWN && message.wParam == VK_RETURN)
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(GetFocus()) == 19 ? 19 : 18, BN_CLICKED), 0);
            else if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    ReleaseMutex(singleton);
    CloseHandle(singleton);
    app.target.Reset();
    app.composition.Reset();
    app.accessible.Reset();
    app.format.Reset();
    app.writer.Reset();
    app.factory.Reset();
    CoUninitialize();
    return window ? 0 : 1;
}
