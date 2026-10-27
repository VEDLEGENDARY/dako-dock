#include "DockShell.h"
#include "DockView.h"
#include "DockAccessible.h"
#include "DesktopShell.h"
#include "CompositionSurface.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shellscalingapi.h>
#include <filesystem>
#include <map>
#include <set>

namespace desktop {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT_PTR ClockTimer = 1, FlashTimer = 2, PreviewTimer = 3, MotionTimer = 4;
constexpr wchar_t DockClass[] = L"Dako.Native.Dock", PreviewClass[] = L"Dako.Native.WindowPreview";
DockStatus ReadClock(DockStatus status) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t time[64]{}, date[96]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, 64);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &now, nullptr, date, 96, nullptr);
    status.clock = time;
    status.date = date;
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power))
        status.battery = power.BatteryFlag == 128          ? L"AC"
                         : power.BatteryLifePercent <= 100 ? std::to_wstring(power.BatteryLifePercent) + L"%"
                                                           : L"?";
    return status;
}
}
struct DockShell::Impl {
    struct DockSurface {
        Impl *shell = nullptr;
        HWND window = nullptr, tooltip = nullptr;
        HMONITOR monitor = nullptr;
        UINT dpi = 96;
        RECT bounds{};
        bool registered = false, positioning = false, fullscreen = false, hidden = false;
        HWND fullscreenOwner = nullptr;
        std::vector<DockGroup> groups;
        std::vector<DockHit> hits;
        std::vector<UINT_PTR> tools;
        std::map<std::wstring, LONG> children;
        LONG nextChild = 1000;
        int hover = -1, focus = -1, pressed = -1, drag = -1;
        POINT pressedPoint{};
        ULONGLONG pulseEnd = 0;
        CompositionSurface composition;
        ComPtr<ID2D1RenderTarget> target;
        dock::Spring magnify{0}, pointer{-1000};
        ULONGLONG motionTime = 0;
        size_t first = 0;
        DockLayout geometry;
        dockview::Resources resources;
        ComPtr<IAccessible> accessible;
        DockAccessible *provider = nullptr;
        float Scale() const { return dpi / 96.0f * shell->textScale; }
        float Width() const { return (bounds.right - bounds.left) / Scale(); }
    };
    HWND owner = nullptr, previewWindow = nullptr;
    Shell *recovery = nullptr;
    bool preview = false, reduced = false, contrast = false, stopping = false, updating = false;
    float textScale = 1;
    DockOptions options;
    Callbacks callbacks;
    std::vector<std::unique_ptr<DockSurface>> surfaces;
    std::vector<PinEntry> pins;
    std::vector<WindowEntry> windows;
    DockStatus status;
    std::set<HWND> flashing;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writer;
    HTHUMBNAIL thumbnail = nullptr;
    DockGroup previewGroup;
    DockSurface *previewDock = nullptr;
    size_t previewIndex = 0;
    HRESULT previewResult = E_PENDING;
    ComPtr<ID2D1HwndRenderTarget> previewTarget;
    dockview::Resources previewResources;
    std::vector<DockHit> previewHits;
    int previewFocus = 0;
    ComPtr<IAccessible> previewAccessible;
    DockAccessible *previewProvider = nullptr;
    HMONITOR activeMonitor = nullptr;
    float previewWidth = 320, previewHeight = 252;

    void Error(const std::wstring &message) {
        if (callbacks.error)
            callbacks.error(message);
    }
    DockSurface *Find(HMONITOR monitor) {
        for (auto &bar : surfaces)
            if (bar->monitor == monitor)
                return bar.get();
        return surfaces.empty() ? nullptr : surfaces.front().get();
    }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto *bar = reinterpret_cast<DockSurface *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            bar = static_cast<DockSurface *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
            bar->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(bar));
        }
        if (!bar)
            return DefWindowProcW(window, message, wparam, lparam);
        return bar->shell->Message(*bar, message, wparam, lparam);
    }
    static LRESULT CALLBACK PreviewProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
            self->previewWindow = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(window, message, wparam, lparam);
        return self->PreviewMessage(message, wparam, lparam);
    }
    void Rebuild(DockSurface &bar) {
        if (!bar.window)
            return;
        for (auto tool : bar.tools) {
            TOOLINFOW info{sizeof(info)};
            info.hwnd = bar.window;
            info.uId = tool;
            SendMessageW(bar.tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        }
        bar.tools.clear();
        auto oldFocus = bar.focus;
        std::wstring focusKey =
            oldFocus >= DockGroupFirst && oldFocus - DockGroupFirst < static_cast<int>(bar.groups.size())
                ? bar.groups[oldFocus - DockGroupFirst].key
                : L"";
        bar.groups = BuildDockGroups(pins, windows, bar.monitor);
        for (auto &group : bar.groups)
            for (const auto &window : group.windows)
                group.flash |= flashing.contains(window.window);
        auto geometry =
            SolveDock(bar.Width(), bar.groups, status, options, bar.first, bar.pointer.value, bar.magnify.value);
        bar.first = geometry.firstGroup;
        bar.geometry = geometry;
        bar.hits = std::move(geometry.hits);
        auto pill = bar.geometry.pill;
        float pixels = bar.Scale();
        int radius = static_cast<int>(std::lround(pill.Height() * pixels));
        HRGN region = CreateRoundRectRgn(static_cast<int>(std::lround(pill.left * pixels)),
                                         static_cast<int>(std::lround(pill.top * pixels)),
                                         static_cast<int>(std::lround(pill.right * pixels)),
                                         static_cast<int>(std::lround(pill.bottom * pixels)), radius, radius);
        if (!SetWindowRgn(bar.window, region, TRUE))
            DeleteObject(region);
        for (auto &hit : bar.hits)
            if (hit.action >= DockGroupFirst) {
                const auto &group = bar.groups[hit.action - DockGroupFirst];
                auto [child, inserted] = bar.children.try_emplace(group.key, bar.nextChild);
                if (inserted)
                    ++bar.nextChild;
                hit.child = child->second;
                if (!focusKey.empty() && focusKey == group.key)
                    bar.focus = hit.action;
            }
        if (bar.children.size() > 4096) {
            for (auto it = bar.children.begin(); it != bar.children.end();) {
                bool exists = std::any_of(bar.groups.begin(), bar.groups.end(),
                                          [&](const DockGroup &group) { return group.key == it->first; });
                if (!exists)
                    it = bar.children.erase(it);
                else
                    ++it;
            }
        }
        for (auto &hit : bar.hits) {
            if (hit.action >= DockGroupFirst && !bar.groups[hit.action - DockGroupFirst].windows.empty())
                continue;
            TOOLINFOW info{sizeof(info)};
            info.uFlags = TTF_SUBCLASS;
            info.hwnd = bar.window;
            info.uId = hit.child;
            float scale = bar.Scale();
            info.rect = {static_cast<LONG>(hit.rect.left * scale), static_cast<LONG>(hit.rect.top * scale),
                         static_cast<LONG>(hit.rect.right * scale), static_cast<LONG>(hit.rect.bottom * scale)};
            info.lpszText = hit.name.data();
            SendMessageW(bar.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            bar.tools.push_back(info.uId);
        }
        InvalidateRect(bar.window, nullptr, FALSE);
    }
    bool Position(DockSurface &bar) {
        if (bar.positioning || !bar.window)
            return true;
        bar.positioning = true;
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(bar.monitor, &info)) {
            bar.positioning = false;
            return false;
        }
        UINT x = 96, y = 96;
        GetDpiForMonitor(bar.monitor, MDT_EFFECTIVE_DPI, &x, &y);
        bool dpiChanged = bar.dpi != x;
        bar.dpi = x;
        LONG height = DipPixels(72 * textScale, x);
        RECT rect = BottomDockRect(info.rcMonitor, x, 72 * textScale);
        if (preview) {
            rect.bottom = info.rcWork.bottom;
            rect.top = rect.bottom - height;
        } else {
            APPBARDATA data{sizeof(data)};
            data.hWnd = bar.window;
            data.uEdge = ABE_BOTTOM;
            data.rc = rect;
            SHAppBarMessage(ABM_QUERYPOS, &data);
            data.rc.top = data.rc.bottom - height;
            SHAppBarMessage(ABM_SETPOS, &data);
            rect = data.rc;
        }
        bool changed = !EqualRect(&rect, &bar.bounds);
        bar.bounds = rect;
        SetWindowPos(bar.window, bar.hidden ? HWND_BOTTOM : HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOACTIVATE);
        if (changed || dpiChanged || !bar.target) {
            bar.target.Reset();
            bar.resources.icons.clear();
            bar.composition.Reset();
            HRESULT rendered = bar.composition.Create(bar.window, factory.Get(), rect.right - rect.left,
                                                      rect.bottom - rect.top, static_cast<float>(bar.dpi), &bar.target);
            if (FAILED(rendered)) {
                bar.positioning = false;
                return false;
            }
        }
        bar.positioning = false;
        Rebuild(bar);
        if (changed && owner)
            PostMessageW(owner, DockChanged, 0, 0);
        return true;
    }
    void ArmClock() {
        for (auto &bar : surfaces)
            KillTimer(bar->window, ClockTimer);
        auto found = std::find_if(surfaces.begin(), surfaces.end(), [](const auto &bar) { return !bar->hidden; });
        if (found == surfaces.end())
            return;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        UINT delay = 60000 - now.wSecond * 1000 - now.wMilliseconds;
        SetTimer((*found)->window, ClockTimer, std::max(100U, delay), nullptr);
    }
    void Fullscreen() {
        if (stopping)
            return;
        QUERY_USER_NOTIFICATION_STATE notification = QUNS_ACCEPTS_NOTIFICATIONS;
        bool busy = !preview && SUCCEEDED(SHQueryUserNotificationState(&notification)) &&
                    HideForNotificationState(notification);
        HWND foregroundWindow = GetForegroundWindow();
        HMONITOR foreground = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTONULL);
        for (auto &bar : surfaces) {
            if (bar->fullscreen && bar->fullscreenOwner != foregroundWindow && !busy)
                bar->fullscreen = false;
            bool hide = !preview && (bar->fullscreen || (busy && bar->monitor == foreground));
            if (hide == bar->hidden)
                continue;
            bar->hidden = hide;
            if (!hide) {
                status = ReadClock(std::move(status));
                Rebuild(*bar);
            }
            ShowWindow(bar->window, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
            SetWindowPos(bar->window, hide ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (hide && previewDock == bar.get())
                HidePreview();
            if (hide && owner && bar->monitor == activeMonitor)
                PostMessageW(owner, DockChanged, 1, 0);
        }
        ArmClock();
    }
    void ArmMotion(DockSurface &bar) {
        if (reduced) {
            bar.magnify.Snap(0);
            bar.pointer.Snap(bar.pointer.goal);
            KillTimer(bar.window, MotionTimer);
            Rebuild(bar);
            return;
        }
        if (!bar.motionTime)
            bar.motionTime = GetTickCount64();
        SetTimer(bar.window, MotionTimer, 16, nullptr);
    }
    void Paint(DockSurface &bar) {
        PAINTSTRUCT paint{};
        BeginPaint(bar.window, &paint);
        if (bar.target) {
            bar.target->BeginDraw();
            bar.target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
            float pulse = 1;
            if (!reduced && bar.pulseEnd > GetTickCount64())
                pulse = .5f + .5f * std::cos((bar.pulseEnd - GetTickCount64()) / 1800.0f * 18.8495559f);
            dockview::Paint(bar.target.Get(), writer.Get(), bar.resources, bar.Width(), bar.groups, status, bar.hover,
                            GetFocus() == bar.window ? bar.focus : -1, pulse, options, bar.first, bar.pointer.value,
                            reduced ? 0 : bar.magnify.value, contrast);
            if (SUCCEEDED(bar.target->EndDraw()))
                bar.composition.Present();
        }
        EndPaint(bar.window, &paint);
    }
    int Hit(const DockSurface &bar, float x, float y) const {
        if (!CapsuleContains(bar.geometry.pill, x, y))
            return -1;
        for (const auto &hit : bar.hits)
            if (hit.rect.Contains(x, y))
                return hit.action;
        return -1;
    }
    void Select(DockSurface &bar, int action) {
        bar.focus = action;
        auto found =
            std::find_if(bar.hits.begin(), bar.hits.end(), [&](const DockHit &hit) { return hit.action == action; });
        if (found != bar.hits.end()) {
            if (bar.provider)
                bar.provider->Focus(found->child);
            NotifyWinEvent(EVENT_OBJECT_FOCUS, bar.window, OBJID_CLIENT, found->child);
        }
        InvalidateRect(bar.window, nullptr, FALSE);
    }
    void ActivateGroup(DockSurface &bar, size_t index, bool launch = false) {
        if (index >= bar.groups.size())
            return;
        auto group = bar.groups[index];
        activeMonitor = bar.monitor;
        HidePreview();
        if (launch || group.windows.empty()) {
            if (!group.launchKey.empty() && callbacks.launch)
                callbacks.launch(group.launchKey);
            return;
        }
        auto active = std::find_if(group.windows.begin(), group.windows.end(),
                                   [](const WindowEntry &entry) { return entry.window == GetForegroundWindow(); });
        if (group.windows.size() == 1 && active != group.windows.end())
            ShowWindowAsync(active->window, SW_MINIMIZE);
        else {
            size_t selected = active == group.windows.end()
                                  ? 0
                                  : (static_cast<size_t>(active - group.windows.begin()) + 1) % group.windows.size();
            if (!WindowList::Activate(group.windows[selected].window))
                Error(L"Windows did not allow activation. Elevated windows may require an elevated Dako.");
        }
    }
    void Overflow(DockSurface &bar) {
        if (bar.geometry.visibleGroups) {
            bar.first = bar.first + bar.geometry.visibleGroups >= bar.groups.size()
                            ? 0
                            : bar.first + bar.geometry.visibleGroups;
            HidePreview();
            bar.magnify.Snap(0);
            Rebuild(bar);
            return;
        }
        HMENU menu = CreatePopupMenu();
        for (size_t i = 0; i < bar.groups.size(); ++i)
            AppendMenuW(menu, MF_STRING, 100 + i, bar.groups[i].name.c_str());
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(bar.window);
        int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, bar.window, nullptr);
        DestroyMenu(menu);
        if (command >= 100)
            ActivateGroup(bar, static_cast<size_t>(command - 100));
    }
    void Invoke(DockSurface &bar, int action, bool launch = false) {
        activeMonitor = bar.monitor;
        if (action >= DockGroupFirst) {
            ActivateGroup(bar, static_cast<size_t>(action - DockGroupFirst), launch);
            return;
        }
        if (action == DockOverflow) {
            Overflow(bar);
            return;
        }
        HidePreview();
        if (action == DockDesktop || action == DockTaskView) {
            WORD key = action == DockDesktop ? 'D' : VK_TAB;
            INPUT input[4]{};
            for (auto &value : input)
                value.type = INPUT_KEYBOARD;
            input[0].ki.wVk = VK_LWIN;
            input[1].ki.wVk = key;
            input[2].ki.wVk = key;
            input[2].ki.dwFlags = KEYEVENTF_KEYUP;
            input[3].ki.wVk = VK_LWIN;
            input[3].ki.dwFlags = KEYEVENTF_KEYUP;
            if (SendInput(4, input, sizeof(INPUT)) != 4)
                Error(L"Windows did not allow the desktop shortcut.");
            return;
        }
        if (callbacks.open)
            callbacks.open(action, bar.monitor);
    }
    void AppMenu(DockSurface &bar, int action) {
        if (action < DockGroupFirst) {
            if (callbacks.context)
                callbacks.context();
            return;
        }
        size_t index = static_cast<size_t>(action - DockGroupFirst);
        if (index >= bar.groups.size())
            return;
        auto group = bar.groups[index];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (group.launchKey.empty() ? MF_GRAYED : 0), 1, L"Open another instance");
        AppendMenuW(menu, MF_STRING, 2, group.pinned ? L"Unpin" : L"Pin to dock");
        if (!group.windows.empty())
            AppendMenuW(menu, MF_STRING, 3, L"Close window");
        if (!group.appId.empty())
            AppendMenuW(menu, MF_STRING, 4, L"Recent and frequent documents…");
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(bar.window);
        int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, bar.window, nullptr);
        DestroyMenu(menu);
        if (command == 1)
            ActivateGroup(bar, index, true);
        if (command == 2 && callbacks.pin) {
            if (group.pinned)
                for (const auto &key : group.pinKeys)
                    callbacks.pin(key, false);
            else
                callbacks.pin(group.launchKey, true);
        }
        if (command == 4 && callbacks.recent)
            callbacks.recent(group.appId, bar.monitor);
        if (command == 3 && !WindowList::Close(group.windows.front().window))
            Error(L"Windows did not allow closing this window. Elevated windows may require an elevated Dako.");
    }
    void HidePreview() {
        if (thumbnail) {
            DwmUnregisterThumbnail(thumbnail);
            thumbnail = nullptr;
        }
        if (previewWindow) {
            KillTimer(previewWindow, PreviewTimer);
            ShowWindow(previewWindow, SW_HIDE);
        }
        previewGroup = {};
        previewDock = nullptr;
    }
    void UpdateThumbnail() {
        if (thumbnail) {
            DwmUnregisterThumbnail(thumbnail);
            thumbnail = nullptr;
        }
        previewResult = E_FAIL;
        if (!previewDock || previewIndex >= previewGroup.windows.size())
            return;
        HWND source = previewGroup.windows[previewIndex].window;
        previewResult = DwmRegisterThumbnail(previewWindow, source, &thumbnail);
        if (SUCCEEDED(previewResult)) {
            SIZE size{};
            DwmQueryThumbnailSourceSize(thumbnail, &size);
            float scale = previewDock->Scale();
            RECT content{static_cast<LONG>(12 * scale), static_cast<LONG>(42 * scale),
                         static_cast<LONG>((previewWidth - 12) * scale),
                         static_cast<LONG>((previewHeight - 48) * scale)};
            float ratio = std::min((content.right - content.left) / static_cast<float>(std::max(1L, size.cx)),
                                   (content.bottom - content.top) / static_cast<float>(std::max(1L, size.cy)));
            LONG width = static_cast<LONG>(size.cx * ratio), height = static_cast<LONG>(size.cy * ratio);
            LONG x = content.left + (content.right - content.left - width) / 2,
                 y = content.top + (content.bottom - content.top - height) / 2;
            DWM_THUMBNAIL_PROPERTIES properties{};
            properties.dwFlags =
                DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
            properties.rcDestination = {x, y, x + width, y + height};
            properties.fVisible = TRUE;
            properties.opacity = 255;
            properties.fSourceClientAreaOnly = FALSE;
            previewResult = DwmUpdateThumbnailProperties(thumbnail, &properties);
        }
        InvalidateRect(previewWindow, nullptr, FALSE);
    }
    void ShowPreview(DockSurface &bar, size_t group) {
        if (group >= bar.groups.size() || bar.groups[group].windows.empty())
            return;
        if (!previewWindow) {
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PreviewClass, L"Dako window preview",
                            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), this);
            if (!previewWindow)
                return;
        }
        HidePreview();
        previewDock = &bar;
        previewGroup = bar.groups[group];
        previewIndex = 0;
        previewFocus = 0;
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(bar.monitor, &info);
        float scale = bar.Scale();
        LONG width = static_cast<LONG>(320 * scale), height = static_cast<LONG>(252 * scale);
        width = std::min(width, info.rcMonitor.right - info.rcMonitor.left);
        height = std::min(height, std::max(80L, bar.bounds.top - info.rcMonitor.top - 8));
        previewWidth = width / scale;
        previewHeight = height / scale;
        if (previewTarget)
            previewTarget->SetDpi(static_cast<float>(bar.dpi), static_cast<float>(bar.dpi));
        LONG left = std::clamp((bar.bounds.left + bar.bounds.right - width) / 2, info.rcMonitor.left,
                               std::max(info.rcMonitor.left, info.rcMonitor.right - width));
        SetWindowPos(previewWindow, HWND_TOPMOST, left, bar.bounds.top - height - 6, width, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateThumbnail();
        SetTimer(previewWindow, PreviewTimer, 100, nullptr);
    }
    void PreviewAction(int action) {
        if (!previewDock || previewGroup.windows.empty())
            return;
        if (action == 1 || action == 2) {
            size_t count = previewGroup.windows.size();
            previewIndex = (previewIndex + (action == 1 ? count - 1 : 1)) % count;
            UpdateThumbnail();
            return;
        }
        HWND window = previewGroup.windows[previewIndex].window;
        if (action == 3) {
            if (!WindowList::Activate(window))
                Error(L"Windows did not allow window activation.");
        }
        if (action == 4 && !WindowList::Close(window))
            Error(L"Windows did not allow closing this window.");
        HidePreview();
    }
    void PaintPreview() {
        PAINTSTRUCT paint{};
        BeginPaint(previewWindow, &paint);
        if (!previewDock) {
            EndPaint(previewWindow, &paint);
            return;
        }
        if (!previewTarget) {
            RECT rect{};
            GetClientRect(previewWindow, &rect);
            factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
                                             static_cast<float>(previewDock->dpi),
                                             static_cast<float>(previewDock->dpi)),
                D2D1::HwndRenderTargetProperties(previewWindow, D2D1::SizeU(rect.right, rect.bottom)), &previewTarget);
        }
        if (previewTarget) {
            previewTarget->BeginDraw();
            previewTarget->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
            if (previewIndex < previewGroup.windows.size()) {
                dockview::PaintPreview(previewTarget.Get(), writer.Get(), previewResources, previewWidth, previewHeight,
                                       previewGroup.windows[previewIndex].title, FAILED(previewResult), previewFocus);
                previewHits = dockview::PreviewHits(previewWidth, previewHeight);
            }
            if (FAILED(previewTarget->EndDraw()))
                previewTarget.Reset();
        }
        EndPaint(previewWindow, &paint);
    }
    LRESULT PreviewMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_PAINT:
            PaintPreview();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (previewTarget)
                previewTarget->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == OBJID_CLIENT) {
                if (!previewAccessible) {
                    previewProvider = new DockAccessible(
                        previewWindow, [this] { return previewHits; },
                        [this] { return previewDock ? previewDock->Scale() : 1.0f; }, L"Dako window preview");
                    previewProvider->Focus(previewFocus + 1);
                    previewAccessible.Attach(previewProvider);
                }
                return LresultFromObject(IID_IAccessible, wparam, previewAccessible.Get());
            }
            break;
        case DockInvoke:
            if (lparam) {
                previewFocus = static_cast<int>(wparam) - 1;
                InvalidateRect(previewWindow, nullptr, FALSE);
            } else
                PreviewAction(static_cast<int>(wparam));
            return 0;
        case WM_TIMER: {
            POINT point{};
            GetCursorPos(&point);
            RECT previewBounds{};
            GetWindowRect(previewWindow, &previewBounds);
            bool inside = PtInRect(&previewBounds, point) != FALSE;
            if (previewDock) {
                auto bar = previewDock->bounds;
                bar.top -= 8;
                inside |= PtInRect(&bar, point) != FALSE;
            }
            if (!inside && GetFocus() != previewWindow)
                HidePreview();
            return 0;
        }
        case WM_LBUTTONUP: {
            float scale = previewDock ? previewDock->Scale() : 1;
            for (const auto &hit : previewHits)
                if (hit.rect.Contains(GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale)) {
                    PreviewAction(hit.action);
                    break;
                }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                HidePreview();
                return 0;
            }
            if (wparam == VK_TAB || wparam == VK_RIGHT)
                previewFocus = (previewFocus + 1) % 4;
            if (wparam == VK_LEFT)
                previewFocus = (previewFocus + 3) % 4;
            if (wparam == VK_RETURN || wparam == VK_SPACE)
                PreviewAction(previewFocus + 1);
            if (wparam == VK_DELETE)
                PreviewAction(4);
            if (previewProvider)
                previewProvider->Focus(previewFocus + 1);
            NotifyWinEvent(EVENT_OBJECT_FOCUS, previewWindow, OBJID_CLIENT, previewFocus + 1);
            InvalidateRect(previewWindow, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            HidePreview();
            return 0;
        case WM_DESTROY:
            if (previewProvider)
                previewProvider->Disconnect();
            previewProvider = nullptr;
            previewAccessible.Reset();
            previewTarget.Reset();
            return 0;
        }
        return DefWindowProcW(previewWindow, message, wparam, lparam);
    }
    LRESULT Message(DockSurface &bar, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case DockCallback:
            if (wparam == ABN_POSCHANGED && !bar.positioning && !stopping && !updating) {
                Position(bar);
                return 0;
            }
            if (wparam == ABN_FULLSCREENAPP) {
                bar.fullscreenOwner = GetForegroundWindow();
                bar.fullscreen =
                    lparam != 0 && bar.monitor == MonitorFromWindow(bar.fullscreenOwner, MONITOR_DEFAULTTONULL);
                Fullscreen();
                return 0;
            }
            return 0;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(bar.window, &point);
            return CapsuleContains(bar.geometry.pill, point.x / bar.Scale(), point.y / bar.Scale()) ? HTCLIENT
                                                                                                    : HTTRANSPARENT;
        }
        case WM_PAINT:
            Paint(bar);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_WINDOWPOSCHANGED:
            if (bar.registered && !bar.positioning && !stopping && !updating) {
                APPBARDATA data{sizeof(data)};
                data.hWnd = bar.window;
                SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
            }
            break;
        case WM_ACTIVATE:
            if (bar.registered) {
                APPBARDATA data{sizeof(data)};
                data.hWnd = bar.window;
                SHAppBarMessage(ABM_ACTIVATE, &data);
            }
            break;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == OBJID_CLIENT) {
                if (!bar.accessible) {
                    bar.provider =
                        new DockAccessible(bar.window, [&bar] { return bar.hits; }, [&bar] { return bar.Scale(); });
                    bar.accessible.Attach(bar.provider);
                    Select(bar, bar.focus);
                }
                return LresultFromObject(IID_IAccessible, wparam, bar.accessible.Get());
            }
            break;
        case WM_DPICHANGED:
            bar.target.Reset();
            bar.resources.icons.clear();
            Position(bar);
            return 0;
        case WM_DISPLAYCHANGE:
            return 0;
        case WM_SETTINGCHANGE:
        case WM_TIMECHANGE:
        case WM_POWERBROADCAST:
            status = ReadClock(std::move(status));
            for (auto &item : surfaces)
                Rebuild(*item);
            Fullscreen();
            return 0;
        case WM_TIMER:
            if (wparam == MotionTimer) {
                auto now = GetTickCount64();
                float dt = std::min(.05f, (now - bar.motionTime) / 1000.0f);
                bar.motionTime = now;
                bar.magnify.Step(dt, 34);
                bar.pointer.Step(dt, 34);
                Rebuild(bar);
                if (!bar.magnify.Moving() && !bar.pointer.Moving()) {
                    KillTimer(bar.window, MotionTimer);
                    bar.motionTime = 0;
                }
            }
            if (wparam == ClockTimer) {
                KillTimer(bar.window, ClockTimer);
                status = ReadClock(std::move(status));
                for (auto &item : surfaces)
                    Rebuild(*item);
                ArmClock();
            }
            if (wparam == FlashTimer) {
                if (reduced || GetTickCount64() >= bar.pulseEnd)
                    KillTimer(bar.window, FlashTimer);
                InvalidateRect(bar.window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_HOVER, bar.window, 250};
            TrackMouseEvent(&track);
            float mouse = GET_X_LPARAM(lparam) / bar.Scale();
            bar.pointer.goal = mouse;
            bar.magnify.goal = bar.geometry.apps.Contains(mouse, GET_Y_LPARAM(lparam) / bar.Scale()) ? 1.0f : 0.0f;
            if (!reduced && (bar.magnify.Moving() || bar.pointer.Moving()))
                ArmMotion(bar);
            int hit = Hit(bar, mouse, GET_Y_LPARAM(lparam) / bar.Scale());
            if (hit != bar.hover) {
                if (hit < DockGroupFirst && previewDock == &bar)
                    HidePreview();
                bar.hover = hit;
                InvalidateRect(bar.window, nullptr, FALSE);
            }
            if (bar.pressed >= DockGroupFirst && bar.pressed - DockGroupFirst < static_cast<int>(bar.groups.size()) &&
                bar.groups[bar.pressed - DockGroupFirst].pinned &&
                (std::abs(GET_X_LPARAM(lparam) - bar.pressedPoint.x) > DipPixels(6, bar.dpi) ||
                 std::abs(GET_Y_LPARAM(lparam) - bar.pressedPoint.y) > DipPixels(6, bar.dpi)))
                bar.drag = bar.pressed - DockGroupFirst;
            return 0;
        }
        case WM_MOUSEHOVER:
            if (bar.hover >= DockGroupFirst)
                ShowPreview(bar, bar.hover - DockGroupFirst);
            return 0;
        case WM_MOUSEWHEEL:
            if (GET_WHEEL_DELTA_WPARAM(wparam) > 0)
                bar.first = bar.first ? bar.first - 1 : 0;
            else if (bar.first + bar.geometry.visibleGroups < bar.groups.size())
                ++bar.first;
            HidePreview();
            bar.magnify.Snap(0);
            Rebuild(bar);
            return 0;
        case WM_MOUSELEAVE:
            bar.magnify.goal = 0;
            if (bar.magnify.Moving())
                ArmMotion(bar);
            bar.hover = -1;
            InvalidateRect(bar.window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            bar.pressed = Hit(bar, GET_X_LPARAM(lparam) / bar.Scale(), GET_Y_LPARAM(lparam) / bar.Scale());
            bar.pressedPoint = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            bar.drag = -1;
            SetCapture(bar.window);
            return 0;
        case WM_LBUTTONUP: {
            int hit = Hit(bar, GET_X_LPARAM(lparam) / bar.Scale(), GET_Y_LPARAM(lparam) / bar.Scale());
            int pressed = bar.pressed, drag = bar.drag;
            bar.pressed = -1;
            bar.drag = -1;
            ReleaseCapture();
            if (drag >= 0 && hit >= DockGroupFirst && hit - DockGroupFirst < static_cast<int>(bar.groups.size()) &&
                bar.groups[hit - DockGroupFirst].pinned) {
                if (callbacks.reorder)
                    callbacks.reorder(bar.groups[drag].pinIndex, bar.groups[hit - DockGroupFirst].pinIndex);
            } else if (hit == pressed)
                Invoke(bar, hit);
            return 0;
        }
        case WM_CAPTURECHANGED:
            bar.pressed = bar.drag = -1;
            return 0;
        case WM_MBUTTONUP:
            Invoke(bar, Hit(bar, GET_X_LPARAM(lparam) / bar.Scale(), GET_Y_LPARAM(lparam) / bar.Scale()), true);
            return 0;
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1)
                AppMenu(bar, bar.focus);
            else {
                ScreenToClient(bar.window, &point);
                AppMenu(bar, Hit(bar, point.x / bar.Scale(), point.y / bar.Scale()));
            }
            return 0;
        }
        case DockInvoke:
            if (lparam)
                Select(bar, static_cast<int>(wparam));
            else
                Invoke(bar, static_cast<int>(wparam));
            return 0;
        case WM_SETFOCUS:
            if (bar.focus < 0 && !bar.hits.empty())
                bar.focus = bar.hits.front().action;
            Select(bar, bar.focus);
            InvalidateRect(bar.window, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            InvalidateRect(bar.window, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN: {
            auto found = std::find_if(bar.hits.begin(), bar.hits.end(),
                                      [&](const DockHit &hit) { return hit.action == bar.focus; });
            int index = found == bar.hits.end() ? 0 : static_cast<int>(found - bar.hits.begin());
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wparam == VK_LEFT || wparam == VK_RIGHT) &&
                bar.focus >= DockGroupFirst && bar.focus - DockGroupFirst < static_cast<int>(bar.groups.size()) &&
                bar.groups[bar.focus - DockGroupFirst].pinned) {
                int first = bar.focus - DockGroupFirst, next = first + (wparam == VK_RIGHT ? 1 : -1);
                if (next >= 0 && next < static_cast<int>(bar.groups.size()) && bar.groups[next].pinned &&
                    callbacks.reorder)
                    callbacks.reorder(bar.groups[first].pinIndex, bar.groups[next].pinIndex);
                return 0;
            }
            if ((wparam == VK_RIGHT || wparam == VK_LEFT || wparam == VK_TAB) && !bar.hits.empty()) {
                int step = wparam == VK_LEFT || (wparam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? -1 : 1;
                index = (index + step + static_cast<int>(bar.hits.size())) % static_cast<int>(bar.hits.size());
                Select(bar, bar.hits[index].action);
            }
            if (wparam == VK_RETURN || wparam == VK_SPACE)
                Invoke(bar, bar.focus, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (wparam == VK_UP && bar.focus >= DockGroupFirst) {
                ShowPreview(bar, bar.focus - DockGroupFirst);
                if (previewWindow)
                    SetFocus(previewWindow);
            }
            if (wparam == VK_APPS)
                AppMenu(bar, bar.focus);
            if (wparam == VK_ESCAPE)
                HidePreview();
            return 0;
        }
        case WM_DESTROY:
            if (bar.provider)
                bar.provider->Disconnect();
            bar.provider = nullptr;
            bar.accessible.Reset();
            KillTimer(bar.window, ClockTimer);
            KillTimer(bar.window, FlashTimer);
            KillTimer(bar.window, MotionTimer);
            return 0;
        }
        return DefWindowProcW(bar.window, message, wparam, lparam);
    }
    void StopDocks() {
        HidePreview();
        for (auto &bar : surfaces) {
            if (bar->registered) {
                APPBARDATA data{sizeof(data)};
                data.hWnd = bar->window;
                bar->registered = false;
                SHAppBarMessage(ABM_REMOVE, &data);
            }
            if (recovery && !preview)
                recovery->ForgetBar(bar->window);
            bar->target.Reset();
            bar->composition.Reset();
            bar->resources.icons.clear();
            if (bar->tooltip)
                DestroyWindow(bar->tooltip);
            if (bar->window)
                DestroyWindow(bar->window);
        }
        surfaces.clear();
    }
    bool Monitors() {
        if (updating || stopping)
            return true;
        updating = true;
        StopDocks();
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                reinterpret_cast<std::vector<HMONITOR> *>(context)->push_back(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        bool success = !monitors.empty() && monitors.size() <= 16;
        for (auto monitor : monitors) {
            if (!success)
                break;
            MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(monitor, &info);
            auto bar = std::make_unique<DockSurface>();
            bar->shell = this;
            bar->monitor = monitor;
            HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, DockClass,
                                          L"Dako floating dock", WS_POPUP, info.rcMonitor.left,
                                          info.rcMonitor.bottom - 40, info.rcMonitor.right - info.rcMonitor.left, 40,
                                          owner, nullptr, GetModuleHandleW(nullptr), bar.get());
            if (!window) {
                success = false;
                break;
            }
            if (!preview) {
                if (!recovery || !recovery->TrackBar(window)) {
                    DestroyWindow(window);
                    success = false;
                    break;
                }
                APPBARDATA data{sizeof(data)};
                data.hWnd = window;
                data.uCallbackMessage = DockCallback;
                if (!SHAppBarMessage(ABM_NEW, &data)) {
                    recovery->ForgetBar(window);
                    DestroyWindow(window);
                    success = false;
                    break;
                }
                bar->registered = true;
            }
            bar->tooltip =
                CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0,
                                0, 0, 0, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(bar->tooltip, TTM_SETMAXTIPWIDTH, 0, 300);
            surfaces.push_back(std::move(bar));
            if (!Position(*surfaces.back())) {
                success = false;
                break;
            }
            ShowWindow(window, SW_SHOWNOACTIVATE);
        }
        if (!success)
            StopDocks();
        updating = false;
        Fullscreen();
        return success;
    }
};
DockShell::DockShell() : impl_(std::make_unique<Impl>()) {
}
DockShell::~DockShell() {
    Stop();
}
bool DockShell::Start(HWND owner, Shell *recovery, bool preview, float textScale, Callbacks callbacks) {
    Stop();
    auto &self = *impl_;
    self.owner = owner;
    self.recovery = recovery;
    self.preview = preview;
    self.textScale = textScale;
    self.callbacks = std::move(callbacks);
    self.stopping = false;
    if (!preview && (!recovery || !recovery->Active() || !recovery->EnsureRecovery()))
        return false;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, self.factory.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(self.writer.GetAddressOf()))))
        return false;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW type{};
    type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpfnWndProc = Impl::Procedure;
    type.lpszClassName = DockClass;
    RegisterClassW(&type);
    type.lpfnWndProc = Impl::PreviewProcedure;
    type.lpszClassName = PreviewClass;
    RegisterClassW(&type);
    self.status = ReadClock({});
    return self.Monitors();
}
void DockShell::Stop() {
    auto &self = *impl_;
    self.stopping = true;
    self.StopDocks();
    if (self.previewWindow) {
        DestroyWindow(self.previewWindow);
        self.previewWindow = nullptr;
    }
    self.previewTarget.Reset();
    self.previewResources = {};
    self.previewAccessible.Reset();
    self.previewHits.clear();
    self.pins.clear();
    self.windows.clear();
    self.flashing.clear();
    self.callbacks = {};
    self.factory.Reset();
    self.writer.Reset();
    self.owner = nullptr;
}
void DockShell::RefreshMonitors() {
    if (!impl_->Monitors())
        impl_->Error(L"Dako could not reserve a bar on every monitor.");
}
void DockShell::ExplorerRestarted() {
    RefreshMonitors();
}
void DockShell::SetData(std::vector<PinEntry> pins, std::vector<WindowEntry> windows, DockStatus status) {
    auto &self = *impl_;
    self.pins = std::move(pins);
    self.windows = std::move(windows);
    self.status = ReadClock(std::move(status));
    if (self.previewDock && self.previewIndex < self.previewGroup.windows.size()) {
        HWND source = self.previewGroup.windows[self.previewIndex].window;
        bool exists = std::any_of(self.windows.begin(), self.windows.end(), [&](const WindowEntry &entry) {
            return entry.window == source && entry.monitor == self.previewDock->monitor;
        });
        if (!exists)
            self.HidePreview();
    }
    for (const auto &window : self.windows)
        if (window.active)
            self.flashing.erase(window.window);
    for (auto it = self.flashing.begin(); it != self.flashing.end();) {
        bool alive = std::any_of(self.windows.begin(), self.windows.end(),
                                 [&](const WindowEntry &entry) { return entry.window == *it; });
        if (!alive)
            it = self.flashing.erase(it);
        else
            ++it;
    }
    for (auto &bar : self.surfaces)
        self.Rebuild(*bar);
    self.Fullscreen();
}
void DockShell::Options(DockOptions options) {
    impl_->options = options;
    for (auto &dock : impl_->surfaces)
        impl_->Rebuild(*dock);
}
RECT DockShell::PillBounds(HMONITOR monitor) const {
    auto *dock = impl_->Find(monitor);
    if (!dock)
        return {};
    auto pill = dock->geometry.pill;
    float scale = dock->Scale();
    return {dock->bounds.left + static_cast<LONG>(std::lround(pill.left * scale)),
            dock->bounds.top + static_cast<LONG>(std::lround(pill.top * scale)),
            dock->bounds.left + static_cast<LONG>(std::lround(pill.right * scale)),
            dock->bounds.top + static_cast<LONG>(std::lround(pill.bottom * scale))};
}
POINT DockShell::Anchor(HMONITOR monitor, int action) const {
    auto *dock = impl_->Find(monitor);
    if (!dock)
        return {};
    for (const auto &hit : dock->hits)
        if (hit.action == action)
            return {dock->bounds.left + static_cast<LONG>(hit.rect.CenterX() * dock->Scale()),
                    dock->bounds.top + static_cast<LONG>(dock->geometry.pill.top * dock->Scale())};
    RECT pill = PillBounds(monitor);
    return {(pill.left + pill.right) / 2, pill.top};
}
bool DockShell::CompositionAvailable() const {
    return !impl_->surfaces.empty() && std::all_of(impl_->surfaces.begin(), impl_->surfaces.end(),
                                                   [](const auto &dock) { return dock->composition.Active(); });
}
void DockShell::Appearance(bool reduced, bool highContrast, float textScale) {
    auto &self = *impl_;
    bool resize = self.textScale != textScale;
    bool motionChanged = self.reduced != (reduced || highContrast);
    self.reduced = reduced || highContrast;
    self.contrast = highContrast;
    self.textScale = textScale;
    if (resize)
        self.HidePreview();
    for (auto &bar : self.surfaces) {
        if (reduced || highContrast) {
            bar->magnify.Snap(0);
            KillTimer(bar->window, MotionTimer);
        }
        if (resize)
            self.Position(*bar);
        if (self.reduced)
            KillTimer(bar->window, FlashTimer);
        if (motionChanged)
            self.Rebuild(*bar);
        InvalidateRect(bar->window, nullptr, FALSE);
    }
}
void DockShell::RefreshFullscreen() {
    impl_->Fullscreen();
}
void DockShell::Flash(HWND window) {
    auto &self = *impl_;
    self.flashing.insert(window);
    for (auto &bar : self.surfaces) {
        self.Rebuild(*bar);
        bool contains =
            std::any_of(bar->groups.begin(), bar->groups.end(), [&](const DockGroup &group) { return group.flash; });
        if (contains && !self.reduced) {
            bar->pulseEnd = GetTickCount64() + 1800;
            SetTimer(bar->window, FlashTimer, 32, nullptr);
        }
    }
}
void DockShell::Focus(HMONITOR monitor) {
    if (auto bar = impl_->Find(monitor)) {
        SetForegroundWindow(bar->window);
        SetFocus(bar->window);
    }
}
void DockShell::ActivateNumber(size_t index) {
    HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (auto bar = impl_->Find(monitor))
        impl_->ActivateGroup(*bar, index);
}
RECT DockShell::Bounds(HMONITOR monitor) const {
    if (auto bar = impl_->Find(monitor))
        return bar->bounds;
    return {};
}
HMONITOR DockShell::ActiveMonitor() const {
    return impl_->activeMonitor;
}
size_t DockShell::Count() const {
    return impl_->surfaces.size();
}
bool DockShell::PreviewAvailable() const {
    return impl_->thumbnail && SUCCEEDED(impl_->previewResult);
}

bool DockShell::Contains(HWND window) const {
    if (window == impl_->previewWindow)
        return true;
    for (const auto &bar : impl_->surfaces)
        if (bar->window == window)
            return true;
    return false;
}
bool DockShell::RenderFixture(const std::wstring &path, UINT dpi, float textScale, int variant) {
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writer;
    ComPtr<IWICImagingFactory> images;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(writer.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images))))
        return false;
    float scale = dpi / 96.0f * textScale;
    bool previewFixture = variant == 3 || variant == 4;
    UINT pixelWidth = previewFixture ? std::min(1366U, static_cast<UINT>(320 * scale)) : 1366;
    UINT pixelHeight = previewFixture
                           ? std::min(static_cast<UINT>(252 * scale), 768 - static_cast<UINT>(72 * scale) - 16)
                           : static_cast<UINT>(72 * scale);
    float width = pixelWidth / scale;
    UINT height = pixelHeight;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    if (FAILED(
            images->CreateBitmap(pixelWidth, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                         static_cast<float>(dpi), static_cast<float>(dpi)),
            &target)))
        return false;
    std::vector<DockGroup> groups;
    int count = variant == 1 ? 0 : variant == 2 ? 40 : 5;
    for (int i = 0; i < count; ++i) {
        DockGroup group;
        group.key = L"fixture:" + std::to_wstring(i);
        group.name = i == 0 ? L"Terminal" : i == 1 ? L"Browser" : i == 2 ? L"Mail" : i == 3 ? L"Files" : L"Editor";
        group.pinned = i < 3;
        group.active = i == 1;
        group.flash = i == 2;
        if (i != 0) {
            WindowEntry window;
            window.title = group.name;
            group.windows.push_back(window);
        }
        groups.push_back(std::move(group));
    }
    DockStatus status{L"18°C", L"Fixture Wi-Fi", L"42 percent", L"76%", L"2:42 PM", L"October 25, 2026"};
    auto geometry = SolveDock(width, groups, status);
    if (previewFixture)
        geometry.hits = dockview::PreviewHits(width, height / scale);
    for (size_t i = 0; i < geometry.hits.size(); ++i) {
        const auto &a = geometry.hits[i];
        if (a.rect.left < 0 || a.rect.right > width || a.name.empty())
            return false;
        for (size_t j = i + 1; j < geometry.hits.size(); ++j)
            if (layout::Intersects(a.rect, geometry.hits[j].rect))
                return false;
    }
    dockview::Resources resources;
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
    HRESULT painted = previewFixture
                          ? dockview::PaintPreview(target.Get(), writer.Get(), resources, width, height / scale,
                                                   L"Fixture document · window preview", variant == 4, 2, true)
                          : dockview::Paint(target.Get(), writer.Get(), resources, width, groups, status, -1, -1, 1, {},
                                            0, width / 2, variant == 5 ? 1.0f : 0.0f, variant == 7);
    if (FAILED(painted) || FAILED(target->EndDraw()))
        return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(images->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(images->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(pixelWidth, height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return false;
    return true;
}
}
