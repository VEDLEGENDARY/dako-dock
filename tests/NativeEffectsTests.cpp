#include "../src/DesktopShell.h"
#include <cstdio>
#include <stdexcept>

namespace fs = std::filesystem;
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::wstring Journal() {
    auto path = (fs::current_path() / (L"effects-" + std::to_wstring(GetCurrentProcessId()) + L".ini")).wstring();
    fs::remove(path);
    SettingsStore store(path);
    return store.Update(L"Recovery", {{L"Count", L"0"}}) ? path : L"";
}
void RestoreMock(const std::wstring &path) {
    effects::RestoreEntries(path, [](const effects::Values &entry) {
        auto target = entry.find(L"Target"), value = entry.find(L"Value");
        if (target == entry.end() || value == entry.end())
            return false;
        auto state = entry.find(L"TaskbarState"), bottom = entry.find(L"WorkBottom");
        if (state != entry.end()) {
            APPBARDATA data{sizeof(data)};
            MONITORINFO monitor{sizeof(monitor)};
            if (SHAppBarMessage(ABM_GETSTATE, &data) != std::stoul(state->second) ||
                !GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) ||
                monitor.rcWork.bottom != std::stol(bottom->second))
                return false;
        }
        SettingsStore file(target->second);
        return file.Update(L"Mock", {{L"Value", value->second}});
    });
}
int wmain(int argc, wchar_t **argv) {
    effects::createHook = Journal;
    effects::restoreHook = RestoreMock;
    if (argc > 1 && !wcscmp(argv[1], L"--shell-guard")) {
        std::wstring args;
        for (int i = 2; i < argc; ++i)
            args += std::wstring(argv[i]) + L" ";
        return desktop::Guard(args.c_str());
    }
    try {
        if (argc > 3 && !wcscmp(argv[1], L"--child")) {
            APPBARDATA data{sizeof(data)};
            auto originalState = SHAppBarMessage(ABM_GETSTATE, &data);
            MONITORINFO monitor{sizeof(monitor)};
            Require(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE,
                    "capture original work area");
            HWND owner = CreateWindowExW(0, L"STATIC", L"Dako mock recovery", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr,
                                         nullptr, nullptr);
            Require(owner != nullptr, "message-only recovery owner");
            desktop::Shell shell;
            shell.Initialize(owner);
            Require(shell.Start(true), "mock desktop guard ready");
            {
                effects::Transaction transaction;
                Require(transaction.Remember(L"mock", L"one",
                                             {{L"Target", argv[2]},
                                              {L"Value", L"original"},
                                              {L"TaskbarState", std::to_wstring(originalState)},
                                              {L"WorkBottom", std::to_wstring(monitor.rcWork.bottom)}}),
                        "journal precedes write");
            }
            SettingsStore file(argv[2]);
            Require(file.Update(L"Mock", {{L"Value", L"changed"}}), "mock write");
            HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
            Require(ready != nullptr, "ready event");
            SetEvent(ready);
            CloseHandle(ready);
            if (argc > 4)
                return 0;
            WaitForSingleObject(GetCurrentProcess(), INFINITE);
            return 3;
        }
        auto target = (fs::current_path() / L"effects-mock-target.ini").wstring();
        SettingsStore file(target);
        auto path = Journal();
        effects::Activate(path);
        {
            effects::Transaction transaction;
            Require(transaction.Remember(L"mock", L"one", {{L"Target", target}, {L"Value", L"original"}}),
                    "capture original");
            Require(transaction.Remember(L"mock", L"one", {{L"Value", L"wrong"}}), "repeat capture succeeds");
        }
        auto entries = effects::Entries(path);
        Require(entries.size() == 1 && entries[0].values[L"Value"] == L"original",
                "first original survives repeated changes");
        int restored = 0;
        Require(!effects::RestoreEntries(path,
                                         [&](const auto &) {
                                             ++restored;
                                             return false;
                                         }),
                "failure retained");
        Require(effects::RestoreEntries(path,
                                        [&](const auto &) {
                                            ++restored;
                                            return true;
                                        }),
                "retry succeeds");
        Require(effects::RestoreEntries(path,
                                        [&](const auto &) {
                                            ++restored;
                                            return true;
                                        }) &&
                    restored == 2,
                "restoration is idempotent");
        Require(file.Update(L"Mock", {{L"Value", L"original"}}), "create mock file before failure injection");
        effects::Activate(target + L"/invalid.ini");
        {
            effects::Transaction transaction;
            Require(!transaction.Remember(L"mock", L"one", {}), "failed journal prevents writes");
        }
        effects::Activate(L"");
        fs::remove(path);
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        for (bool normal : {true, false}) {
            Require(file.Update(L"Mock", {{L"Value", L"original"}}), "reset mock");
            auto event = L"Local\\Dako.Effects.Test." + std::to_wstring(GetCurrentProcessId());
            HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, event.c_str());
            ResetEvent(ready);
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --child \"" + target + L"\" " + event +
                                   (normal ? L" normal" : L"");
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            Require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &startup, &child) != FALSE,
                    "mock child starts");
            CloseHandle(child.hThread);
            Require(WaitForSingleObject(ready, 15000) == WAIT_OBJECT_0, "child journals and writes before ready");
            CloseHandle(ready);
            if (!normal)
                Require(TerminateProcess(child.hProcess, 77) != FALSE, "force kill only mock child");
            Require(WaitForSingleObject(child.hProcess, 15000) == WAIT_OBJECT_0, "child exits");
            CloseHandle(child.hProcess);
            ULONGLONG end = GetTickCount64() + 15000;
            while (GetTickCount64() < end && file.Get(L"Mock")[L"Value"] != L"original")
                Sleep(20);
            Require(file.Get(L"Mock")[L"Value"] == L"original",
                    "guard restores original mock data after exit and crash");
        }
        fs::remove(target);
        std::puts("PASS: first-value journal, fail-closed writes, partial retry, idempotence, normal and forced-kill "
                  "guard restoration; mock files only.");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
