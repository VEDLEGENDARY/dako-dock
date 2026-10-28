#include "../src/IslandState.h"
#include "../src/Anchor.h"
#include <cassert>
#include <cstdio>

static void Advance(island::Machine &machine, float seconds) {
    for (float time = 0; time < seconds; time += .01f)
        machine.Step(.01f);
}
int main() {
    using island::Mode;
    for (Mode initial : {Mode::Dock, Mode::Peek, Mode::Expanded}) {
        for (int event = 0; event < 6; ++event) {
            island::Machine machine;
            if (initial == Mode::Peek) {
                machine.OnMedia(true);
                Advance(machine, .4f);
                machine.OnPointer(true);
                Advance(machine, .2f);
            } else if (initial == Mode::Expanded)
                machine.OnExpand();
            switch (event) {
            case 0:
                machine.OnExpand();
                assert(machine.Current() == Mode::Expanded);
                break;
            case 1:
                machine.OnToggle();
                assert(machine.Current() == (initial == Mode::Expanded ? Mode::Dock : Mode::Expanded));
                break;
            case 2:
                machine.OnDismiss();
                assert(machine.Current() == Mode::Dock);
                break;
            case 3:
                machine.OnPointer(false);
                Advance(machine, 1.5f);
                assert(machine.Current() == Mode::Dock);
                break;
            case 4:
                machine.OnBusy(true);
                machine.OnPointer(false);
                Advance(machine, 1);
                assert(machine.Current() == initial);
                break;
            case 5:
                machine.OnPointer(true);
                Advance(machine, .3f);
                assert(machine.Current() == (initial == Mode::Dock ? Mode::Expanded : initial));
                break;
            }
        }
    }
    island::Machine machine;
    machine.OnMedia(true);
    machine.OnPointer(true);
    Advance(machine, .18f);
    assert(machine.Current() == Mode::Dock);
    Advance(machine, .2f);
    assert(machine.Current() == Mode::Peek);
    for (int i = 0; i < 20; ++i) {
        machine.OnMedia(false);
        Advance(machine, .04f);
        machine.OnMedia(true);
        Advance(machine, .04f);
        assert(machine.Current() == Mode::Peek);
    }
    machine.OnPointer(false);
    Advance(machine, 2);
    assert(!machine.NeedsTick());
    for (float width : {216.0f, 580.0f, 800.0f})
        for (float height : {18.0f, 130.0f, 390.0f}) {
            auto top = dock::Attached(30, 40, width, height);
            auto bottom = anchor::ForEdge(anchor::Edge::Bottom, top, 40, height);
            for (float x = 0; x < width + 80; x += 7)
                for (float y = 35; y < height + 50; y += 7)
                    assert(anchor::Contains(top, x, y) ==
                           anchor::Contains(bottom, x, anchor::MirrorY(y, 40 + height / 2)));
        }
    std::puts("PASS event/state matrix, media churn debounce, stopped idle deadlines and mirrored polygon hit testing");
}
