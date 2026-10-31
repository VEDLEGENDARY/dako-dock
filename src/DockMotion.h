#pragma once
#include "LayoutMetrics.h"
#include "ThemePalette.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <string_view>
#include <cwctype>

namespace dock {
struct Point {
    float x, y;
};
namespace motion {
struct Spec {
    float frequency, ratio, duration;
};
inline constexpr Spec Settle{24, 1, .30f}, Snappy{34, .92f, .14f}, Soft{18, 1, .50f}, Lively{26, .65f, .40f};
inline constexpr Spec Height{24, .9f, .30f}, Width{24 * .92f, 1, .26f}, TabTrail{26, 1, .22f};
inline constexpr Spec Press{34, .92f, .10f}, Fade{34, 1, .20f}, Focus{34, 1, .10f}, Collapse{34, 1, .12f};
inline constexpr Spec Carousel{24, .9f, .50f}, Task{34, .92f, .16f};
} // namespace motion
// Flared shoulders attach to the top edge; the lower center is narrower.
template <int Segments = 16>
inline std::array<Point, 2 * (Segments + 1) + 1> Notch(float left, float top, float width, float height) {
    std::array<Point, 2 * (Segments + 1) + 1> points{};
    for (int i = 0; i <= Segments; ++i) {
        float t = i / static_cast<float>(Segments), u = 1 - t;
        points[i] = {left + width * (3 * u * u * t * 0.11f + 3 * u * t * t * 0.11f + t * t * t * 0.22f),
                     top + height * (u * u * u + 3 * u * u * t)};
        points[Segments + 1 + i] = {
            left + width * (0.78f * u * u * u + 3 * u * u * t * 0.89f + 3 * u * t * t * 0.89f + t * t * t),
            top + height * (3 * u * t * t + t * t * t)};
    }
    points.back() = points[0];
    for (auto &point : points)
        point.y = top + height - (point.y - top);
    return points;
}
inline bool BrowserExecutable(std::wstring_view path) {
    path.remove_prefix(path.find_last_of(L"\\/") == std::wstring_view::npos ? 0 : path.find_last_of(L"\\/") + 1);
    constexpr std::wstring_view browsers[]{L"chrome.exe",   L"msedge.exe",    L"brave.exe",  L"firefox.exe",
                                           L"vivaldi.exe",  L"opera.exe",     L"arc.exe",    L"zen.exe",
                                           L"waterfox.exe", L"librewolf.exe", L"floorp.exe", L"chromium.exe",
                                           L"thorium.exe",  L"browser.exe"};
    for (auto browser : browsers) {
        if (path.size() != browser.size())
            continue;
        bool match = true;
        for (size_t i = 0; i < path.size(); ++i)
            if (std::towlower(path[i]) != browser[i]) {
                match = false;
                break;
            }
        if (match)
            return true;
    }
    return false;
}
inline float PanelTop(bool browser) { return browser ? 112.0f : 0.0f; }
template <int Segments = 16>
inline std::array<Point, 4 * (Segments + 1) + 1> Attached(float left, float top, float width, float height,
                                                          float corner = layout::Metrics::PanelRadius) {
    std::array<Point, 4 * (Segments + 1) + 1> out{};
    float r = std::min(corner, height / 2), shoulder = shape::Fillet;
    auto curve = [&](int offset, Point a, Point b, Point c, Point d) {
        for (int i = 0; i <= Segments; ++i) {
            float t = i / static_cast<float>(Segments), u = 1 - t;
            out[offset + i] = {u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                               u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y};
        }
    };
    curve(0, {left - shoulder, top}, {left - 3, top}, {left, top + 3}, {left, top + r});
    curve(Segments + 1, {left, top + height - r}, {left, top + height}, {left, top + height}, {left + r, top + height});
    curve(2 * (Segments + 1), {left + width - r, top + height}, {left + width, top + height},
          {left + width, top + height}, {left + width, top + height - r});
    curve(3 * (Segments + 1), {left + width, top + r}, {left + width, top + 3}, {left + width + 3, top},
          {left + width + shoulder, top});
    out.back() = out[0];
    return out;
}
// Exact damped integration. Retargeting preserves velocity; finite guards release the scheduler.
struct Spring {
    float value = 0, velocity = 0, goal = 0;
    float age = 0, trackedGoal = 0;
    explicit Spring(float initial = 0) : value(initial), goal(initial), trackedGoal(initial) {}
    void Snap(float next) {
        value = goal = next;
        velocity = 0;
        age = 0;
        trackedGoal = next;
    }
    bool Moving() const { return std::abs(goal - value) > 0.025f || std::abs(velocity) > 0.025f; }
    void Step(float seconds, float frequency = motion::Settle.frequency, float ratio = 1) {
        if (!Moving())
            return;
        if (trackedGoal != goal) {
            trackedGoal = goal;
            age = 0;
        }
        if (seconds > .5f) {
            Snap(goal);
            return;
        }
        seconds = std::clamp(seconds, 0.0f, 0.05f);
        age += seconds;
        frequency = std::max(1.0f, frequency);
        float displacement = value - goal;
        if (ratio < .999f) {
            ratio = std::clamp(ratio, .1f, .999f);
            float damped = frequency * std::sqrt(1 - ratio * ratio);
            float b = (velocity + ratio * frequency * displacement) / damped;
            float e = std::exp(-ratio * frequency * seconds), c = std::cos(damped * seconds);
            float s = std::sin(damped * seconds);
            value = goal + e * (displacement * c + b * s);
            velocity = e * ((b * damped - ratio * frequency * displacement) * c -
                            (displacement * damped + ratio * frequency * b) * s);
        } else {
            float c = velocity + frequency * displacement, e = std::exp(-frequency * seconds);
            value = goal + (displacement + c * seconds) * e;
            velocity = (velocity - frequency * c * seconds) * e;
        }
        if (!Moving() || age >= .599f)
            Snap(goal);
    }
    void Step(float seconds, motion::Spec spec, float speed = 1) {
        Step(seconds, spec.frequency * speed, spec.ratio);
        if (Moving() && age >= spec.duration / std::max(.5f, speed) - .0001f)
            Snap(goal);
    }
};
struct Proximity {
    float dwell = 0, outside = 0, cooldown = 0;
    void Dismiss() {
        dwell = outside = 0;
        cooldown = 0.8f;
    }
    int Step(float seconds, bool open, bool approaching, bool inside, bool busy) {
        seconds = std::clamp(seconds, 0.0f, 0.2f);
        cooldown = std::max(0.0f, cooldown - seconds);
        if (!open) {
            outside = 0;
            dwell = approaching && cooldown == 0 ? dwell + seconds : 0;
            return dwell >= 0.16f ? 1 : 0;
        }
        dwell = 0;
        outside = inside || busy ? 0 : outside + seconds;
        return outside >= 0.55f ? -1 : 0;
    }
};
inline unsigned Mix(unsigned from, unsigned to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    unsigned result = 0;
    for (int shift : {0, 8, 16}) {
        float a = static_cast<float>((from >> shift) & 255), b = static_cast<float>((to >> shift) & 255);
        result |= static_cast<unsigned>(std::lround(a + (b - a) * amount)) << shift;
    }
    return result;
}
} // namespace dock

namespace theme {
constexpr unsigned Paper = 0xEEEBE3, Ink = 0x0F0F0E, Signal = 0xFF4A1C;
inline unsigned Surface = palette::Tokens::Surface, Card = palette::Tokens::Card, CardHover = palette::Tokens::Raised;
inline unsigned Inset = palette::Tokens::Inset, Border = 0x232325;
inline unsigned Edge = palette::Tokens::White, Text = palette::Tokens::Text, Muted = palette::Tokens::Muted;
inline unsigned Danger = palette::Tokens::Danger;
inline unsigned Focus = 0xC03610, SelectedText = Ink;
inline unsigned Accent = Signal, AccentHover = 0xFF633B, AccentInk = Ink, AccentSoft = 0xF3DCD1, Secondary = 0x687F76,
                Tertiary = 0x677A8A;
inline constexpr float Radius = shape::Surface;
constexpr float SmallRadius = layout::Metrics::SmallRadius;
constexpr float InsetSize = layout::Metrics::Padding, Gap = layout::Metrics::Gap;
constexpr float DockWidth = 216, DockHeight = 18, TopRail = 0, EnvelopeWidth = 1000, EnvelopeHeight = 760;
} // namespace theme
