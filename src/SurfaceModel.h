#pragma once
#include <algorithm>
#include <string>

namespace surface {
enum class Kind { FullIsland, QuickPanel, Apps, Palette, AppContext, Preview, Status, Overflow, Tooltip, Media };
enum class Section { None, Wifi, Volume, Battery, Clock, Weather, Bluetooth, Audio };
enum class Width { Compact, Standard, Wide };
enum class Behavior { Action, Peek, Panel };
enum class Trigger { Click, Hover, Context, SearchKey, Touch, Pen };
enum class Control {
    Apps,
    Search,
    Wifi,
    Volume,
    Battery,
    Clock,
    Weather,
    More,
    Desktop,
    TaskView,
    Start,
    App,
    Dako,
    Media
};
inline constexpr float Compact = 360, Standard = 480, Wide = 800;
inline constexpr unsigned HoverDelay = 280, LabelDelay = 500, LeaveGrace = 450;
inline float Pixels(Width width) {
    return width == Width::Compact ? Compact : width == Width::Standard ? Standard : Wide;
}
struct Request {
    Kind kind = Kind::FullIsland;
    Section section = Section::None;
    Width width = Width::Wide;
    Behavior behavior = Behavior::Panel;
    int control = -1, tab = 0;
    std::wstring group;
    bool SameTarget(const Request &other) const {
        return kind == other.kind && section == other.section && control == other.control && group == other.group;
    }
};
inline Request Describe(Control control, Trigger trigger = Trigger::Click, size_t windows = 0) {
    Request request;
    request.control = static_cast<int>(control);
    if (trigger == Trigger::Hover &&
        (control == Control::Apps || control == Control::Search || control == Control::Dako ||
         control == Control::Desktop || control == Control::TaskView || control == Control::Start)) {
        request.behavior = Behavior::Peek;
        request.kind = Kind::Tooltip;
        request.width = Width::Compact;
        return request;
    }
    if (control == Control::Desktop || control == Control::TaskView || control == Control::Start ||
        (control == Control::Search && trigger == Trigger::SearchKey) ||
        (control == Control::App && trigger != Trigger::Hover && trigger != Trigger::Context && windows <= 1)) {
        request.behavior = Behavior::Action;
        return request;
    }
    request.behavior = trigger == Trigger::Hover ? Behavior::Peek : Behavior::Panel;
    switch (control) {
    case Control::Apps:
        request.kind = Kind::Apps;
        request.width = Width::Standard;
        break;
    case Control::Search:
        request.kind = Kind::Palette;
        request.width = Width::Standard;
        break;
    case Control::Dako:
        break;
    case Control::App:
        request.kind = trigger == Trigger::Context ? Kind::AppContext : windows ? Kind::Preview : Kind::Tooltip;
        request.width = Width::Compact;
        break;
    case Control::Media:
        request.kind = Kind::Media;
        request.width = Width::Compact;
        break;
    default:
        request.kind = Kind::QuickPanel;
        request.width = Width::Standard;
        request.section = control == Control::Wifi      ? Section::Wifi
                          : control == Control::Volume  ? Section::Volume
                          : control == Control::Battery ? Section::Battery
                          : control == Control::Clock   ? Section::Clock
                          : control == Control::Weather ? Section::Weather
                                                        : Section::None;
        break;
    }
    if (request.behavior == Behavior::Peek) {
        request.width = Width::Compact;
        if (request.kind == Kind::QuickPanel)
            request.kind = Kind::Status;
    }
    return request;
}
// Event-only hover policy. The HWND adapter owns a single cancellable deadline.
struct HoverPolicy {
    enum class Due { None, Show, Hide };
    Request pending;
    Due due = Due::None;
    unsigned generation = 0, delay = 0;
    bool peeking = false, pinned = false;
    unsigned Enter(const Request &request) {
        ++generation;
        due = Due::None;
        delay = 0;
        if (pinned)
            return generation;
        pending = request;
        due = Due::Show;
        delay = peeking ? 0 : request.kind == Kind::Tooltip ? LabelDelay : HoverDelay;
        return generation;
    }
    unsigned Pointer(bool inside) {
        if (inside && due == Due::Show)
            return generation;
        ++generation;
        due = !inside && peeking && !pinned ? Due::Hide : Due::None;
        delay = due == Due::Hide ? LeaveGrace : 0;
        return generation;
    }
    Due Consume(unsigned token) {
        if (token != generation)
            return Due::None;
        auto result = due;
        due = Due::None;
        delay = 0;
        return result;
    }
    void Close() {
        ++generation;
        due = Due::None;
        delay = 0;
        peeking = pinned = false;
    }
};
} // namespace surface
