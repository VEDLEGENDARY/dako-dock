#include "../src/KeepAwakeModule.h"
#include "../src/ModuleStrings.h"
#include <cstdio>
#include <stdexcept>
#include <filesystem>

void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
bool WaitState(awake::Worker &worker, awake::State state) {
    ULONGLONG end = GetTickCount64() + 3000;
    while (GetTickCount64() < end) {
        if (worker.Status() == state) return true;
        if (worker.Status() == awake::State::Failed) return false;
        Sleep(1);
    }
    return false;
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--event-source") {
        HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, argv[2]);
        if (!stop) return 2;
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window) NotifyWinEvent(EVENT_OBJECT_SHOW, window, OBJID_WINDOW, CHILDID_SELF);
        WaitForSingleObject(stop, 5000);
        if (window) DestroyWindow(window);
        CloseHandle(stop); return window ? 0 : 3;
    }
    try {
        using awake::Fact;
        auto names = awake::ParseApps(L" Editor.EXE; player.exe; editor.exe ");
        Require(names && names->size() == 2 && awake::ParseApps(L"")->empty(), "bounded exact app names deduplicate");
        for (auto bad : {L"*.exe", L"C:\\app.exe", L"../app.exe", L"cmd.exe /c", L"app.exe;", L";app.exe", L"app.dll"})
            Require(!awake::ParseApps(bad), "paths, arguments and malformed executable names rejected");
        Require(!awake::ParseApps(std::wstring(L"a.exe\0b.exe", 11)), "embedded NUL rejected");
        awake::Rules rules; rules.external = true; rules.apps = {L"DakoRuleFixture.exe"};
        for (auto ac : {Fact::Unknown, Fact::No, Fact::Yes})
            for (auto external : {Fact::Unknown, Fact::No, Fact::Yes})
                for (auto app : {Fact::Unknown, Fact::No, Fact::Yes})
                    Require(awake::Match(rules, ac, external, app) ==
                        (ac == Fact::Yes && external == Fact::Yes && app == Fact::Yes), "all chosen criteria required; unknown fails closed");
        awake::Rules none; none.ac = false;
        Require(!awake::Match(none, Fact::Yes, Fact::Yes, Fact::Yes), "empty conditions cannot keep awake");
        modules::Registry unsaved;
        modules::RegisterKeepAwake(unsaved); unsaved.Grant(L"keep-awake");
        Require(unsaved.Enable(L"keep-awake") && !unsaved.Invoke(L"keep-awake.rule-apps", nullptr, L"unsaved.exe"),
                "missing settings path reports failure without changing active rules");
        auto oldRules = unsaved.View(L"keep-awake", L"");
        Require(oldRules && std::none_of(oldRules->rows.begin(), oldRules->rows.end(), [](const auto &row) {
            return row.label.find(L"unsaved.exe") != std::wstring::npos;
        }) && unsaved.Capabilities().Active().empty(), "failed rule save rolls back and starts no observer");
        awake::Worker monitor;
        for (int warm = 0; warm < 50; ++warm) {
            Require(monitor.Start(rules, nullptr, true) && WaitState(monitor, awake::State::Waiting), "native rule subscriptions warm up without a power request");
            monitor.Stop(); Require(monitor.Resources() == 0, "warm-up subscriptions release owned resources");
        }
        // WTS can initialize shared RPC/thread-pool resources asynchronously after first registration.
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        Sleep(2000);
        DWORD beforeRules = 0, afterRules = 0;
        GetProcessHandleCount(GetCurrentProcess(), &beforeRules);
        DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int cycle = 0; cycle < 50; ++cycle) {
            bool started = monitor.Start(rules, nullptr, true) && WaitState(monitor, awake::State::Waiting);
            if (!started) std::printf("failed_cycle=%d state=%u owned=%u hook_max_us=%.3f\n", cycle,
                static_cast<unsigned>(monitor.Status()), monitor.Resources(), monitor.HookMicroseconds());
            Require(started, "rule worker starts waiting");
            Require(monitor.CheckFacts(Fact::Yes, Fact::Yes, Fact::Yes) && WaitState(monitor, awake::State::Active), "matching rules activate synthetic request");
            Require(monitor.CheckFacts(Fact::No, Fact::Yes, Fact::Yes) && WaitState(monitor, awake::State::Waiting), "AC removal releases synthetic request");
            Require(monitor.CheckFacts(Fact::Yes, Fact::Unknown, Fact::Yes), "unknown display supplied");
            Require(monitor.CheckFacts(Fact::Yes, Fact::Yes, Fact::Yes) && WaitState(monitor, awake::State::Active), "rules reactivate after condition change");
            Require(monitor.CheckFacts(Fact::Yes, Fact::Yes, Fact::No) && WaitState(monitor, awake::State::Waiting), "last selected app exit releases synthetic request");
            monitor.Stop();
            Require(monitor.Resources() == 0 && monitor.Status() == awake::State::Stopped, "all rule native registrations and handles paired on stop");
        }
        GetProcessHandleCount(GetCurrentProcess(), &afterRules);
        std::printf("rule_handles_before=%lu after=%lu user_before=%lu after=%lu owned=%u\n", beforeRules, afterRules,
            user, GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), monitor.Resources());
        Require(beforeRules == afterRules && user == GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), "rule resource count returns exactly to warmed baseline");
        // A real synthetic desktop child proves window-event discovery and process-exit waits.
        // Its short execution request is scoped to this worker; no power preferences change.
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        auto childPath = std::filesystem::path(executable).parent_path() / L"AwakeRuleEventSource.exe";
        awake::Rules childRule; childRule.ac = false; childRule.apps = {L"AwakeRuleEventSource.exe"};
        for (int cycle = 0; cycle < 3; ++cycle) {
            Require(monitor.Start(childRule, nullptr) && WaitState(monitor, awake::State::Waiting), "desktop app watcher starts before process exists");
            std::wstring eventName = L"Local\\Dako.AwakeTest." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
            struct Child {
                HANDLE event = nullptr; PROCESS_INFORMATION process{};
                ~Child() {
                    if (event) SetEvent(event);
                    if (process.hProcess) { WaitForSingleObject(process.hProcess, 6000); CloseHandle(process.hProcess); }
                    if (process.hThread) CloseHandle(process.hThread);
                    if (event) CloseHandle(event);
                }
            } child;
            child.event = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
            std::wstring command = L"\"" + childPath.wstring() + L"\" --event-source \"" + eventName + L"\"";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup);
            Require(child.event && CreateProcessW(childPath.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child.process), "private synthetic event source starts");
            bool activeChild = WaitState(monitor, awake::State::Active);
            std::printf("app_rule_state=%u callbacks=%llu resources=%u hook_max_us=%.3f\n", static_cast<unsigned>(monitor.Status()),
                static_cast<unsigned long long>(monitor.Callbacks()), monitor.Resources(), monitor.HookMicroseconds());
            Require(activeChild && monitor.Callbacks() > 0, "WinEvent discovers selected desktop process");
            SetEvent(child.event);
            Require(WaitForSingleObject(child.process.hProcess, 3000) == WAIT_OBJECT_0 &&
                WaitState(monitor, awake::State::Waiting), "process exit releases rule request without polling");
            monitor.Stop(); Require(monitor.Resources() == 0, "real rule resources close after process exit");
        }
        Require(monitor.HookMicroseconds() <= 1000, "rule callback stays below one millisecond");
        std::printf("PASS 50 rule subscription cycles, conditions, native desktop process start/exit; callback max %.3f us\n", monitor.HookMicroseconds());
        modules::Registry registry;
        modules::RegisterKeepAwake(registry);
        registry.Grant(L"keep-awake");
        Require(registry.Enable(L"keep-awake"), "keep-awake module starts without a request");
        Require(registry.Capabilities().Active().empty(), "enable alone does not prevent sleep");
        Require(registry.Invoke(L"keep-awake.rules-start", nullptr) && registry.Capabilities().Count(L"keep-awake") == 2,
                "rule monitoring capabilities are visible while started");
        Require(!registry.Invoke(L"keep-awake.rule-ac", nullptr), "configuration cannot change underneath an active watcher");
        Require(registry.Invoke(L"keep-awake.stop", nullptr) && registry.Capabilities().Active().empty(),
                "rule monitoring stops immediately without a settings write");
        // Only this test's short-lived worker execution requests are changed. No power preferences are written.
        for (int cycle = 0; cycle < 51; ++cycle) {
            Require(registry.Invoke(cycle % 2 ? L"keep-awake.15m" : L"keep-awake.display-15m", nullptr),
                    "explicit native execution request starts");
            ULONGLONG end = GetTickCount64() + 2000;
            bool active = false;
            while (GetTickCount64() < end && !active) {
                auto panel = registry.View(L"keep-awake", L"");
                active = panel && (panel->rows.front().label == modules::String(IDS_AWAKE_ACTIVE) ||
                    panel->rows.front().label == modules::String(IDS_AWAKE_DISPLAY_ACTIVE));
                if (!active) Sleep(1);
            }
            Require(active && registry.Capabilities().Count(L"keep-awake") == 1, "live request is in the capability ledger");
            Require(registry.Invoke(L"keep-awake.stop", nullptr), "explicit stop joins the owning thread");
            auto stopped = registry.View(L"keep-awake", L"");
            Require(stopped && stopped->rows.front().label == modules::String(IDS_AWAKE_STOPPED) &&
                registry.Capabilities().Active().empty(), "previous worker state restored and lease released");
        }
        DWORD before = 0, after = 0;
        GetProcessHandleCount(GetCurrentProcess(), &before);
        Require(registry.Invoke(L"keep-awake.until-stop", nullptr), "indefinite request starts");
        Require(registry.Disable(L"keep-awake"), "module removal cancels an indefinite request");
        GetProcessHandleCount(GetCurrentProcess(), &after);
        Require(before == after && registry.Capabilities().Active().empty(), "power worker and event handles return to baseline");
        std::puts("PASS 51 native power request/restore cycles, disabled-state cleanup; no power preference changes");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1;
    }
}
