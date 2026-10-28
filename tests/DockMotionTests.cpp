#include "../src/DockMotion.h"
#include "../src/ThemePalette.h"
#include "../src/LayoutEngine.h"
#include "../src/MotionController.h"
#include <cstdio>
#include <cstdlib>

void Require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
int main() {
    Require(dock::BrowserExecutable(L"C:\\Program Files\\BraveSoftware\\Brave.exe") &&
                dock::BrowserExecutable(L"firefox.exe"),
            "browser names and paths get browser clearance");
    Require(!dock::BrowserExecutable(L"Code.exe") && !dock::BrowserExecutable(L"discord.exe"),
            "Chromium-based desktop apps keep normal popup positioning");
    Require(dock::PanelTop(true) == 112 && dock::PanelTop(false) == 0,
            "only browsers detach; normal panels attach at the screen edge");
    unsigned char blue[64], red[64];
    for (int i = 0; i < 64; i += 4) {
        blue[i] = 210;
        blue[i + 1] = 110;
        blue[i + 2] = 40;
        blue[i + 3] = 255;
        red[i] = 40;
        red[i + 1] = 60;
        red[i + 2] = 210;
        red[i + 3] = 255;
    }
    unsigned blueAccent = palette::Accent(blue, 64), redAccent = palette::Accent(red, 64);
    Require(blueAccent != redAccent && palette::ToHsl(blueAccent).h > 0.5f && palette::ToHsl(redAccent).h < 0.1f,
            "wallpaper hue drives the accent");
    Require(palette::RevealRadius(0, 1920, 1080) == 0 &&
                palette::RevealRadius(0.5f, 1920, 1080) < palette::RevealRadius(1, 1920, 1080) &&
                palette::RevealRadius(1, 1920, 1080) > 1101,
            "circular reveal grows from center through all corners");
    const auto notch = dock::Notch(0, 0, 216, 18);
    Require(notch[0].y == 0 && notch[16].y == 18 && notch[17].y == 18 && notch[33].y == 0,
            "notch attaches to the top with downward shoulders");
    dock::Spring reveal(0);
    reveal.goal = 1;
    float previous = 0;
    for (int frame = 0; frame < 120; ++frame) {
        reveal.Step(1.0f / 120);
        Require(reveal.value >= previous - 0.00001f && reveal.value <= 1.00001f, "reveal must not overshoot");
        previous = reveal.value;
    }
    Require(reveal.value == 1 && !reveal.Moving(), "finite reveal settles and stops its timer");
    dock::Spring reversal(0);
    reversal.goal = 600;
    for (int frame = 0; frame < 10; ++frame)
        reversal.Step(1.0f / 60);
    float position = reversal.value, velocity = reversal.velocity;
    reversal.goal = 148;
    Require(reversal.value == position && reversal.velocity == velocity, "reversal preserves position and velocity");
    for (int frame = 0; frame < 120; ++frame)
        reversal.Step(1.0f / 60);
    Require(reversal.value == 148 && !reversal.Moving(), "interrupted animation settles at new destination");
    dock::Proximity input;
    Require(input.Step(0.1f, false, true, false, false) == 0, "passing through the edge does not open immediately");
    Require(input.Step(0.1f, false, false, false, false) == 0, "leaving resets the hover dwell");
    Require(input.Step(0.1f, false, true, false, false) == 0 && input.Step(0.1f, false, true, false, false) == 1,
            "deliberate dwell opens the dock");
    for (int i = 0; i < 20; ++i)
        Require(input.Step(0.1f, true, false, true, false) == 0, "travel corridor keeps dock open");
    for (int i = 0; i < 20; ++i)
        Require(input.Step(0.1f, true, false, false, true) == 0, "editing and dialogs prevent auto-dismiss");
    int action = 0;
    for (int i = 0; i < 6; ++i)
        action = input.Step(0.1f, true, false, false, false);
    Require(action == -1, "pointer departure dismisses after the grace period");
    input.Dismiss();
    for (int i = 0; i < 7; ++i)
        Require(input.Step(0.1f, false, true, false, false) == 0, "Escape cooldown prevents immediate reopening");
    dock::Scheduler scheduler;
    int starts = 0;
    for (int i = 0; i < 1000; ++i)
        if (scheduler.Start())
            ++starts;
    Require(starts == 1 && scheduler.active, "retargeting owns one timer and never rearms it");
    Require(scheduler.Stop() && !scheduler.Stop(), "scheduler stops exactly once");
    dock::Feedback pulse;
    pulse.value = 1;
    float minimum = 1;
    for (int i = 0; i < 180; ++i) {
        pulse.Step(i % 2 ? 0.012f : 0.021f);
        minimum = std::min(minimum, pulse.value);
    }
    Require(pulse.value == 0 && !pulse.Moving() && minimum < 0 && minimum > -0.2f,
            "feedback has bounded overshoot and finite termination");
    pulse.value = 1;
    pulse.Step(2);
    Require(pulse.value == 0 && !pulse.Moving(), "resume settles feedback instead of replaying a stall");
    for (float dpi : {96.0f, 120.0f, 144.0f, 168.0f, 192.0f})
        for (auto physical : std::array<dock::Point, 4>{{{1920, 1080}, {1366, 768}, {1080, 1920}, {2560, 1440}}})
            for (int v = 0; v < 8; ++v) {
                float aw = std::min(900.0f, physical.x * 96 / dpi) - 40,
                      ah = std::min(760.0f, physical.y * 96 / dpi) - 124;
                auto l = layout::Solve({aw, ah, static_cast<layout::View>(v), 100});
                Require(l.panel.Width() <= aw && l.panel.Height() <= ah,
                        "panel is bounded at all supported DPI/monitor cases");
                Require(l.body.Height() > 0 && l.body.top >= l.header.bottom && l.body.bottom < l.status.top,
                        "header, content and status have separate usable geometry");
                if (l.footer.Height() > 0)
                    Require(l.body.bottom < l.footer.top && l.footer.bottom < l.status.top,
                            "footer and status never overlap content");
                if (v == 5) {
                    float left = l.stage.CenterX() - l.previous.CenterX(), right = l.next.CenterX() - l.stage.CenterX();
                    Require(std::abs(std::lround(left * dpi / 96) - std::lround(right * dpi / 96)) <= 1,
                            "wallpaper arrows mirror within one physical pixel");
                }
            }
    for (float text : {1.0f, 1.25f, 1.5f, 2.0f, 2.25f})
        for (float dpi : {96.0f, 144.0f, 192.0f})
            for (int v = 0; v < 9; ++v) {
                float availableWidth = (std::min(900.0f, 1366 * 96 / dpi) - 40) / text,
                      availableHeight = (std::min(760.0f, 768 * 96 / dpi) - 124) / text;
                auto l = layout::Solve({availableWidth, availableHeight, static_cast<layout::View>(v), 100});
                Require(l.panel.Width() <= availableWidth + 0.01f && l.panel.Height() <= availableHeight + 0.01f,
                        "large text reflows within physical monitor bounds");
                Require(l.body.Height() > 0 && l.body.Width() > 0, "large text retains a positive scroll viewport");
                if (l.emergency && v != 8)
                    Require(l.headerHeight == 32 && l.footer.Height() == 0,
                            "short large-text layouts retain compact navigation and menu actions");
                if (v == 8)
                    Require(l.panel.Height() >= 80, "compact transport has space for metadata and controls");
            }
    auto pages = layout::Paginate({0, 0, 1, 1, 2, 2, 2, 2}, 180);
    size_t entries = 0;
    for (auto page : pages) {
        float h = 0;
        int group = -1;
        for (size_t i = page.begin; i < page.end; ++i) {
            int g = i < 2 ? 0 : i < 4 ? 1 : 2;
            h += 44 + (g == group ? 0 : 24);
            group = g;
        }
        Require(h <= 180, "app page capacity includes every group heading");
        entries += page.end - page.begin;
    }
    Require(entries == 8, "pagination loses and duplicates no apps");
    Require(layout::SkewContains({0, 0, 100, 100}, -0.2f, 10, 50, 50),
            "inverse transform accepts the visible card center");
    Require(!layout::SkewContains({0, 0, 100, 100}, -0.2f, 10, 100, 99),
            "inverse transform rejects empty skew corners");
    std::puts("PASS: spring/reversal, finite feedback, scheduler ownership, proximity, 295 monitor/DPI/text layout "
              "cases, pagination and transformed hits.");
}
