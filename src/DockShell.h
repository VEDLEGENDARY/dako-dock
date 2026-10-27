#pragma once
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "WindowList.h"
#include "LayoutEngine.h"
#include "DockMotion.h"

namespace desktop {
class Shell;
inline constexpr UINT DockCallback = WM_APP + 50, DockChanged = WM_APP + 51, DockInvoke = WM_APP + 52;
enum DockAction {
    DockApps = 1,
    DockSearch,
    DockWifi,
    DockVolume,
    DockBattery,
    DockClock,
    DockWeather,
    DockDesktop,
    DockTaskView,
    DockOverflow,
    DockStatusMore,
    DockRecent,
    DockGroupFirst = 100
};
inline LONG DipPixels(float dip, UINT dpi) {
    return static_cast<LONG>(std::lround(dip * dpi / 96.0f));
}
inline RECT BottomDockRect(RECT monitor, UINT dpi, float dip = 72) {
    LONG height = std::clamp(DipPixels(dip, dpi), 0L, std::max(0L, monitor.bottom - monitor.top));
    return {monitor.left, monitor.bottom - height, monitor.right, monitor.bottom};
}
inline bool HideForNotificationState(QUERY_USER_NOTIFICATION_STATE state) {
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}
struct PinEntry {
    std::wstring key, name, appId, executable;
    std::shared_ptr<std::vector<BYTE>> pixels;
    UINT width = 0, height = 0;
};
struct DockGroup {
    std::wstring key, name, launchKey;
    bool pinned = false, active = false, flash = false;
    std::wstring appId;
    size_t pinIndex = 0;
    std::vector<std::wstring> pinKeys;
    std::vector<WindowEntry> windows;
    std::shared_ptr<std::vector<BYTE>> pixels;
    UINT width = 0, height = 0;
};
inline bool SameApplication(const PinEntry &pin, const WindowEntry &window) {
    if (!pin.appId.empty())
        return AppIdentity(pin.appId) == AppIdentity(window.appId);
    for (const auto &key : {pin.key, pin.executable}) {
        if (key.empty())
            continue;
        auto value = AppIdentity(key);
        if (value == window.identity || value == AppIdentity(window.executable) || value == AppIdentity(window.appId))
            return true;
    }
    return false;
}
inline std::vector<DockGroup> BuildDockGroups(const std::vector<PinEntry> &pins,
                                              const std::vector<WindowEntry> &windows, HMONITOR monitor) {
    std::vector<DockGroup> groups;
    for (size_t i = 0; i < pins.size(); ++i) {
        const auto &pin = pins[i];
        auto identity = AppIdentity(!pin.appId.empty()        ? pin.appId
                                    : !pin.executable.empty() ? pin.executable
                                                              : pin.key);
        auto existing = std::find_if(groups.begin(), groups.end(), [&](const auto &group) {
            const auto &first = pins[group.pinIndex];
            return identity == AppIdentity(!first.appId.empty()        ? first.appId
                                           : !first.executable.empty() ? first.executable
                                                                       : first.key);
        });
        if (existing != groups.end()) {
            existing->pinKeys.push_back(pin.key);
            continue;
        }
        DockGroup group;
        group.key = group.launchKey = pin.key;
        group.name = pin.name;
        group.appId = pin.appId;
        group.pinned = true;
        group.pinIndex = i;
        group.pinKeys.push_back(pin.key);
        group.pixels = pin.pixels;
        group.width = pin.width;
        group.height = pin.height;
        groups.push_back(std::move(group));
    }
    size_t pinned = groups.size();
    for (const auto &window : windows) {
        if (monitor && window.monitor != monitor)
            continue;
        auto found = std::find_if(groups.begin(), groups.begin() + static_cast<ptrdiff_t>(pinned),
                                  [&](const auto &group) { return SameApplication(pins[group.pinIndex], window); });
        if (found == groups.begin() + static_cast<ptrdiff_t>(pinned)) {
            found = std::find_if(groups.begin() + static_cast<ptrdiff_t>(pinned), groups.end(),
                                 [&](const auto &group) { return group.key == window.identity; });
            if (found == groups.end()) {
                DockGroup group;
                group.key = window.identity;
                group.name = window.title;
                group.appId = window.appId;
                group.launchKey = window.appId.empty() ? window.executable : L"shell:AppsFolder\\" + window.appId;
                groups.push_back(std::move(group));
                found = groups.end() - 1;
            }
        }
        found->windows.push_back(window);
        found->active |= window.active;
        if (found->appId.empty())
            found->appId = window.appId;
    }
    std::stable_sort(groups.begin() + static_cast<ptrdiff_t>(pinned), groups.end(),
                     [](const auto &a, const auto &b) { return a.key < b.key; });
    return groups;
}
struct DockOptions {
    unsigned statusMask = 31;
    bool showDesktop = false, showTaskView = false;
};
inline std::wstring NormalizeLayout(std::wstring value) {
    return value == L"notch" ? L"notch" : L"dock";
}
inline bool CapsuleContains(layout::Rect pill, float x, float y) {
    return pill.Width() > 0 && layout::RoundedContains(pill, pill.Height() / 2, x, y);
}
struct DockStatus {
    std::wstring weather = L"Weather off", wifi = L"Wi-Fi", volume = L"Sound", battery = L"AC", clock, date;
};
struct DockHit {
    int action = 0;
    LONG child = 0;
    layout::Rect rect;
    std::wstring name, label;
    bool active = false;
};
struct DockLayout {
    layout::Rect pill, apps;
    std::vector<DockHit> hits;
    std::vector<float> dividers;
    size_t visibleGroups = 0, firstGroup = 0;
};
inline DockLayout SolveDock(float width, const std::vector<DockGroup> &groups, const DockStatus &status,
                            DockOptions options = {}, size_t first = 0, float pointer = -1000, float strength = 0) {
    DockLayout out;
    float limit = std::max(100.0f, std::min(width * .90f, width - 16));
    bool compact = limit < 380;
    if (limit < 308) {
        options.showDesktop = false;
        options.showTaskView = false;
    }
    std::vector<std::pair<int, float>> statusItems;
    float launcher = compact ? 64.0f : 80.0f;
    if (options.showDesktop)
        launcher += 40;
    if (options.showTaskView)
        launcher += 40;
    float statuses = 32;
    const int ids[]{DockClock, DockVolume, DockWifi, DockBattery, DockWeather};
    const unsigned masks[]{16, 4, 2, 8, 1};
    for (int i = 0; i < 5; ++i) {
        float size = i == 0 ? (compact ? 56.0f : 72.0f) : 32.0f;
        if (limit >= 308 && (options.statusMask & masks[i]) && launcher + statuses + size + 150 <= limit) {
            statusItems.emplace_back(ids[i], size);
            statuses += size;
        }
    }
    float fixed = 24 + launcher + 16 + statuses + 16;
    float available = std::max(32.0f, limit - fixed);
    size_t capacity = static_cast<size_t>(std::max(0.0f, std::floor((available - 16) / 46)));
    bool overflow = groups.size() > capacity;
    if (overflow && capacity)
        capacity = static_cast<size_t>(std::max(0.0f, std::floor((available - 48) / 46)));
    out.visibleGroups = std::min(capacity, groups.size());
    out.firstGroup = groups.empty() ? 0 : std::min(first, groups.size() - 1);
    if (out.firstGroup + out.visibleGroups > groups.size())
        out.firstGroup = groups.size() - out.visibleGroups;
    float apps = groups.empty() ? 12.0f : 16 + 46 * static_cast<float>(out.visibleGroups) + (overflow ? 32.0f : 0.0f);
    apps = std::min(apps, available);
    float total = std::min(limit, fixed + apps), x = (width - total) / 2;
    out.pill = {x, 8, x + total, 64};
    auto add = [&](int id, float size, const std::wstring &name, const std::wstring &label, bool active = false) {
        out.hits.push_back({id, id, {x, 16, x + size, 56}, name, label, active});
        x += size;
    };
    x += 12;
    add(DockApps, compact ? 32.0f : 40.0f, L"Open Apps", L"mark");
    add(DockSearch, compact ? 32.0f : 40.0f, L"Search Dako commands, apps and windows", L"\xE721");
    if (options.showDesktop)
        add(DockDesktop, 40, L"Show desktop", L"\xE80F");
    if (options.showTaskView)
        add(DockTaskView, 40, L"Task view", L"\xE7C4");
    out.dividers.push_back(x + 8);
    x += 16;
    out.apps = {x, 12, x + apps, 60};
    std::vector<float> sizes;
    float occupied = 0;
    for (size_t slot = 0; slot < out.visibleGroups; ++slot) {
        float base = x + 8 + static_cast<float>(slot) * 46 + 20;
        float d = (pointer - base) / 60, size = 40 + 8 * std::clamp(strength, 0.0f, 1.0f) * std::exp(-d * d);
        sizes.push_back(size);
        occupied += size + (slot ? 6 : 0);
    }
    float appX = x + std::max(0.0f, (apps - (overflow ? 32 : 0) - occupied) / 2);
    for (size_t slot = 0; slot < out.visibleGroups; ++slot) {
        size_t i = out.firstGroup + slot;
        const auto &group = groups[i];
        if (i && group.pinned != groups[i - 1].pinned)
            out.dividers.push_back(appX - 3);
        float size = sizes[slot], center = 36;
        std::wstring name = group.name + (group.pinned ? L", pinned" : L"");
        name +=
            group.windows.empty() ? L", launch" : L", " + std::to_wstring(group.windows.size()) + L" running windows";
        out.hits.push_back({DockGroupFirst + static_cast<int>(i),
                            DockGroupFirst + static_cast<int>(i),
                            {appX, center - size / 2, appX + size, center + size / 2},
                            name,
                            L"\xE8A5",
                            group.active});
        appX += size + 6;
    }
    x = out.apps.right;
    if (overflow) {
        float scrollX = std::max(out.apps.left, x - 32);
        out.hits.push_back({DockOverflow,
                            DockOverflow,
                            {scrollX, 16, scrollX + 32, 56},
                            L"Scroll pinned and running apps",
                            L"\xE76C"});
    }
    out.dividers.push_back(x + 8);
    x += 16;
    for (auto item : statusItems) {
        std::wstring label = item.first == DockClock     ? status.clock
                             : item.first == DockVolume  ? L"\xE767"
                             : item.first == DockWifi    ? L"\xE701"
                             : item.first == DockBattery ? L"\xE850"
                                                         : L"\xE706";
        std::wstring name = item.first == DockClock     ? L"Clock and calendar. " + status.clock + L". " + status.date
                            : item.first == DockVolume  ? L"Volume. " + status.volume
                            : item.first == DockWifi    ? L"Wi-Fi. " + status.wifi
                            : item.first == DockBattery ? L"Battery. " + status.battery
                                                        : L"Weather. " + status.weather;
        add(item.first, item.second, name, label);
    }
    add(DockStatusMore, 32, L"Status and tray controls", L"\xE70D");
    std::sort(out.hits.begin(), out.hits.end(), [](const auto &a, const auto &b) { return a.rect.left < b.rect.left; });
    return out;
}
class DockShell {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    struct Callbacks {
        std::function<void(int, HMONITOR)> open;
        std::function<void(const std::wstring &)> launch;
        std::function<void(size_t, size_t)> reorder;
        std::function<void(const std::wstring &, bool)> pin;
        std::function<void(const std::wstring &)> error;
        std::function<void()> context;
        std::function<void(const std::wstring &, HMONITOR)> recent;
    };
    DockShell();
    ~DockShell();
    bool Start(HWND owner, Shell *recovery, bool preview, float textScale, Callbacks callbacks);
    void Stop();
    void RefreshMonitors();
    void ExplorerRestarted();
    void SetData(std::vector<PinEntry> pins, std::vector<WindowEntry> windows, DockStatus status);
    void Options(DockOptions options);
    RECT PillBounds(HMONITOR monitor) const;
    POINT Anchor(HMONITOR monitor, int action) const;
    bool CompositionAvailable() const;
    void Appearance(bool reduced, bool highContrast, float textScale);
    void RefreshFullscreen();
    void Flash(HWND window);
    void Focus(HMONITOR monitor = nullptr);
    void ActivateNumber(size_t index);
    RECT Bounds(HMONITOR monitor) const;
    HMONITOR ActiveMonitor() const;
    size_t Count() const;
    bool Contains(HWND window) const;
    bool PreviewAvailable() const;
    static bool RenderFixture(const std::wstring &path, UINT dpi, float textScale, int variant = 0);
};
}
