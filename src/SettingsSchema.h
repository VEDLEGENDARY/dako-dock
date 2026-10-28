#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace v4 {
enum class Kind { Boolean, Integer, Choice, Text };
struct Setting {
    const wchar_t *key, *label, *page, *value, *description;
    Kind kind;
    int minimum = 0, maximum = 1;
    const wchar_t *choices = L"";
    bool live = false, restart = false;
};
inline constexpr Setting Schema[]{
    {L"Edge", L"Screen edge", L"Island", L"Bottom", L"Attach the island to this screen edge.", Kind::Choice, 0, 1,
     L"Bottom;Top", true},
    {L"Monitor", L"Monitor", L"Island", L"Primary", L"Primary screen, screen under the pointer, or every screen.",
     Kind::Choice, 0, 2, L"Primary;Cursor;Each", true},
    {L"Scale", L"Island scale (%)", L"Island", L"100", L"Scale the island in addition to Windows text scaling.",
     Kind::Integer, 75, 150, L"", true},
    {L"Radius", L"Corner radius", L"Island", L"20", L"Panel corner radius in logical pixels.", Kind::Integer, 8, 32,
     L"", true},
    {L"Opacity", L"Idle opacity (%)", L"Island", L"100", L"Opacity of the resting app strip.", Kind::Integer, 30, 100,
     L"", true},
    {L"AutoHide", L"Auto-hide", L"Island", L"0", L"Keep an edge affordance while the pointer is away.", Kind::Boolean,
     0, 1, L"", true},
    {L"IconSize", L"App icon size", L"Apps", L"36", L"App icons in logical pixels.", Kind::Integer, 24, 44, L"", true},
    {L"Magnify", L"Hover magnification", L"Apps", L"1", L"Magnify app icons near the pointer.", Kind::Boolean, 0, 1,
     L"", true},
    {L"Labels", L"App labels", L"Apps", L"1", L"Show app names in hover tooltips.", Kind::Boolean, 0, 1, L"", true},
    {L"RunningDots", L"Running indicators", L"Apps", L"1", L"Show a dot for running apps.", Kind::Boolean, 0, 1, L"",
     true},
    {L"Theme", L"Theme", L"Appearance", L"Midnight", L"Choose Midnight, Paper, or follow Windows.", Kind::Choice, 0, 2,
     L"Midnight;Paper;System", true},
    {L"Accent", L"Accent (hex RGB)", L"Appearance", L"CFAE60", L"Six hexadecimal digits, for example D4B35C.",
     Kind::Text, 0, 1, L"", true},
    {L"Animation", L"Animation speed (%)", L"Appearance", L"100", L"Speed of the shared shape spring.", Kind::Integer,
     50, 200, L"", true},
    {L"ReducedMotion", L"Reduced motion", L"Appearance", L"0", L"Use instant transitions without magnification.",
     Kind::Boolean, 0, 1, L"", true},
    {L"HoverDelay", L"Hover delay (ms)", L"Behavior", L"160", L"Dwell before Peek or Expanded opens.", Kind::Integer,
     80, 1500},
    {L"LeaveDelay", L"Leave delay (ms)", L"Behavior", L"550", L"Grace period before returning to the dock.",
     Kind::Integer, 150, 2000},
    {L"Scroll", L"Expand scroll direction", L"Behavior", L"Natural", L"Natural scroll moves toward the screen centre.",
     Kind::Choice, 0, 2, L"Natural;Reversed"},
    {L"Hotkey", L"Open shortcut", L"Behavior", L"Space", L"Ctrl+Alt plus the selected key.", Kind::Choice, 0, 3,
     L"Space;D;I;O"},
    {L"Fullscreen", L"Fullscreen policy", L"Overlay", L"Always show",
     L"Exclusive fullscreen and higher Windows bands have platform limits.", Kind::Choice, 0, 2,
     L"Always show;Hide over games;Hide over fullscreen"},
    {L"HideApps", L"Hide in these apps", L"Overlay", L"", L"Exact executable names separated by semicolons.",
     Kind::Text},
    {L"Tabs", L"Visible tabs (in order)", L"Content", L"Controls;Today;Stats;Clipboard;Apps;Wallpaper;Power",
     L"Semicolon-separated tab names; keep at least one.", Kind::Text, 0, 1, L"", true},
    {L"MediaSources", L"Media sources", L"Content", L"",
     L"Allowed source app IDs separated by semicolons; empty allows all.", Kind::Text},
    {L"PeekPlayingOnly", L"Peek only while playing", L"Content", L"1",
     L"Paused sessions open the full island on hover; turn off to include paused media in Peek.", Kind::Boolean},
    {L"ClipboardSize", L"Clipboard history size", L"Clipboard", L"25",
     L"Bounded optional history, enabled separately in Features.", Kind::Integer, 1, 25},
    {L"ClipboardRetention", L"Clipboard retention (minutes)", L"Clipboard", L"10",
     L"Unpinned records expire after this interval.", Kind::Integer, 1, 1440},
    {L"ClipboardIgnore", L"Ignore clipboard apps", L"Clipboard", L"",
     L"Exact source executable names, separated by semicolons.", Kind::Text},
    {L"WallpaperFolder", L"Wallpaper folder", L"Wallpaper", L"", L"A local folder; images remain on this PC.",
     Kind::Text},
    {L"Slideshow", L"Slideshow interval (minutes)", L"Wallpaper", L"0",
     L"Zero disables the explicitly configured slideshow.", Kind::Integer, 0, 1440},
    {L"WeatherCity", L"Weather city", L"Privacy", L"", L"Opt-in Open-Meteo city search; Offline still blocks requests.",
     Kind::Text},
    {L"SpeedTest", L"Allow speed test", L"Privacy", L"0",
     L"Explicit Cloudflare download/upload tests use internet data.", Kind::Boolean},
    {L"Offline", L"Offline", L"Privacy", L"1", L"Block Dako web requests, including weather and speed testing.",
     Kind::Boolean},
    {L"Startup", L"Start with Windows", L"General", L"0", L"Start this portable copy at sign-in for this user.",
     Kind::Boolean},
    {L"ReplaceTaskbar", L"Replace Windows taskbar", L"General", L"0",
     L"Requires the dock's separate consent and recovery guard.", Kind::Boolean},
    {L"DefaultTab", L"Default tab", L"Content", L"Controls", L"Page opened by the shortcut or hover without media.",
     Kind::Choice, 0, 6, L"Controls;Today;Stats;Clipboard;Apps;Wallpaper;Power", true},
    {L"ClipboardEncrypted", L"Encrypted clipboard storage", L"Clipboard", L"0",
     L"Optional CurrentUser DPAPI storage; memory is the default.", Kind::Boolean},
    {L"MediaScrubber", L"Media progress", L"Content", L"1", L"Display elapsed and remaining media time.", Kind::Boolean,
     0, 1, L"", true},
    {L"SafeMode", L"Safe mode on next launch", L"Advanced", L"0",
     L"Disable optional modules and taskbar replacement on launch.", Kind::Boolean, 0, 1, L"", false, true},
};
inline std::vector<std::wstring> Split(std::wstring_view value) {
    std::vector<std::wstring> result;
    while (!value.empty()) {
        auto end = value.find(L';');
        auto item = value.substr(0, end);
        if (!item.empty())
            result.emplace_back(item);
        if (end == value.npos)
            break;
        value.remove_prefix(end + 1);
    }
    return result;
}
inline const Setting *Find(std::wstring_view key) {
    for (const auto &setting : Schema)
        if (setting.key == key)
            return &setting;
    return nullptr;
}
inline bool Valid(const Setting &setting, const std::wstring &value) {
    if (value.size() > 1024 || value.find_first_of(L"\r\n") != value.npos || value.find(L'\0') != value.npos)
        return false;
    if (setting.kind == Kind::Boolean)
        return value == L"0" || value == L"1";
    if (setting.kind == Kind::Integer) {
        try {
            size_t end = 0;
            int n = std::stoi(value, &end);
            return end == value.size() && n >= setting.minimum && n <= setting.maximum;
        } catch (...) {
            return false;
        }
    }
    if (setting.kind == Kind::Choice) {
        auto choices = Split(setting.choices);
        return std::find(choices.begin(), choices.end(), value) != choices.end();
    }
    if (std::wstring_view(setting.key) == L"Accent")
        return value.size() == 6 && value.find_first_not_of(L"0123456789abcdefABCDEF") == value.npos;
    if (std::wstring_view(setting.key) == L"Tabs") {
        auto tabs = Split(value);
        const auto allowed = Split(L"Controls;Today;Stats;Clipboard;Apps;Wallpaper;Power");
        if (tabs.empty() || tabs.size() > 7)
            return false;
        for (const auto &tab : tabs)
            if (std::find(allowed.begin(), allowed.end(), tab) == allowed.end() ||
                std::count(tabs.begin(), tabs.end(), tab) != 1)
                return false;
    }
    return true;
}
struct Values {
    std::map<std::wstring, std::wstring> data;
    Values() {
        for (const auto &s : Schema)
            data[s.key] = s.value;
    }
    explicit Values(const std::map<std::wstring, std::wstring> &saved) : Values() {
        for (const auto &[key, value] : saved)
            if (auto s = Find(key); s && Valid(*s, value))
                data[key] = value;
    }
    std::wstring Text(const wchar_t *key) const {
        auto i = data.find(key);
        return i == data.end() ? L"" : i->second;
    }
    int Int(const wchar_t *key) const {
        try {
            return std::stoi(Text(key));
        } catch (...) {
            return 0;
        }
    }
    bool Bool(const wchar_t *key) const { return Text(key) == L"1"; }
};
} // namespace v4
