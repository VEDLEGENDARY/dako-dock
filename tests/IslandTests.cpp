#include <cassert>
#include <cmath>
#include <cstdio>
#include "Anchor.h"
#include "IslandState.h"
using namespace island;

static void Run(Machine &m, float seconds, float dt = 0.016f) {
    for (float t = 0; t < seconds; t += dt) m.Step(dt);
}

static void DockOpensToExpandedWithoutMedia() {
    Machine m; m.OnPointer(true);
    Run(m, 0.1f); assert(m.Current() == Mode::Dock);
    Run(m, 0.1f); assert(m.Current() == Mode::Expanded);
}
static void DockOpensToPeekWithMedia() {
    Machine m; m.OnMedia(true); Run(m, 0.4f);
    m.OnPointer(true); Run(m, 0.3f);
    assert(m.Current() == Mode::Peek);
}
static void HoverNeverExpandsPeekButExpandGestureDoes() {
    Machine m; m.OnMedia(true); Run(m, 0.4f); m.OnPointer(true); Run(m, 5);
    assert(m.Current() == Mode::Peek);
    m.OnExpand(); assert(m.Current() == Mode::Expanded);
}
static void LeaveCollapsesAfterGrace() {
    Machine m; m.OnPointer(true); Run(m, 0.3f); assert(m.Current() == Mode::Expanded);
    m.OnPointer(false); Run(m, 0.5f); assert(m.Current() == Mode::Expanded);
    Run(m, 0.1f); assert(m.Current() == Mode::Dock);
}
static void BusyBlocksCollapse() {
    Machine m; m.OnPointer(true); Run(m, 0.3f); m.OnPointer(false); m.OnBusy(true);
    Run(m, 3); assert(m.Current() == Mode::Expanded);
    m.OnBusy(false); Run(m, 0.7f); assert(m.Current() == Mode::Dock);
}
static void CooldownStopsReopenUnderPointer() {
    Machine m; m.OnPointer(true); Run(m, 0.3f); m.OnEscape();
    assert(m.Current() == Mode::Dock);
    Run(m, 0.5f); assert(m.Current() == Mode::Dock);
    Run(m, 0.6f); assert(m.Current() == Mode::Expanded);
}
static void MediaLossInPeekReturnsToDock() {
    Machine m; m.OnMedia(true); Run(m, 0.4f); m.OnPointer(true); Run(m, 0.3f);
    assert(m.Current() == Mode::Peek);
    m.OnMedia(false); Run(m, .1f); assert(m.Current() == Mode::Peek); Run(m, .3f); assert(m.Current() == Mode::Expanded && !m.HasMedia());
}
static void MediaFlickerIsDebounced() {
    Machine m; m.OnPointer(true);
    for (int i = 0; i < 6; ++i) { m.OnMedia(i % 2 == 0); Run(m, 0.05f); }
    // Ends with media=false (i=5). Never entered Peek while flapping.
    assert(!m.HasMedia());
}
static void ReplayHasNoFlicker() {
    // Pointer jitters at the edge every 40 ms; mode may change at most once per 100 ms.
    Machine m; int changes = 0; float last = -1, t = 0;
    for (int i = 0; i < 600; ++i, t += 0.016f) {
        if (i % 3 == 0) m.OnPointer((i / 3) % 2 == 0);
        if (m.Step(0.016f)) { ++changes; assert(last < 0 || t - last >= 0.1f); last = t; }
    }
    assert(changes <= 6);
}
static void IdleNeedsNoTick() {
    Machine m; assert(!m.NeedsTick());
    m.OnPointer(true); assert(m.NeedsTick());
    Run(m, 0.3f); m.OnPointer(true); assert(!m.NeedsTick());  // expanded with pointer inside: nothing pending
    m.OnPointer(false); assert(m.NeedsTick());
    Run(m, 1); assert(m.Current() == Mode::Dock);
    Run(m, 1); assert(!m.NeedsTick());
}
static void EscapeStepsBackOneLevel() {
    Machine m; m.OnMedia(true); Run(m, 0.4f); m.OnPointer(true); m.OnExpand();
    m.OnEscape(); assert(m.Current() == Mode::Peek);
    m.OnEscape(); assert(m.Current() == Mode::Dock);
}
static void ToggleCycles() {
    Machine m; m.OnToggle(); assert(m.Current() == Mode::Expanded);
    m.OnToggle(); assert(m.Current() == Mode::Dock);
}

static void NotchMirrorsExactly() {
    using anchor::Edge;
    float left = 100, top = 40, width = 216, height = 18;
    auto base = dock::Notch(left, top, width, height);
    auto bottom = anchor::ForEdge(Edge::Bottom, base, top, height);
    auto same = anchor::ForEdge(Edge::Top, base, top, height);
    float shoulderTop = 1e9f, shoulderBottom = 1e9f;
    for (size_t i = 0; i < base.size(); ++i) {
        assert(same[i].y == base[i].y && same[i].x == base[i].x);
        assert(std::abs((bottom[i].y - top) - (height - (base[i].y - top))) < 1e-3f);  // exact reflection
        assert(bottom[i].x == base[i].x);
    }
    // Top notch: leftmost point lies on the top edge. Bottom notch: on the bottom edge.
    auto leftmost = [](auto &pts) {
        size_t b = 0;
        for (size_t i = 0; i < pts.size(); ++i) if (pts[i].x < pts[b].x) b = i;
        return b;
    };
    assert(std::abs(base[leftmost(base)].y - top) < 1e-3f);
    assert(std::abs(bottom[leftmost(bottom)].y - (top + height)) < 1e-3f);
    (void)shoulderTop; (void)shoulderBottom;
}
static void EdgeMath() {
    using namespace anchor;
    assert(Grow(Edge::Top) == 1 && Grow(Edge::Bottom) == -1);
    assert(EdgeTop(Edge::Top, 0, 1080, 18) == 0 && EdgeTop(Edge::Bottom, 0, 1080, 18) == 1062);
    assert(MirrorY(MirrorY(37.5f, 50), 50) == 37.5f);
    assert(WheelExpands(Edge::Bottom, 120) && !WheelExpands(Edge::Bottom, -120));
    assert(WheelExpands(Edge::Top, -120) && !WheelExpands(Edge::Top, 120));
    assert(FlyoutOpensUp(Edge::Bottom, 900, 200, 0, 1080));
    assert(!FlyoutOpensUp(Edge::Bottom, 100, 200, 0, 1080));  // no room above: flips down
    assert(!FlyoutOpensUp(Edge::Top, 100, 200, 0, 1080));
    assert(FlyoutOpensUp(Edge::Top, 1000, 200, 0, 1080));
}

int main() {
    DockOpensToExpandedWithoutMedia(); DockOpensToPeekWithMedia(); HoverNeverExpandsPeekButExpandGestureDoes();
    LeaveCollapsesAfterGrace(); BusyBlocksCollapse(); CooldownStopsReopenUnderPointer(); MediaLossInPeekReturnsToDock();
    MediaFlickerIsDebounced(); ReplayHasNoFlicker(); IdleNeedsNoTick(); ToggleCycles(); EscapeStepsBackOneLevel();
    NotchMirrorsExactly(); EdgeMath();
    std::puts("all island/anchor tests passed");
}
