#define UNICODE
#define _UNICODE
#include "../src/DockShell.h"
#include "../src/IslandState.h"
#include "../src/OneShotDeadline.h"
#include "../src/SlideshowPlan.h"
#include <cassert>
#include <iostream>

void Pump(unsigned milliseconds) {
    auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    }
}
unsigned notices = 0;
WPARAM lastToken = 0;
LRESULT CALLBACK Notice(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_APP + 1) {
        ++notices;
        lastToken = wparam;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int main() {
    desktop::DockOptions options;
    options.island = true;
    std::vector<desktop::DockGroup> groups(80);
    auto layout = desktop::SolveDock(900, groups, {}, options);
    assert(layout.visibleGroups < groups.size());
    auto launcher = std::find_if(layout.hits.begin(), layout.hits.end(),
                                 [](const auto &hit) { return hit.action == desktop::DockApps; });
    assert(launcher != layout.hits.end());
    island::Machine state;
    for (const auto &hit : layout.hits)
        if (hit.action >= desktop::DockGroupFirst) {
            assert(!desktop::DockDwellContains(layout, hit.rect.CenterX(), 36));
            state.OnPointer(desktop::DockDwellContains(layout, hit.rect.CenterX(), 36));
            for (int step = 0; step < 100; ++step)
                state.Step(.016f);
            assert(state.Current() == island::Mode::Dock && !state.NeedsTick());
        }
    state.OnPointer(desktop::DockDwellContains(layout, launcher->rect.CenterX(), 36));
    for (int step = 0; step < 30; ++step)
        state.Step(.016f);
    assert(state.Current() == island::Mode::Expanded);
    for (auto edge : {anchor::Edge::Top, anchor::Edge::Bottom})
        for (bool reverse : {false, true}) {
            options.edge = edge;
            options.reversedScroll = reverse;
            assert(desktop::ResolveDockWheel(layout, options, layout.apps.CenterX(), 36, 120) ==
                   desktop::DockWheel::PreviousApps);
            assert(desktop::ResolveDockWheel(layout, options, layout.apps.CenterX(), 36, -120) ==
                   desktop::DockWheel::NextApps);
            assert(desktop::ResolveDockWheel(layout, options, layout.apps.CenterX(), 36, 120, true) ==
                   desktop::DockWheel::NextApps);
            int toward = edge == anchor::Edge::Bottom ? 120 : -120;
            if (reverse)
                toward = -toward;
            assert(desktop::ResolveDockWheel(layout, options, launcher->rect.CenterX(), 36, toward) ==
                   desktop::DockWheel::Expand);
        }
    assert(island::MediaAllowsPeek(true, true, false, true, true));
    assert(!island::MediaAllowsPeek(true, false, true, true, true));
    assert(island::MediaAllowsPeek(true, false, true, true, false));
    assert(!island::MediaAllowsPeek(true, false, true, false, false));
    SlideshowPlan plan;
    assert(plan.Configure(L"fixture-folder", 1));
    auto generation = plan.Generation();
    plan.Arm();
    assert(!plan.Configure(L"fixture-folder", 1) && plan.Generation() == generation);
    assert(plan.Due(generation) && !plan.Due(generation));
    assert(plan.Complete(generation, true));
    plan.Arm();
    assert(plan.Configure(L"other-folder", 2) && !plan.Due(generation));
    generation = plan.Generation();
    plan.Arm();
    assert(plan.Due(generation) && !plan.Complete(generation, false) && !plan.Due(generation));
    assert(plan.Configure(L"", 0) && !plan.Enabled());
    WNDCLASSW type{};
    type.lpfnWndProc = Notice;
    type.lpszClassName = L"Dako.Deadline.Test";
    type.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&type);
    HWND window =
        CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    assert(window);
    {
        OneShotDeadline deadline(window, WM_APP + 1);
        assert(deadline.Arm(5, 12));
        Pump(100);
        assert(notices == 1 && lastToken == 12);
        Pump(50);
        assert(notices == 1);
        assert(deadline.Arm(200, 13));
        deadline.Cancel();
        Pump(250);
        assert(notices == 1);
        assert(deadline.Arm(5, 14));
        Pump(100);
        assert(notices == 2 && lastToken == 14);
    }
    DestroyWindow(window);
    std::cout << "PASS icon dwell exclusion, mirrored bidirectional app wheel, playing-only Peek, unchanged slideshow "
                 "configuration, stale/completion guards and one-shot cancellation; no wallpaper writes\n";
}
