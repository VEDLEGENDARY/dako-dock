#include "../src/SettingsStore.h"
#include "../src/TodayNetwork.h"
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool condition, const char *label) {
    if (!condition)
        throw std::runtime_error(label);
}
void Raw(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Require(static_cast<bool>(file), "fixture file write");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    try {
        auto root = std::filesystem::path(argv[1]) / L"privacy-tests" /
                    (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root);
        auto &policy = privacy::Policy();
        policy.SetOffline(false);
        {
            privacy::Network request(L"fixture.invalid", L"fixture-request");
            Require(request.Allowed() && policy.Read().requests == 1, "online request acquires a lease");
            policy.SetOffline(true);
            Require(!request.Allowed() && policy.Read().requests == 1, "offline exposes draining request");
            policy.SetOffline(false);
            Require(!request.Allowed(), "online cannot revive a request cancelled by an earlier offline toggle");
        }
        Require(policy.Read().requests == 0, "request cleanup drains the policy");
        policy.SetOffline(true);
        std::atomic<bool> stop = false;
        Require(today::Get(L"https://fixture.invalid/private?key=do-not-log", stop).empty(),
                "offline Get returns without a network request");
        auto blocked = policy.Read().events.back();
        Require(blocked.result == privacy::Result::Blocked && blocked.target == L"fixture.invalid" &&
                    blocked.reason == L"calendar-subscription",
                "network ledger records host and reason only");
        policy.SetOffline(false);
        privacy::Runtime concurrent;
        auto writer = [&] {
            for (int i = 0; i < 200; ++i) {
                auto id = concurrent.Begin(L"fixture", L"file-write", L"fixture-path", L"fixture-reason");
                concurrent.Finish(id, true);
            }
        };
        std::thread first(writer), second(writer);
        first.join();
        second.join();
        auto history = concurrent.Read();
        Require(history.events.size() == 128 && history.omitted == 272, "bounded concurrent audit history");
        concurrent.Watch(L"test-hook", 1, L"fixture", L"test");
        Require(concurrent.Read().active.size() == 1, "active resource is visible");
        concurrent.Unwatch(L"test-hook", 1);
        Require(concurrent.Read().active.empty(), "released resource disappears");

        SettingsStore source((root / L"source.ini").wstring());
        Require(source.Update(L"Dako", {{L"DesktopMode", L"1"}, {L"DesktopPromptDone", L"1"}}), "source consent");
        Require(source.Update(L"Today", {{L"CalendarUrl", L"https://fixture.invalid/private?key=do-not-log"}}),
                "source configuration");
        Require(source.Update(L"Module.windows", {{L"value", L"base"}, {L"retained", L"yes"}}), "base settings");
        Require(source.Override(L"Module.windows", L"monitor:@[]日本語", {}, {{L"value", L"monitor"}}),
                "monitor override uses encoded identity");
        Require(source.Override(L"Module.windows", {}, L"app.exe", {{L"value", L"app"}}), "app override");
        Require(source.Override(L"Module.windows", L"monitor:@[]日本語", L"app.exe", {{L"value", L"both"}}),
                "combined override");
        Require(source.Effective(L"Module.windows", L"monitor:@[]日本語")[L"value"] == L"monitor" &&
                    source.Effective(L"Module.windows", {}, L"app.exe")[L"value"] == L"app" &&
                    source.Effective(L"Module.windows", L"monitor:@[]日本語", L"app.exe")[L"value"] == L"both",
                "override precedence is global, monitor, app, combined");
        auto exported = (root / L"settings.dako").wstring();
        Require(source.Export(exported) && !source.Export(source.Path()), "single-file export rejects self overwrite");
        SettingsStore target((root / L"target.ini").wstring());
        Require(target.Update(L"Dako", {{L"DesktopMode", L"0"}, {L"DesktopPromptDone", L"1"}}), "target consent");
        policy.SetOffline(true);
        Require(target.Import(exported), "settings import with backup");
        Require(target.Get(L"Dako")[L"DesktopMode"] == L"0" && target.Get(L"Privacy")[L"Offline"] == L"1",
                "import cannot grant desktop consent or weaken Offline");
        Require(target.Get(L"Today")[L"CalendarUrl"] == source.Get(L"Today")[L"CalendarUrl"] &&
                    target.Effective(L"Module.windows", L"monitor:@[]日本語", L"app.exe")[L"value"] == L"both",
                "export/import preserves private settings and overrides");
        auto invalid = root / L"invalid.dako";
        Raw(invalid, "[Schema]\nVersion=999\n[DakoExport]\nFormat=1\n");
        Require(!target.Import(invalid.wstring()), "future schema import rejected");
        Raw(invalid, "[Schema]\nVersion=3\nVersion=3\n[DakoExport]\nFormat=1\n");
        Require(!target.Import(invalid.wstring()), "ambiguous duplicate key import rejected");
        Raw(invalid, "[Schema]\nVersion=3\n[DakoExport]\nFormat=1\n[Broken\n");
        Require(!target.Import(invalid.wstring()), "malformed import rejected");
        Raw(invalid, std::string(2 * 1024 * 1024 + 1, 'x'));
        Require(!target.Import(invalid.wstring()), "oversized import rejected before allocating its contents");
        Require(target.Get(L"Dako")[L"DesktopMode"] == L"0", "rejected imports preserve committed settings");
        auto corrupt = root / L"corrupt.ini";
        Raw(corrupt, std::string("\xff\xfe\x01", 3));
        Require(!SettingsStore(corrupt.wstring()).Update(L"Test", {{L"value", L"no"}}),
                "unreadable document is never replaced by an empty merge");
        HANDLE busy =
            CreateFileW(target.Path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(busy != INVALID_HANDLE_VALUE, "sharing conflict fixture");
        std::thread unlock([busy] {
            Sleep(20);
            CloseHandle(busy);
        });
        bool committed = target.Update(L"Retry", {{L"value", L"retained"}});
        unlock.join();
        Require(committed, "bounded atomic-rename retry handles a transient sharing conflict");
        for (const auto &event : policy.Read().events) {
            Require(event.target.find(L"do-not-log") == std::wstring::npos &&
                        event.reason.find(L"do-not-log") == std::wstring::npos,
                    "ledger never records setting values");
        }
        policy.SetOffline(false);
        std::puts("PASS offline epochs/drain, host-only ledger, concurrent bounds, atomic import/export and overrides");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
