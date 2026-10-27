#pragma once
#include "TodayNetwork.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace calendar {
using Seconds = std::chrono::sys_seconds;
using Days = std::chrono::sys_days;
struct Event {
    Seconds start{};
    std::wstring title;
    bool allDay = false;
};
struct Result {
    std::vector<Event> events;
    unsigned skipped = 0;
};
inline std::wstring Unescape(const std::wstring& text) {
    std::wstring value;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) {
            wchar_t next = text[++i]; value += next == L'n' || next == L'N' ? L' ' : next;
        } else value += text[i];
    }
    return value;
}
inline bool Date(const std::wstring& text, Seconds& out) {
    if (text.size() != 8 && text.size() != 15 && text.size() != 16) return false;
    auto number = [&](size_t at, size_t n) {
        int value = 0;
        for (size_t i = at; i < at + n; ++i) {
            if (text[i] < L'0' || text[i] > L'9') return -1;
            value = value * 10 + text[i] - L'0';
        }
        return value;
    };
    std::chrono::year_month_day day{std::chrono::year(number(0, 4)),
        std::chrono::month(static_cast<unsigned>(number(4, 2))),
        std::chrono::day(static_cast<unsigned>(number(6, 2)))};
    if (!day.ok()) return false;
    int h = 0, m = 0, s = 0;
    if (text.size() > 8) {
        if (text[8] != L'T' || (text.size() == 16 && text.back() != L'Z')) return false;
        h = number(9, 2); m = number(11, 2); s = number(13, 2);
        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return false;
    }
    out = Days(day) + std::chrono::hours(h) + std::chrono::minutes(m) + std::chrono::seconds(s);
    return true;
}
inline SYSTEMTIME SystemTime(Seconds value) {
    auto day = std::chrono::floor<std::chrono::days>(value);
    std::chrono::year_month_day date(day);
    std::chrono::hh_mm_ss clock(value - day);
    SYSTEMTIME time{}; time.wYear = static_cast<WORD>(int(date.year()));
    time.wMonth = static_cast<WORD>(unsigned(date.month())); time.wDay = static_cast<WORD>(unsigned(date.day()));
    time.wHour = static_cast<WORD>(clock.hours().count()); time.wMinute = static_cast<WORD>(clock.minutes().count());
    time.wSecond = static_cast<WORD>(clock.seconds().count());
    return time;
}
inline Seconds FromSystemTime(SYSTEMTIME time) {
    return Days(std::chrono::year(time.wYear) / time.wMonth / time.wDay) + std::chrono::hours(time.wHour) +
        std::chrono::minutes(time.wMinute) + std::chrono::seconds(time.wSecond);
}
inline Seconds LocalNow() { SYSTEMTIME time{}; GetLocalTime(&time); return FromSystemTime(time); }
inline bool Local(Seconds input, const std::wstring& zone, Seconds& output) {
    if (zone.empty()) { output = input; return true; }
    auto time = SystemTime(input); SYSTEMTIME utc{}, local{};
    if (zone == L"UTC") utc = time;
    else {
        static const std::map<std::wstring, std::wstring> names{
            {L"America/Chicago", L"Central Standard Time"}, {L"America/New_York", L"Eastern Standard Time"},
            {L"America/Denver", L"Mountain Standard Time"}, {L"America/Los_Angeles", L"Pacific Standard Time"},
            {L"Europe/London", L"GMT Standard Time"}, {L"Europe/Paris", L"Romance Standard Time"},
            {L"Asia/Kolkata", L"India Standard Time"}, {L"Asia/Tokyo", L"Tokyo Standard Time"},
            {L"Australia/Sydney", L"AUS Eastern Standard Time"}};
        auto found = names.find(zone); std::wstring key = found == names.end() ? zone : found->second;
        bool converted = false;
        for (DWORD i = 0; i < 256; ++i) {
            DYNAMIC_TIME_ZONE_INFORMATION tz{};
            if (EnumDynamicTimeZoneInformation(i, &tz) != ERROR_SUCCESS) break;
            if (key == tz.TimeZoneKeyName) {
                converted = TzSpecificLocalTimeToSystemTimeEx(&tz, &time, &utc) != FALSE; break;
            }
        }
        if (!converted) return false;
    }
    if (!SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) return false;
    output = FromSystemTime(local); return true;
}
inline Result Parse(const std::wstring& ics, Seconds from, Seconds through) {
    Result result;
    if (ics.size() > 2 * 1024 * 1024 || through < from) return result;
    std::vector<std::wstring> lines;
    std::wistringstream stream(ics); std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty() && (line[0] == L' ' || line[0] == L'\t') && !lines.empty()) lines.back() += line.substr(1);
        else lines.push_back(line);
    }
    std::map<std::wstring, std::wstring> fields;
    std::wstring startParams, excluded;
    bool active = false;
    auto emit = [&] {
        if (fields.count(L"RECURRENCE-ID")) { ++result.skipped; return; }
        Seconds start;
        if (!Date(fields[L"DTSTART"], start) || fields[L"SUMMARY"].empty() || fields[L"STATUS"] == L"CANCELLED")
            return;
        std::wstring zone;
        if (fields[L"DTSTART"].back() == L'Z') zone = L"UTC";
        else if (auto at = startParams.find(L"TZID="); at != std::wstring::npos) {
            zone = startParams.substr(at + 5); zone = zone.substr(0, zone.find(L';'));
            if (zone.size() >= 2 && zone.front() == L'"') zone = zone.substr(1, zone.size() - 2);
        }
        std::map<std::wstring, std::wstring> rule;
        std::wistringstream rules(fields[L"RRULE"]); std::wstring part;
        while (std::getline(rules, part, L';')) {
            auto equal = part.find(L'='); if (equal != std::wstring::npos) rule[part.substr(0, equal)] = part.substr(equal + 1);
        }
        auto frequency = rule[L"FREQ"];
        if (!frequency.empty() && frequency != L"DAILY" && frequency != L"WEEKLY" && frequency != L"MONTHLY") {
            ++result.skipped; return;
        }
        for (const auto& [name, value] : rule) {
            (void)value;
            if (name != L"FREQ" && name != L"COUNT" && name != L"UNTIL" && name != L"INTERVAL" &&
                name != L"BYDAY" && name != L"BYMONTHDAY" && name != L"WKST") { ++result.skipped; return; }
        }
        int count = 100000, interval = 1;
        try {
            if (rule.count(L"COUNT")) count = std::stoi(rule[L"COUNT"]);
            if (rule.count(L"INTERVAL")) interval = std::stoi(rule[L"INTERVAL"]);
        } catch (...) { ++result.skipped; return; }
        if (interval < 1 || count < 1) { ++result.skipped; return; }
        Seconds until = through + std::chrono::days(2);
        if (rule.count(L"UNTIL") && !Date(rule[L"UNTIL"], until)) { ++result.skipped; return; }
        bool untilUtc = !rule[L"UNTIL"].empty() && rule[L"UNTIL"].back() == L'Z';
        Seconds untilLocal{};
        if (untilUtc && !Local(until, L"UTC", untilLocal)) { ++result.skipped; return; }
        std::set<Seconds> exceptions;
        std::wistringstream ex(excluded);
        while (std::getline(ex, part, L',')) { Seconds value; if (Date(part, value)) exceptions.insert(value); }
        auto firstDay = std::chrono::floor<std::chrono::days>(start);
        auto time = start - firstDay;
        auto limit = std::min(through + std::chrono::days(2), until +
            (untilUtc ? std::chrono::days(2) : std::chrono::days(0)));
        std::chrono::year_month_day original(firstDay);
        int generated = 0;
        // Daily candidate scan is bounded to 100 years and at most 4096 visible instances.
        for (int offset = 0; offset < 36600 && result.events.size() < 4096; ++offset) {
            auto day = firstDay + std::chrono::days(offset); auto candidate = day + time;
            if (candidate > limit || generated >= count) break;
            std::chrono::year_month_day date(day);
            int weekday = static_cast<int>(std::chrono::weekday(day).c_encoding());
            bool matches = offset == 0;
            if (frequency == L"DAILY") matches = offset % interval == 0;
            if (frequency == L"WEEKLY") {
                int initial = static_cast<int>(std::chrono::weekday(firstDay).c_encoding());
                constexpr const wchar_t* names[]{L"SU", L"MO", L"TU", L"WE", L"TH", L"FR", L"SA"};
                int weekStart = 1;
                for (int i = 0; i < 7; ++i) if (rule[L"WKST"] == names[i]) weekStart = i;
                matches = ((offset + (initial - weekStart + 7) % 7) / 7) % interval == 0;
                if (rule[L"BYDAY"].empty()) matches = matches && weekday == initial;
            }
            if (frequency == L"MONTHLY") {
                int months = (int(date.year()) - int(original.year())) * 12 +
                    static_cast<int>(unsigned(date.month())) - static_cast<int>(unsigned(original.month()));
                matches = months % interval == 0;
                if (rule[L"BYDAY"].empty() && rule[L"BYMONTHDAY"].empty()) matches &= date.day() == original.day();
            }
            if (!rule[L"BYMONTHDAY"].empty()) {
                bool found = false; std::wistringstream days(rule[L"BYMONTHDAY"]);
                while (std::getline(days, part, L',')) {
                    try {
                        int wanted = std::stoi(part);
                        auto last = std::chrono::year_month_day_last(date.year(),
                            std::chrono::month_day_last(date.month()));
                        if (wanted < 0) wanted += static_cast<int>(unsigned(last.day())) + 1;
                        found |= wanted == static_cast<int>(unsigned(date.day()));
                    } catch (...) {}
                }
                matches &= found;
            }
            if (!rule[L"BYDAY"].empty()) {
                constexpr const wchar_t* codes[]{L"SU", L"MO", L"TU", L"WE", L"TH", L"FR", L"SA"};
                bool found = false; std::wistringstream days(rule[L"BYDAY"]);
                while (std::getline(days, part, L',')) {
                    if (part.size() < 2 || part.substr(part.size() - 2) != codes[weekday]) continue;
                    int ordinal = 0;
                    try { if (part.size() > 2) ordinal = std::stoi(part.substr(0, part.size() - 2)); } catch (...) {}
                    int dom = static_cast<int>(unsigned(date.day()));
                    auto last = std::chrono::year_month_day_last(date.year(),
                        std::chrono::month_day_last(date.month()));
                    int backward = -(static_cast<int>(unsigned(last.day())) - dom) / 7 - 1;
                    found |= ordinal == 0 || ordinal == (dom - 1) / 7 + 1 || ordinal == backward;
                }
                matches &= found;
            }
            // DTSTART is always the first occurrence of the recurrence set.
            if (offset == 0) matches = true;
            if (frequency.empty() && offset != 0) break;
            if (!matches) continue;
            ++generated;
            if (exceptions.count(candidate)) continue;
            if(candidate<from-std::chrono::days(2))continue;
            Seconds local;
            if (!Local(candidate, zone, local)) { ++result.skipped; return; }
            if (untilUtc && local > untilLocal) break;
            if (local >= from && local < through) result.events.push_back({local,
                Unescape(fields[L"SUMMARY"]), fields[L"DTSTART"].size() == 8});
            if (frequency.empty()) break;
        }
    };
    for (const auto& value : lines) {
        if (value == L"BEGIN:VEVENT") { fields.clear(); excluded.clear(); startParams.clear(); active = true; }
        else if (value == L"END:VEVENT" && active) { emit(); active = false; }
        else if (active) {
            auto colon = value.find(L':'); if (colon == std::wstring::npos) continue;
            auto header = value.substr(0, colon), name = header.substr(0, header.find(L';'));
            if (name == L"DTSTART") startParams = header;
            if (name == L"EXDATE") { if (!excluded.empty()) excluded += L','; excluded += value.substr(colon + 1); }
            fields[name] = value.substr(colon + 1);
        }
    }
    std::sort(result.events.begin(), result.events.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
    return result;
}
}

struct CalendarState {
    bool available = false;
    std::wstring content, message;
};
class CalendarService {
    today::Fetcher<CalendarState> fetcher_;
    std::wstring lastUrl_;
    ULONGLONG lastRefresh_ = 0;
public:
    void Invalidate() { lastRefresh_ = 0; lastUrl_.clear(); }
    CalendarState Snapshot() { return fetcher_.Snapshot(); }
    void Refresh(HWND window, const std::wstring& url) {
        if (url.empty() || fetcher_.Busy()) return;
        ULONGLONG interval = fetcher_.Available() ? 30 * 60 * 1000 : 60000;
        if (url == lastUrl_ && GetTickCount64() - lastRefresh_ < interval) return;
        lastUrl_ = url; lastRefresh_ = GetTickCount64();
        fetcher_.Request(window, [url](const std::atomic<bool>& stop) {
            CalendarState result; result.content = today::Get(url, stop);
            result.message = result.content.find(L"BEGIN:VCALENDAR") == std::wstring::npos ?
                L"Subscription unavailable. Local events are still shown." : L"Private calendar subscription";
            if (result.content.find(L"BEGIN:VCALENDAR") == std::wstring::npos) result.content.clear();
            result.available = !result.content.empty();
            return result;
        });
    }
};
