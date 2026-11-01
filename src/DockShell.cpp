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
constexpr UINT_PTR ClockTimer = 1, FlashTimer = 2, MotionTimer = 4, AutoHideTimer = 5, HudTimer = 6;
constexpr wchar_t DockClass[] = L"Dako.Native.Dock";
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
} // namespace
struct DockShell::Impl {
    struct DockSurface {
        Impl *shell = nullptr;
        HWND window = nullptr;
        HMONITOR monitor = nullptr;
        UINT dpi = 96;
        RECT bounds{};
        bool registered = false, positioning = false, fullscreen = false, hidden = false;
        HWND fullscreenOwner = nullptr;
        std::vector<DockGroup> groups;
        std::vector<DockHit> hits;
        std::map<std::wstring, LONG> children;
        LONG nextChild = 1000;
        int hover = -1, focus = -1, pressed = -1, drag = -1;
        POINT pressedPoint{};
        ULONGLONG pulseEnd = 0;
        CompositionSurface composition;
        ComPtr<ID2D1RenderTarget> target;
        dock::Spring magnify{0}, pointer{-1000};
        ULONGLONG motionTime = 0;
        int hideOffset = 0;
        bool volumeHud = false;
        size_t first = 0;
        DockLayout geometry;
        dockview::Resources resources;
        graphics::AmbientShadow shadow;
        ComPtr<IAccessible> accessible;
        DockAccessible *provider = nullptr;
        float Scale() const { return dpi / 96.0f * shell->textScale; }
        float Width() const { return (bounds.right - bounds.left) / Scale(); }
    };
    HWND owner = nullptr;
    Shell *recovery = nullptr;
    bool reserve = false, preview = false, reduced = false, contrast = false, stopping = false, updating = false;
    float textScale = 1;
    bool panelOpen = false;
    DockOptions options;
    Callbacks callbacks;
    std::vector<std::unique_ptr<DockSurface>> surfaces;
    std::vector<PinEntry> pins;
    std::vector<WindowEntry> windows;
    DockStatus status;
    std::set<HWND> flashing;
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> writer;
    HMONITOR activeMonitor = nullptr, joinedMonitor = nullptr;
    int returnAction = -1;
    dockview::Resources joinedResources;

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
    void Rebuild(DockSurface &bar) {
        if (!bar.window)
            return;
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
        for (auto it = bar.resources.actionPress.begin(); it != bar.resources.actionPress.end();)
            if (std::none_of(bar.hits.begin(), bar.hits.end(),
                             [&](const auto &hit) { return hit.action == it->first; }))
                it = bar.resources.actionPress.erase(it);
            else
                ++it;
        auto pill = bar.geometry.pill;
        float pixels = bar.Scale();
        auto silhouette = shape::RoundedPath(factory.Get(), {pill.left, pill.top, pill.right, pill.bottom},
                                             shape::Pill(pill.Height()), shape::Flat());
        HRGN region = shape::Region(silhouette.Get(), D2D1::Matrix3x2F::Scale(pixels, pixels));
        for (const auto &hit : bar.hits)
            if (hit.action >= DockGroupFirst) {
                auto r = hit.rect;
                r.left -= 4;
                r.right += 4;
                if (options.edge == anchor::Edge::Bottom)
                    r.top -= 8;
                else
                    r.bottom += 8;
                auto path = shape::RoundedPath(factory.Get(), {r.left, r.top, r.right, r.bottom}, shape::Control);
                HRGN icon = shape::Region(path.Get(), D2D1::Matrix3x2F::Scale(pixels, pixels));
                CombineRgn(region, region, icon, RGN_OR);
                DeleteObject(icon);
            }
        if (bar.hideOffset)
            OffsetRgn(region, 0, static_cast<int>(std::lround(bar.hideOffset * bar.Scale())));
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
        if (options.edge == anchor::Edge::Top) {
            rect.top = info.rcMonitor.top;
            rect.bottom = rect.top + height;
        }
        if (!bar.registered) {
            rect.bottom = info.rcWork.bottom;
            rect.top = rect.bottom - height;
        } else {
            APPBARDATA data{sizeof(data)};
            data.hWnd = bar.window;
            data.uEdge = options.edge == anchor::Edge::Top ? ABE_TOP : ABE_BOTTOM;
            data.rc = rect;
            SHAppBarMessage(ABM_QUERYPOS, &data);
            if (options.edge == anchor::Edge::Top)
                data.rc.bottom = data.rc.top + height;
            else
                data.rc.top = data.rc.bottom - height;
            SHAppBarMessage(ABM_SETPOS, &data);
            rect = data.rc;
        }
        if (options.edge == anchor::Edge::Top && !bar.registered) {
            rect.top = info.rcWork.top;
            rect.bottom = rect.top + height;
        }
        bool changed = !EqualRect(&rect, &bar.bounds);
        bar.bounds = rect;
        SetWindowPos(bar.window, bar.hidden ? HWND_BOTTOM : HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOACTIVATE);
        if (changed || dpiChanged || !bar.target) {
            bar.target.Reset();
            bar.resources.ResetBitmaps();
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
                    HideForNotificationState(notification, options.fullscreenPolicy);
        HWND foregroundWindow = GetForegroundWindow();
        HMONITOR foreground = MonitorFromWindow(foregroundWindow, MONITOR_DEFAULTTONULL);
        bool excluded = false;
        DWORD pid = 0;
        GetWindowThreadProcessId(foregroundWindow, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t path[32768]{};
            DWORD length = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &length)) {
                std::wstring name = std::filesystem::path(path).filename().wstring();
                for (const auto &item : options.hideApps)
                    excluded |= _wcsicmp(item.c_str(), name.c_str()) == 0;
            }
            CloseHandle(process);
        }
        for (auto &bar : surfaces) {
            if (bar->fullscreen && bar->fullscreenOwner != foregroundWindow && !busy)
                bar->fullscreen = false;
            bool hide = !preview && (excluded || (options.fullscreenPolicy == 2 && bar->fullscreen) ||
                                     (busy && bar->monitor == foreground));
            if (!hide && options.fullscreenPolicy == 0 && foregroundWindow != owner &&
                (GetWindowLongPtrW(foregroundWindow, GWL_EXSTYLE) & WS_EX_TOPMOST))
                SetWindowPos(bar->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (hide == bar->hidden)
                continue;
            bar->hidden = hide;
            if (hide)
                bar->shadow.Hide();
            if (!hide) {
                status = ReadClock(std::move(status));
                Rebuild(*bar);
            }
            ShowWindow(bar->window, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
            SetWindowPos(bar->window, hide ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        if (!bar.motionTime) {
            bar.motionTime = GetTickCount64();
            SetTimer(bar.window, MotionTimer, 16, nullptr);
        }
    }
    void ShowVolumeHud(DockSurface &bar) {
        bar.volumeHud = true;
        SetTimer(bar.window, HudTimer, 900, nullptr);
        InvalidateRect(bar.window, nullptr, FALSE);
    }
    void PaintVolumeHud(ID2D1RenderTarget *target, DockSurface &bar) {
        if (!bar.volumeHud)
            return;
        auto found =
            std::find_if(bar.hits.begin(), bar.hits.end(), [](const DockHit &hit) { return hit.action == DockVolume; });
        if (found == bar.hits.end())
            return;
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(theme::CardHover), &brush)))
            return;
        ComPtr<ID2D1Factory> targetFactory;
        target->GetFactory(&targetFactory);
        auto rect = dockview::Rect(found->rect);
        auto &geometry = target == bar.target.Get() ? bar.resources.geometryCache : joinedResources.geometryCache;
        graphics::Rounded(target, targetFactory.Get(), geometry, rect, shape::Control, brush.Get());
        auto value = status.volume;
        auto space = value.find(L' ');
        if (space != std::wstring::npos)
            value = value.substr(0, space) + L"%";
        brush->SetColor(D2D1::ColorF(theme::Text));
        bar.resources.Initialize(writer.Get());
        bool muted = value == L"Muted";
        if (muted)
            value = L"\xE74F";
        auto format = bar.resources.typography->formats[muted ? type::InlineIcon : type::Caption];
        auto alignment = format->GetTextAlignment();
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        target->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format.Get(), rect, brush.Get());
        format->SetTextAlignment(alignment);
    }
    void Paint(DockSurface &bar) {
        PAINTSTRUCT paint{};
        BeginPaint(bar.window, &paint);
        if (bar.target) {
            bar.target->BeginDraw();
            bar.target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
            float pulse = 1;
            if (reduced && bar.pulseEnd > GetTickCount64()) {
                float elapsed = 1 - (bar.pulseEnd - GetTickCount64()) / 120.0f;
                pulse = .7f + .3f * std::abs(2 * elapsed - 1);
            }
            bar.target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale) *
                                     D2D1::Matrix3x2F::Translation(0, bar.hideOffset * textScale));
            if (joinedMonitor == bar.monitor) {
                bar.resources.Synchronize(bar.geometry, bar.groups, bar.hover, bar.drag, !reduced, bar.pressed);
                bar.target->Clear(D2D1::ColorF(0, 0.0f));
                InvalidateRect(owner, nullptr, FALSE);
            } else {
                dockview::Paint(bar.target.Get(), writer.Get(), bar.resources, bar.Width(), bar.groups, status,
                                bar.hover, GetFocus() == bar.window ? bar.focus : -1, pulse, options, bar.first,
                                bar.pointer.value, reduced ? 0 : bar.magnify.value, contrast, !reduced, bar.drag, true,
                                true, bar.pressed);
            }
            if (joinedMonitor != bar.monitor)
                PaintVolumeHud(bar.target.Get(), bar);
            if (SUCCEEDED(bar.target->EndDraw()))
                bar.composition.Present();
            if (!reduced && bar.resources.Moving())
                ArmMotion(bar);
        }
        RECT bounds{};
        GetWindowRect(bar.window, &bounds);
        auto pill = bar.geometry.pill;
        float scale = bar.Scale();
        RECT surface{bounds.left + static_cast<LONG>(std::lround(pill.left * scale)),
                     bounds.top + static_cast<LONG>(std::lround(pill.top * scale)),
                     bounds.left + static_cast<LONG>(std::lround(pill.right * scale)),
                     bounds.top + static_cast<LONG>(std::lround(pill.bottom * scale))};
        bool fits = (pill.Width() + 96) * scale <= bounds.right - bounds.left;
        bar.shadow.Position(bar.window, surface, bar.dpi * textScale, shape::Surface, options.edge, 1,
                            fits && !contrast && !bar.hidden && !bar.hideOffset && joinedMonitor != bar.monitor);
        EndPaint(bar.window, &paint);
    }
    int Hit(const DockSurface &bar, float x, float y) const {
        if (!CapsuleContains(bar.geometry.pill, x, y) &&
            std::none_of(bar.hits.begin(), bar.hits.end(),
                         [&](const DockHit &hit) { return hit.action >= DockGroupFirst && hit.rect.Contains(x, y); }))
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
    void LaunchFeedback(DockSurface &bar, const std::wstring &key) {
        if (contrast)
            return;
        if (reduced) {
            bar.resources.pulseKey = key;
            bar.pulseEnd = GetTickCount64() + 120;
            SetTimer(bar.window, FlashTimer, 16, nullptr);
        } else {
            bar.resources.Launch(key);
            ArmMotion(bar);
        }
    }
    void ActivateGroup(DockSurface &bar, size_t index, bool launch = false) {
        if (index >= bar.groups.size())
            return;
        auto group = bar.groups[index];
        activeMonitor = bar.monitor;
        HidePreview();
        if (launch || group.windows.empty()) {
            LaunchFeedback(bar, group.key);
            if (!group.launchKey.empty() && callbacks.launch)
                callbacks.launch(group.launchKey);
            return;
        }
        if (group.windows.size() > 1) {
            Popover(bar, DockGroupFirst + static_cast<int>(index), surface::Trigger::Click);
            return;
        }
        auto active = std::find_if(group.windows.begin(), group.windows.end(),
                                   [](const WindowEntry &entry) { return entry.window == GetForegroundWindow(); });
        {
            size_t selected = active == group.windows.end()
                                  ? 0
                                  : (static_cast<size_t>(active - group.windows.begin()) + 1) % group.windows.size();
            if (!WindowList::Activate(group.windows[selected].window))
                Error(L"Windows did not allow activation. Elevated windows may require an elevated Dako.");
            else
                PostMessageW(owner, WM_SYSCOMMAND, SC_CLOSE, 0);
        }
    }
    void Popover(DockSurface &bar, int action, surface::Trigger trigger, bool forcePreview = false) {
        if (!callbacks.popover)
            return;
        if (trigger != surface::Trigger::Hover)
            returnAction = action;
        auto control = action == DockApps         ? surface::Control::Apps
                       : action == DockSearch     ? surface::Control::Search
                       : action == DockWifi       ? surface::Control::Wifi
                       : action == DockVolume     ? surface::Control::Volume
                       : action == DockBattery    ? surface::Control::Battery
                       : action == DockClock      ? surface::Control::Clock
                       : action == DockWeather    ? surface::Control::Weather
                       : action == DockDesktop    ? surface::Control::Desktop
                       : action == DockTaskView   ? surface::Control::TaskView
                       : action >= DockGroupFirst ? surface::Control::App
                                                  : surface::Control::More;
        DockGroup group;
        if (action >= DockGroupFirst && action - DockGroupFirst < static_cast<int>(bar.groups.size()))
            group = bar.groups[action - DockGroupFirst];
        auto request = surface::Describe(control, trigger, group.windows.size());
        if (forcePreview) {
            request.kind = surface::Kind::Preview;
            request.width = surface::Width::Compact;
            request.behavior = surface::Behavior::Panel;
        }
        request.control = action;
        request.group = group.key;
        if (action == DockOverflow) {
            request.kind = surface::Kind::Overflow;
            request.width = surface::Width::Compact;
        }
        callbacks.popover(std::move(request), std::move(group), bar.monitor);
    }
    void Overflow(DockSurface &bar) {
        if (bar.geometry.visibleGroups) {
            bar.first = bar.first + bar.geometry.visibleGroups >= bar.groups.size()
                            ? 0
                            : bar.first + bar.geometry.visibleGroups;
            bar.magnify.Snap(0);
            Rebuild(bar);
        } else
            Popover(bar, DockOverflow, surface::Trigger::Click);
    }
    void Invoke(DockSurface &bar, int action, bool launch = false) {
        returnAction = action;
        if (action >= 0 && !contrast && !reduced) {
            auto &press = bar.resources.actionPress[action];
            press.Snap(1);
            press.goal = 0;
            ArmMotion(bar);
        }
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
            PostMessageW(owner, WM_SYSCOMMAND, SC_CLOSE, 0);
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
        } else
            Popover(bar, action, surface::Trigger::Context);
    }
    void HidePreview() {}
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
        case WM_SYSCOMMAND:
            if ((wparam & 0xFFF0) == SC_CLOSE) {
                PostMessageW(owner, WM_SYSCOMMAND, SC_CLOSE, 0);
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(bar.window, &point);
            auto pill = bar.geometry.pill;
            auto *path = bar.resources.geometryCache.Rounded(factory.Get(), pill.Width(), pill.Height(),
                                                             shape::Pill(pill.Height()), shape::Flat());
            bool inside = shape::Contains(
                path, {point.x / bar.Scale() - pill.left, point.y / bar.Scale() - bar.hideOffset - pill.top});
            return inside ? HTCLIENT : HTTRANSPARENT;
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
            bar.resources.ResetBitmaps();
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
            if (wparam == HudTimer) {
                KillTimer(bar.window, HudTimer);
                bar.volumeHud = false;
                InvalidateRect(bar.window, nullptr, FALSE);
                return 0;
            }
            if (wparam == AutoHideTimer) {
                KillTimer(bar.window, AutoHideTimer);
                if (panelOpen)
                    return 0;
                bar.hideOffset = options.edge == anchor::Edge::Bottom ? 62 : -62;
                Rebuild(bar);
                return 0;
            }
            if (wparam == MotionTimer) {
                auto now = GetTickCount64();
                float dt = std::min(.05f, (now - bar.motionTime) / 1000.0f);
                bar.motionTime = now;
                bar.magnify.Step(dt, dock::motion::Snappy, options.motionSpeed);
                bar.pointer.Step(dt, dock::motion::Snappy, options.motionSpeed);
                bar.resources.Step(dt, options.motionSpeed);
                Rebuild(bar);
                if (!bar.magnify.Moving() && !bar.pointer.Moving() && !bar.resources.Moving()) {
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
                if (GetTickCount64() >= bar.pulseEnd)
                    KillTimer(bar.window, FlashTimer);
                InvalidateRect(bar.window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            KillTimer(bar.window, AutoHideTimer);
            if (bar.hideOffset) {
                bar.hideOffset = 0;
                Rebuild(bar);
            }
            if (callbacks.pointer)
                callbacks.pointer(true, bar.monitor);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, bar.window, 0};
            TrackMouseEvent(&track);
            float mouse = GET_X_LPARAM(lparam) / bar.Scale();
            bar.pointer.goal = mouse;
            bar.magnify.goal =
                options.magnify && bar.geometry.apps.Contains(mouse, GET_Y_LPARAM(lparam) / bar.Scale()) ? 1.0f : 0.0f;
            if (!reduced && (bar.magnify.Moving() || bar.pointer.Moving()))
                ArmMotion(bar);
            int hit = Hit(bar, mouse, GET_Y_LPARAM(lparam) / bar.Scale());
            if (hit != bar.hover) {
                bar.hover = hit;
                if (hit >= 0 && (GetMessageExtraInfo() & 0xFFFFFF00) != 0xFF515700)
                    Popover(bar, hit, surface::Trigger::Hover);
                InvalidateRect(bar.window, nullptr, FALSE);
            }
            if (bar.pressed >= DockGroupFirst && bar.pressed - DockGroupFirst < static_cast<int>(bar.groups.size()) &&
                bar.groups[bar.pressed - DockGroupFirst].pinned &&
                (std::abs(GET_X_LPARAM(lparam) - bar.pressedPoint.x) > DipPixels(6, bar.dpi) ||
                 std::abs(GET_Y_LPARAM(lparam) - bar.pressedPoint.y) > DipPixels(6, bar.dpi)))
                bar.drag = bar.pressed - DockGroupFirst;
            if (bar.drag >= 0) {
                bar.magnify.goal = 0;
                InvalidateRect(bar.window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(bar.window, &point);
            if (Hit(bar, point.x / bar.Scale(), point.y / bar.Scale()) == DockVolume && callbacks.volume) {
                callbacks.volume(GET_WHEEL_DELTA_WPARAM(wparam), bar.monitor);
                ShowVolumeHud(bar);
                return 0;
            }
            auto intent =
                ResolveDockWheel(bar.geometry, options, point.x / bar.Scale(), point.y / bar.Scale() - bar.hideOffset,
                                 GET_WHEEL_DELTA_WPARAM(wparam), message == WM_MOUSEHWHEEL);
            if (callbacks.expand && intent == DockWheel::Expand) {
                callbacks.expand(bar.monitor);
                return 0;
            }
            if (intent == DockWheel::None)
                return 0;
            if (intent == DockWheel::PreviousApps)
                bar.first = bar.first ? bar.first - 1 : 0;
            else if (bar.first + bar.geometry.visibleGroups < bar.groups.size())
                ++bar.first;
            HidePreview();
            bar.magnify.Snap(0);
            Rebuild(bar);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (options.autoHide)
                SetTimer(bar.window, AutoHideTimer, 550, nullptr);
            if (callbacks.pointer)
                callbacks.pointer(false, bar.monitor);
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
            if (Hit(bar, GET_X_LPARAM(lparam) / bar.Scale(), GET_Y_LPARAM(lparam) / bar.Scale()) == DockVolume &&
                callbacks.volume) {
                callbacks.volume(0, bar.monitor);
                ShowVolumeHud(bar);
                return 0;
            }
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
            if (wparam == VK_TAB && panelOpen && !GetPropW(owner, L"Dako.SurfaceLabel") &&
                !(GetKeyState(VK_SHIFT) & 0x8000)) {
                SetForegroundWindow(owner);
                SetFocus(owner);
                PostMessageW(owner, WM_KEYDOWN, VK_TAB, 0);
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
                Popover(bar, bar.focus, surface::Trigger::Click, true);
            }
            if (wparam == VK_APPS)
                AppMenu(bar, bar.focus);
            if (wparam == VK_ESCAPE)
                PostMessageW(owner, WM_KEYDOWN, VK_ESCAPE, 0);
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
            bar->shadow.Hide();
            bar->composition.Reset();
            bar->resources.ResetBitmaps();
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
        if (options.monitorMode != L"Each" && !monitors.empty()) {
            POINT point{};
            GetCursorPos(&point);
            HMONITOR selected = options.monitorMode == L"Cursor" ? MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY)
                                                                 : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
            monitors = {selected};
        }
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
            if (reserve && !options.autoHide) {
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
            surfaces.push_back(std::move(bar));
            if (!Position(*surfaces.back())) {
                success = false;
                break;
            }
            ShowWindow(window, SW_SHOWNOACTIVATE);
            if (options.autoHide)
                SetTimer(window, AutoHideTimer, 550, nullptr);
        }
        if (!success)
            StopDocks();
        updating = false;
        Fullscreen();
        return success;
    }
};
DockShell::DockShell() : impl_(std::make_unique<Impl>()) {}
DockShell::~DockShell() { Stop(); }
bool DockShell::Start(HWND owner, Shell *recovery, bool preview, float textScale, Callbacks callbacks, bool reserve) {
    Stop();
    auto &self = *impl_;
    self.reserve = reserve && !preview;
    self.owner = owner;
    self.recovery = recovery;
    self.preview = preview;
    self.textScale = textScale;
    self.callbacks = std::move(callbacks);
    self.stopping = false;
    if (self.reserve && (!recovery || !recovery->Active() || !recovery->EnsureRecovery()))
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
    self.status = ReadClock({});
    return self.Monitors();
}
void DockShell::Stop() {
    auto &self = *impl_;
    self.stopping = true;
    self.StopDocks();
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
        impl_->Error(L"Dako could not create a dock on every monitor.");
}
void DockShell::ExplorerRestarted() { RefreshMonitors(); }
void DockShell::SetData(std::vector<PinEntry> pins, std::vector<WindowEntry> windows, DockStatus status) {
    auto &self = *impl_;
    self.pins = std::move(pins);
    self.windows = std::move(windows);
    self.status = ReadClock(std::move(status));
    for (auto &bar : self.surfaces)
        self.Rebuild(*bar);
    self.Fullscreen();
}
void DockShell::Options(DockOptions options) {
    impl_->options = options;
    for (auto &dock : impl_->surfaces)
        impl_->Position(*dock);
}
void DockShell::PanelVisible(bool visible) {
    auto &self = *impl_;
    if (self.panelOpen == visible)
        return;
    self.panelOpen = visible;
    for (auto &bar : self.surfaces) {
        if (visible) {
            KillTimer(bar->window, AutoHideTimer);
            if (bar->hideOffset) {
                bar->hideOffset = 0;
                self.Rebuild(*bar);
            }
        } else if (self.options.autoHide)
            SetTimer(bar->window, AutoHideTimer, 550, nullptr);
    }
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
void DockShell::Joined(HMONITOR monitor, bool visible) {
    auto &self = *impl_;
    HMONITOR next = visible ? monitor : nullptr;
    if (self.joinedMonitor == next)
        return;
    self.joinedMonitor = next;
    for (auto &bar : self.surfaces)
        InvalidateRect(bar->window, nullptr, FALSE);
}
void DockShell::ReleaseJoined() { impl_->joinedResources = {}; }
void DockShell::PaintJoined(ID2D1RenderTarget *target, HMONITOR monitor, POINT origin, float dpi) {
    auto &self = *impl_;
    auto *bar = self.Find(monitor);
    if (!bar || !target)
        return;
    D2D1::Matrix3x2F saved;
    target->GetTransform(&saved);
    float scale = dpi / 96;
    target->SetTransform(
        D2D1::Matrix3x2F::Scale(self.textScale, self.textScale) *
        D2D1::Matrix3x2F::Translation((bar->bounds.left - origin.x) / scale, (bar->bounds.top - origin.y) / scale));
    self.joinedResources.visuals = bar->resources.visuals;
    self.joinedResources.actionHover = bar->resources.actionHover;
    self.joinedResources.actionPress = bar->resources.actionPress;
    self.joinedResources.focusOpacity = bar->resources.focusOpacity;
    self.joinedResources.previousFocus = bar->resources.previousFocus;
    dockview::Paint(target, self.writer.Get(), self.joinedResources, bar->Width(), bar->groups, self.status, bar->hover,
                    GetFocus() == bar->window ? bar->focus : -1, 1, self.options, bar->first, bar->pointer.value,
                    self.reduced ? 0 : bar->magnify.value, self.contrast, !self.reduced, bar->drag, false, false,
                    bar->pressed);
    self.PaintVolumeHud(target, *bar);
    target->SetTransform(saved);
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
void DockShell::RefreshFullscreen() { impl_->Fullscreen(); }
void DockShell::Flash(HWND window) {
    auto &self = *impl_;
    self.flashing.insert(window);
    for (auto &bar : self.surfaces) {
        self.Rebuild(*bar);
        bool contains =
            std::any_of(bar->groups.begin(), bar->groups.end(), [&](const DockGroup &group) { return group.flash; });
        if (contains)
            for (const auto &group : bar->groups)
                if (group.flash)
                    self.LaunchFeedback(*bar, group.key);
    }
}
void DockShell::Focus(HMONITOR monitor) {
    if (auto bar = impl_->Find(monitor)) {
        if (impl_->returnAction >= 0)
            impl_->Select(*bar, impl_->returnAction);
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
HMONITOR DockShell::ActiveMonitor() const { return impl_->activeMonitor; }
size_t DockShell::Count() const { return impl_->surfaces.size(); }
bool DockShell::PreviewAvailable() const { return false; }

bool DockShell::Contains(HWND window) const {
    for (const auto &bar : impl_->surfaces)
        if (bar->window == window)
            return true;
    return false;
}
bool DockShell::DwellContains(POINT screen) const {
    HWND under = GetAncestor(WindowFromPoint(screen), GA_ROOT);
    for (const auto &bar : impl_->surfaces) {
        if (bar->window != under)
            continue;
        POINT local = screen;
        ScreenToClient(bar->window, &local);
        return DockDwellContains(bar->geometry, local.x / bar->Scale(), local.y / bar->Scale() - bar->hideOffset);
    }
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
    DockOptions fixtureOptions;
    if (variant >= 20) {
        fixtureOptions.edge = variant == 20 ? anchor::Edge::Top : anchor::Edge::Bottom;
    }
    dockview::Resources resources;
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale));
    HRESULT painted = previewFixture
                          ? dockview::PaintPreview(target.Get(), writer.Get(), resources, width, height / scale,
                                                   L"Fixture document · window preview", variant == 4, 2, true)
                          : dockview::Paint(target.Get(), writer.Get(), resources, width, groups, status, -1, -1, 1,
                                            fixtureOptions, 0, width / 2, variant == 5 ? 1.0f : 0.0f, variant == 7);
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
} // namespace desktop
