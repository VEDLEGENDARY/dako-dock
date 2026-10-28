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

using Microsoft::WRL::ComPtr;
namespace {
struct App {
    HWND window = nullptr, search = nullptr, navigation = nullptr;
    HFONT font = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(22, 22, 23));
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
                style |= WS_BORDER | ES_AUTOHSCROLL;
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
        RECT raw{};
        GetClientRect(window, &raw);
        UINT dpi = GetDpiForWindow(window);
        RECT r{0, 0, MulDiv(raw.right, 96, static_cast<int>(dpi)), MulDiv(raw.bottom, 96, static_cast<int>(dpi))};
        auto move = [&](HWND control, int x, int y, int width, int height, BOOL redraw) {
            MoveWindow(control, MulDiv(x, static_cast<int>(dpi), 96), MulDiv(y, static_cast<int>(dpi), 96),
                       MulDiv(width, static_cast<int>(dpi), 96), MulDiv(height, static_cast<int>(dpi), 96), redraw);
        };
        move(search, 20, 24, 170, 30, TRUE);
        move(navigation, 20, 72, 170, r.bottom - 100, TRUE);
        for (size_t i = 0; i < rows.size(); ++i) {
            int y = 82 + static_cast<int>(i) * 108 - scroll;
            move(rows[i].control, r.right - 240, y + 10, 208, rows[i].setting->kind == v4::Kind::Choice ? 220 : 30,
                 TRUE);
            ShowWindow(rows[i].control, y >= 66 && y + 70 < r.bottom - 80 ? SW_SHOW : SW_HIDE);
        }
        const int ids[]{10, 11, 12, 13, 14, 15, 17};
        for (int i = 0; i < 7; ++i)
            move(GetDlgItem(window, ids[i]), 220 + i * 88, r.bottom - 52, 82, 30, TRUE);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        std::max(0, static_cast<int>(rows.size()) * 108),
                        static_cast<UINT>(std::max(1L, r.bottom - 170)),
                        scroll,
                        0};
        SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    void Paint() {
        RECT raw{};
        GetClientRect(window, &raw);
        UINT dpi = GetDpiForWindow(window);
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
        target->Clear(D2D1::ColorF(0x0B0B0C));
        ComPtr<ID2D1SolidColorBrush> brush;
        target->CreateSolidColorBrush(D2D1::ColorF(0xF2F2F2), &brush);
        auto text = [&](const std::wstring &value, D2D1_RECT_F rect, unsigned color = 0xF2F2F2) {
            brush->SetColor(D2D1::ColorF(color));
            target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format.Get(), rect, brush.Get());
        };
        text(L"Dako settings", {220, 22, static_cast<float>(r.right - 25), 56}, 0xD4B35C);
        target->PushAxisAlignedClip({212, 66, static_cast<float>(r.right - 12), static_cast<float>(r.bottom - 80)},
                                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < rows.size(); ++i) {
            float y = 82 + static_cast<float>(i) * 108 - scroll;
            brush->SetColor(D2D1::ColorF(0x161617));
            target->FillRoundedRectangle(D2D1::RoundedRect({220, y, static_cast<float>(r.right - 20), y + 94}, 12, 12),
                                         brush.Get());
            text(std::wstring(rows[i].setting->label) +
                     (rows[i].setting->kind == v4::Kind::Integer ? L" · " + values.Text(rows[i].setting->key) : L""),
                 {236, y + 8, static_cast<float>(r.right - 258), y + 40});
            text(rows[i].setting->description, {236, y + 42, static_cast<float>(r.right - 258), y + 88}, 0xA0A0A6);
        }
        target->PopAxisAlignedClip();
        text(status,
             {220, static_cast<float>(r.bottom - 78), static_cast<float>(r.right - 20),
              static_cast<float>(r.bottom - 54)},
             0xD4B35C);
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
        app->font = CreateFontW(-MulDiv(15, static_cast<int>(GetDpiForWindow(window)), 96), 0, 0, 0, FW_NORMAL, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, app->factory.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown **>(app->writer.GetAddressOf()));
        app->writer->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, 14, L"en-us", &app->format);
        CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&app->accessible));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        app->search =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0,
                            1, 1, window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        SendMessageW(app->search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search settings"));
        app->navigation = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY, 0, 0, 1,
                                          1, window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        for (const auto &page : app->pages)
            SendMessageW(app->navigation, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(page.c_str()));
        SendMessageW(app->navigation, LB_SETCURSEL, 0, 0);
        const wchar_t *buttons[]{L"Apply", L"Reset page", L"Reset all", L"Export", L"Import", L"Close"};
        for (int i = 0; i < 6; ++i) {
            HWND control =
                CreateWindowExW(0, L"BUTTON", buttons[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, window,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(10 + i)), nullptr, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
        }
        HWND diagnostic = CreateWindowExW(0, L"BUTTON", L"Diagnostics", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1,
                                          window, reinterpret_cast<HMENU>(17), nullptr, nullptr);
        SendMessageW(diagnostic, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
        SendMessageW(app->navigation, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
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
        app->scroll = std::clamp(position, 0, std::max(0, static_cast<int>(app->rows.size()) * 108 - 350));
        app->Layout();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_DPICHANGED: {
        auto rect = reinterpret_cast<RECT *>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->target.Reset();
        app->composition.Reset();
        app->Layout();
        return 0;
    }
    case WM_SIZE:
        app->target.Reset();
        app->composition.Reset();
        app->Layout();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        app->Paint();
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), RGB(242, 242, 242));
        SetBkColor(reinterpret_cast<HDC>(wparam), RGB(22, 22, 23));
        return reinterpret_cast<LRESULT>(app->background);
    case WM_MOUSEWHEEL:
        app->scroll = std::clamp(app->scroll - GET_WHEEL_DELTA_WPARAM(wparam) / 120 * 72, 0,
                                 std::max(0, static_cast<int>(app->rows.size()) * 108 - 350));
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
        if (app->dirty &&
            MessageBoxW(window, L"Discard unsaved settings?", L"Dako settings", MB_YESNO | MB_DEFBUTTON2) != IDYES)
            return 0;
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
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
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
