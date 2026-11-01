#include "../src/SurfaceMotion.h"
#include <cstdio>
#include <cstdlib>

void RequireSurface(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
int main() {
    unsigned cases = 0;
    for (auto control :
         {surface::Control::Apps, surface::Control::Search, surface::Control::Wifi, surface::Control::Volume,
          surface::Control::Battery, surface::Control::Clock, surface::Control::Weather, surface::Control::More,
          surface::Control::App, surface::Control::Dako, surface::Control::Media}) {
        auto request = surface::Describe(control, surface::Trigger::Click, 3);
        surface::SurfaceMotion motion;
        motion.Rest(360, 0);
        motion.Open(420, surface::Pixels(request.width), 520);
        motion.Step(.02f);
        RequireSurface(motion.height.value > 0 && motion.width.value == 360,
                       "height leads width for all panel controls");
        float elapsed = .02f;
        while (motion.Moving() && elapsed < .4f) {
            motion.Step(1.0f / 240);
            elapsed += 1.0f / 240;
        }
        RequireSurface(!motion.Moving() && elapsed <= .32f, "every panel opens within 320 ms");
        motion.Open(620, 480, 440);
        motion.Step(.03f);
        float velocity = motion.anchor.velocity;
        motion.Open(250, 800, 380);
        RequireSurface(motion.anchor.velocity == velocity, "neighbor retarget preserves spring velocity");
        elapsed = 0;
        while (motion.Moving() && elapsed < .3f) {
            motion.Step(1.0f / 240);
            elapsed += 1.0f / 240;
        }
        RequireSurface(!motion.Moving() && elapsed <= .24f, "retarget completes within 240 ms");
        motion.Close();
        float height = motion.height.value;
        motion.Step(.04f);
        RequireSurface(motion.content.value < 1 && motion.height.value == height, "content fades before shrink");
        motion.Open(250, 360, 240, true);
        RequireSurface(!motion.Moving() && motion.content.value == 1, "reduced motion emits no intermediate frame");
        motion.Close(true);
        RequireSurface(!motion.Moving() && motion.phase == surface::SurfaceMotion::Phase::Closed,
                       "settled closed state owns no deadlines or animation");
        ++cases;
    }
    for (auto control : {surface::Control::Wifi, surface::Control::Volume, surface::Control::Battery,
                         surface::Control::Clock, surface::Control::Weather})
        RequireSurface(surface::Describe(control).kind == surface::Kind::QuickPanel,
                       "all status clicks deep-link the same Quick Panel");
    for (auto control : {surface::Control::Desktop, surface::Control::TaskView, surface::Control::Start})
        RequireSurface(surface::Describe(control).behavior == surface::Behavior::Action,
                       "one-shot shell controls are Actions and never open a panel");
    RequireSurface(surface::Describe(surface::Control::Search, surface::Trigger::SearchKey).behavior ==
                           surface::Behavior::Action &&
                       surface::Describe(surface::Control::App, surface::Trigger::Click, 1).behavior ==
                           surface::Behavior::Action &&
                       surface::Describe(surface::Control::App, surface::Trigger::Click, 3).behavior ==
                           surface::Behavior::Panel,
                   "Search key and single-window activation are Actions; multi-window clicks open a Panel");
    for (auto trigger : {surface::Trigger::Touch, surface::Trigger::Pen})
        RequireSurface(surface::Describe(surface::Control::Wifi, trigger).behavior == surface::Behavior::Panel,
                       "touch and pen open a panel directly without a peek");
    surface::SurfaceMotion label;
    label.label = true;
    label.Open(200, 360, 28);
    label.Step(.05f);
    label.Step(.05f);
    RequireSurface(label.content.value == 1 && !label.Moving(), "label chip fades within 100 ms");
    label.Close();
    label.Step(.05f);
    label.Step(.05f);
    RequireSurface(label.phase == surface::SurfaceMotion::Phase::Closed && !label.Moving(),
                   "label fade owns no timer after closing");
    label.Open(200, 360, 28);
    label.Step(.2f);
    RequireSurface(!label.Moving() && label.content.value == 1,
                   "label consumes a delayed frame without extending its fade");
    label.Open(500, 360, 28);
    label.Step(.05f);
    float labelVelocity = label.anchor.velocity;
    label.Open(350, 360, 28);
    RequireSurface(label.anchor.velocity == labelVelocity, "neighbor label retarget also preserves anchor velocity");
    surface::HoverPolicy hover;
    auto peek = surface::Describe(surface::Control::App, surface::Trigger::Hover, 2);
    unsigned stale = hover.Enter(peek);
    RequireSurface(hover.delay == 280, "first peek waits 280 ms");
    RequireSurface(hover.Pointer(true) == stale && hover.due == surface::HoverPolicy::Due::Show,
                   "motion within the same icon preserves the pending show deadline");
    hover.peeking = true;
    hover.Enter(peek);
    RequireSurface(hover.delay == 0 && hover.Consume(stale) == surface::HoverPolicy::Due::None,
                   "neighbor peek is immediate and rejects stale callbacks");
    hover.Pointer(false);
    RequireSurface(hover.delay == 450, "leave uses a one-shot grace period");
    hover.Pointer(true);
    RequireSurface(hover.due == surface::HoverPolicy::Due::None, "re-entry cancels leave without polling");
    hover.Close();
    std::printf("PASS: %u surface routes, bounded choreography, interruption, reduced motion and event-only hover.\n",
                cases);
}
