#include <windows.h>
#include "../src/SettingsStore.h"
#include "../src/CalendarService.h"
#include "../src/WeatherService.h"
#include "../src/WindowList.h"
#include "../src/DockShell.h"
#include "../src/LayoutEngine.h"
#include <cstdio>
#include <stdexcept>

void RequireFoundation(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
calendar::Seconds Date(const wchar_t *text) {
    calendar::Seconds result;
    RequireFoundation(calendar::Date(text, result), "valid date parsed");
    return result;
}
int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    try {
        namespace fs = std::filesystem;
        auto directory = fs::current_path() / L"foundation-fixtures";
        fs::create_directories(directory);
        auto path = (directory / L"settings.ini").wstring();
        for (auto name : {L"settings.ini", L"appearance.ini", L"tasks.ini", L"apps.ini"})
            fs::remove(directory / name);
        auto legacy = [&](const wchar_t *name, const std::wstring &text) {
            std::ofstream out(directory / name, std::ios::binary);
            WORD bom = 0xFEFF;
            out.write(reinterpret_cast<const char *>(&bom), 2);
            out.write(reinterpret_cast<const char *>(text.data()), static_cast<std::streamsize>(text.size() * 2));
        };
        legacy(L"appearance.ini", L"[Dako]\r\nAppearance=2\r\nReducedMotion=1\r\n");
        legacy(L"tasks.ini", L"[Tasks]\r\ntask0=1|日本語 ✨\r\n");
        legacy(L"apps.ini", L"[Favorites]\r\n0=fixture:star\r\n[Recent]\r\n0=fixture:recent\r\n");
        SettingsStore store(path);
        RequireFoundation(store.Get(L"Dako")[L"Appearance"] == L"2" && !fs::exists(path),
                          "legacy read is side-effect free");
        RequireFoundation(store.Migrate() &&
                              store.Get(L"Schema")[L"Version"] == std::to_wstring(SettingsStore::Version),
                          "versioned migration commits");
        RequireFoundation(store.Get(L"Favorites")[L"0"] == L"fixture:star" &&
                              store.Get(L"Recent")[L"0"] == L"fixture:recent",
                          "favorites and recents migrate");
        TaskStore tasks(path);
        auto loaded = tasks.Load();
        RequireFoundation(loaded.size() == 1 && loaded[0].first == L"日本語 ✨" && loaded[0].second,
                          "Unicode tasks migrate");
        std::vector<std::pair<std::wstring, bool>> twenty;
        for (int i = 0; i < 20; ++i)
            twenty.emplace_back(L"Task " + std::to_wstring(i), i % 2 == 0);
        RequireFoundation(tasks.Save(twenty) && tasks.Load() == twenty, "20 tasks round trip");
        RequireFoundation(store.Get(L"Dako")[L"Appearance"] == L"2" && store.Get(L"Favorites")[L"0"] == L"fixture:star",
                          "section saves preserve unrelated settings");
        RequireFoundation(store.Update(L"Today", {{L"CalendarUrl", L"https://fixture.invalid/private?key=secret"}}),
                          "private feed settings round trip");
        RequireFoundation(store.Migrate() && tasks.Load() == twenty, "repeat migration does not overwrite changes");
        RequireFoundation(fs::exists(directory / L"tasks.ini") && fs::exists(directory / L"apps.ini"),
                          "legacy files retained for rollback");
        RequireFoundation(!TaskStore((directory / L"settings.ini" / L"invalid.ini").wstring()).Save(twenty),
                          "failed atomic save is reported");
        RequireFoundation(tasks.Load() == twenty, "failed save preserves the committed document");
        legacy(L"future.ini", L"[Schema]\r\nVersion=999\r\n[Tasks]\r\ntask0=0|Future\r\n");
        SettingsStore future((directory / L"future.ini").wstring());
        RequireFoundation(!future.Update(L"Dako", {{L"Appearance", L"1"}}), "future schema is never overwritten");

        auto from = Date(L"20261001"), through = Date(L"20261201");
        auto parse = [&](const std::wstring &fields) {
            return calendar::Parse(L"BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n" + fields + L"\r\nEND:VEVENT\r\nEND:VCALENDAR",
                                   from, through);
        };
        auto one = parse(L"DTSTART;VALUE=DATE:20261023\r\nSUMMARY:Long\\, folded\r\n title");
        RequireFoundation(one.events.size() == 1 && one.events[0].allDay && one.events[0].title == L"Long, foldedtitle",
                          "all-day, escaped text and folded lines");
        auto daily = parse(L"DTSTART:20261023T090000\r\nSUMMARY:Daily\r\nRRULE:FREQ=DAILY;COUNT=5;INTERVAL=2");
        RequireFoundation(daily.events.size() == 5 && daily.events.back().start == Date(L"20261031T090000"),
                          "daily interval and count");
        auto weekly = parse(L"DTSTART:20261005T090000\r\nSUMMARY:Weekly\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE;COUNT=5");
        RequireFoundation(weekly.events.size() == 5 && weekly.events[1].start == Date(L"20261007T090000"),
                          "weekly weekdays and count");
        auto until = parse(L"DTSTART:20261023T090000\r\nSUMMARY:Until\r\nRRULE:FREQ=DAILY;UNTIL=20261025T090000");
        RequireFoundation(until.events.size() == 3, "inclusive recurrence UNTIL");
        auto monthly = parse(L"DTSTART:20260831T090000\r\nSUMMARY:Monthly\r\nRRULE:FREQ=MONTHLY;COUNT=3");
        RequireFoundation(monthly.events.size() == 1 && monthly.events[0].start == Date(L"20261031T090000"),
                          "monthly skips nonexistent dates without consuming COUNT");
        auto ordinal =
            parse(L"DTSTART:20261005T090000\r\nSUMMARY:First Monday\r\nRRULE:FREQ=MONTHLY;BYDAY=1MO;COUNT=2");
        RequireFoundation(ordinal.events.size() == 2 && ordinal.events.back().start == Date(L"20261102T090000"),
                          "monthly ordinal weekdays");
        auto ex = parse(L"DTSTART:20261023\r\nSUMMARY:Exceptions\r\nRRULE:FREQ=DAILY;COUNT=3\r\nEXDATE:20261024");
        RequireFoundation(ex.events.size() == 2, "EXDATE excludes without extending count");
        RequireFoundation(parse(L"DTSTART:20261023\r\nSUMMARY:Cancelled\r\nSTATUS:CANCELLED").events.empty(),
                          "cancelled event omitted");
        auto unsupported = parse(L"DTSTART:20261023\r\nSUMMARY:Yearly\r\nRRULE:FREQ=YEARLY");
        RequireFoundation(unsupported.events.empty() && unsupported.skipped == 1, "unsupported recurrence reported");
        calendar::Seconds bad;
        RequireFoundation(!calendar::Date(L"20260230", bad) && !calendar::Date(L"20261023T250000", bad),
                          "invalid dates and times rejected");
        RequireFoundation(calendar::Parse(L"BEGIN:VEVENT\nSUMMARY:no date\nEND:VEVENT", from, through).events.empty(),
                          "malformed event is inert");
        auto utc = parse(L"DTSTART:20261023T120000Z\r\nSUMMARY:UTC");
        RequireFoundation(utc.events.size() == 1, "UTC event converted to local display time");
        auto zonedUntil = parse(L"DTSTART;TZID=Asia/Tokyo:20261023T090000\r\nSUMMARY:Tokyo\r\n"
                                L"RRULE:FREQ=DAILY;UNTIL=20261025T000000Z");
        RequireFoundation(zonedUntil.events.size() == 3, "TZID recurrence compares UNTIL in UTC correctly");

        auto weather =
            WeatherService::ParseForecast(LR"({"current":{"temperature_2m":18.5,"weather_code":2},)"
                                          LR"("daily":{"temperature_2m_max":[21],"temperature_2m_min":[12]}})",
                                          L"Offline fixture city");
        RequireFoundation(weather.available && weather.temperature == 18.5 && weather.high == 21 && weather.low == 12 &&
                              WeatherService::Condition(weather.code) == L"Cloudy",
                          "Open-Meteo fixture parses without network");
        bool rejected = false;
        try {
            WeatherService::ParseForecast(L"{\"current\":null}", L"Fixture");
        } catch (...) {
            rejected = true;
        }
        RequireFoundation(rejected, "missing or malformed forecast is rejected");
        RequireFoundation(today::Escape(L"New York") == L"New%20York", "city query is URL encoded");

        RequireFoundation(desktop::EligibleWindow(true, false, false, 0), "normal window eligible");
        RequireFoundation(!desktop::EligibleWindow(false, false, false, 0), "hidden window excluded");
        RequireFoundation(!desktop::EligibleWindow(true, true, false, 0), "cloaked window excluded");
        RequireFoundation(!desktop::EligibleWindow(true, false, true, 0), "owned tool excluded");
        RequireFoundation(desktop::EligibleWindow(true, false, true, WS_EX_APPWINDOW), "owned APPWINDOW eligible");
        RequireFoundation(!desktop::EligibleWindow(true, false, false, WS_EX_TOOLWINDOW), "tool window excluded");
        for (UINT dpi : {96U, 120U, 144U, 168U, 192U}) {
            RECT monitor{-1920, -200, 0, 880};
            auto bar = desktop::BottomDockRect(monitor, dpi);
            RequireFoundation(bar.left == monitor.left && bar.right == monitor.right && bar.bottom == monitor.bottom &&
                                  bar.bottom - bar.top == desktop::DipPixels(72, dpi),
                              "bar geometry uses monitor origin and DPI");
        }
        auto firstMonitor = reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(1));
        auto secondMonitor = reinterpret_cast<HMONITOR>(static_cast<UINT_PTR>(2));
        desktop::PinEntry pin;
        pin.key = L"shell:AppsFolder\\Example.Package!App";
        pin.name = L"Pinned package";
        desktop::PinEntry editor;
        editor.key = L"C:\\Tools\\Editor.exe";
        editor.name = L"Editor";
        std::vector<desktop::WindowEntry> windows(4);
        windows[0].appId = L"Example.Package!App";
        windows[0].identity = desktop::AppIdentity(windows[0].appId);
        windows[0].title = L"Package A";
        windows[0].monitor = firstMonitor;
        windows[0].active = true;
        windows[1] = windows[0];
        windows[1].monitor = secondMonitor;
        windows[1].active = false;
        windows[2].executable = L"c:\\tools\\EDITOR.EXE";
        windows[2].identity = desktop::AppIdentity(windows[2].executable);
        windows[2].title = L"Editor document";
        windows[2].monitor = firstMonitor;
        windows[3].identity = L"unmatched.exe";
        windows[3].title = L"Unpinned app";
        windows[3].monitor = firstMonitor;
        auto groups = desktop::BuildDockGroups({pin, editor}, windows, firstMonitor);
        RequireFoundation(groups.size() == 3 && groups[0].pinned && groups[0].active && groups[0].windows.size() == 1 &&
                              groups[1].windows.size() == 1 && !groups[2].pinned,
                          "pins group by AUMID/path with monitor filtering");
        auto alias = pin;
        alias.key = L"second alias";
        alias.appId = windows[0].appId;
        auto dedup = desktop::BuildDockGroups({pin, alias, editor}, windows, firstMonitor);
        RequireFoundation(dedup.size() == 3 && dedup[0].pinKeys.size() == 2 && dedup[1].pinIndex == 2,
                          "duplicate pinned AUMIDs merge without losing reorder indices");
        RequireFoundation(desktop::NormalizeLayout(L"bar") == L"dock" && desktop::NormalizeLayout(L"notch") == L"notch",
                          "old bar preference migrates to dock");
        layout::Rect capsule{100, 8, 500, 64};
        RequireFoundation(desktop::CapsuleContains(capsule, 300, 36) && !desktop::CapsuleContains(capsule, 100, 8) &&
                              !desktop::CapsuleContains(capsule, 99, 36),
                          "pill excludes rounded corners and full-width margins");
        auto secondary = desktop::BuildDockGroups({pin, editor}, windows, secondMonitor);
        RequireFoundation(secondary.size() == 2 && secondary[0].windows.size() == 1 && secondary[1].windows.empty(),
                          "each monitor retains pins and only its own running windows");
        RequireFoundation(desktop::AppIdentity(L"::{4234}\\Example.Package!App") == desktop::AppIdentity(pin.key),
                          "AppsFolder identities normalize consistently");
        RequireFoundation(desktop::HideForNotificationState(QUNS_BUSY) &&
                              desktop::HideForNotificationState(QUNS_RUNNING_D3D_FULL_SCREEN) &&
                              desktop::HideForNotificationState(QUNS_PRESENTATION_MODE) &&
                              !desktop::HideForNotificationState(QUNS_ACCEPTS_NOTIFICATIONS),
                          "fullscreen notification states hide bars");
        for (float physical : {1366.0f, 1920.0f, 3840.0f})
            for (float dpi : {96.0f, 120.0f, 144.0f, 168.0f, 192.0f})
                for (float text : {1.0f, 1.25f, 1.5f, 2.0f, 2.25f}) {
                    float width = physical / (dpi / 96 * text);
                    std::vector<desktop::DockGroup> crowded(150, groups.front());
                    auto bar = desktop::SolveDock(width, crowded, {});
                    for (size_t i = 0; i < bar.hits.size(); ++i) {
                        const auto &hit = bar.hits[i];
                        RequireFoundation(hit.rect.left >= 0 && hit.rect.right <= width && hit.rect.Height() > 0 &&
                                              !hit.name.empty(),
                                          "bar actions stay named and inside monitor bounds at every scale");
                        for (size_t j = i + 1; j < bar.hits.size(); ++j)
                            RequireFoundation(!layout::Intersects(hit.rect, bar.hits[j].rect),
                                              "bar hit targets never overlap");
                    }
                    RequireFoundation(bar.visibleGroups < crowded.size(),
                                      "bounded bar exposes overflow instead of overlap");
                }
        for (float height : {300.0f, 560.0f, 748.0f}) {
            float expected = layout::Solve({800, height, layout::View::Controls, 20}).panel.Height();
            for (auto view : {layout::View::Today, layout::View::Apps, layout::View::Stats, layout::View::Wallpaper})
                RequireFoundation(layout::Solve({800, height, view, 20}).panel.Height() == expected,
                                  "tab changes preserve fixed island height");
        }
        std::puts("PASS: versioned settings migration/atomic merge, 20 tasks, ICS recurrence/time zones, window "
                  "filter, bar DPI math and fixed tab height.");
        winrt::uninit_apartment();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
