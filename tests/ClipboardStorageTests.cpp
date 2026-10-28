#include "../src/ModuleRegistry.h"
#include "../src/ClipboardVault.h"
#include "../src/ClipboardModule.h"
#include <cstdio>
#include <stdexcept>

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Predicate> bool Until(modules::Registry &registry, Predicate predicate) {
    ULONGLONG end = GetTickCount64() + 4000;
    while (GetTickCount64() < end) {
        registry.Poll();
        if (predicate())
            return true;
        Sleep(5);
    }
    return false;
}
void Prepare(modules::Registry &registry, const std::filesystem::path &root, const SettingsStore &store) {
    registry.Mode(true, false);
    registry.StorageFixture(root.wstring());
    modules::RegisterBuiltins(registry);
    Require(registry.Load(store), "load isolated storage fixture");
    registry.Grant(L"clipboard");
    Require(registry.Enable(L"clipboard"), "start synthetic source without clipboard listeners or hotkeys");
    Require(registry.Capabilities().Count(L"clipboard") <= 1, "only the optional file lease can be active");
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    try {
        auto root = std::filesystem::absolute(argv[1]) / L"isolated-storage";
        std::filesystem::create_directories(root);
        SettingsStore store((root / L"settings.ini").wstring());
        auto vault = root / L"clipboard-history.dpapi";
        Require(clipboard::EraseVault(vault), "reset only the generated fixture vault");
        Require(store.Update(L"Modules", {{L"Configured", L"1"}}) &&
                    store.Update(L"Module.clipboard", {{L"Persist", L"0"}}),
                "reset isolated module settings");
        modules::Registry first;
        Prepare(first, root, store);
        Require(!std::filesystem::exists(vault), "memory-only default writes no history file");
        for (const auto &action : first.Actions())
            if (action.id.starts_with(L"clipboard.pin-")) {
                Require(first.Invoke(action.id, nullptr), "pin synthetic history item");
                break;
            }
        Require(!first.Invoke(L"clipboard.persist-on", nullptr), "opt-in needs confirmation");
        Require(first.Invoke(L"clipboard.persist-on", nullptr, L"", true), "explicit encryption opt-in");
        Require(Until(first, [&] { return std::filesystem::exists(vault); }),
                "synthetic records are encrypted asynchronously");
        auto saved = clipboard::LoadVault(vault);
        Require(saved && saved->size() == 2 && store.Get(L"Module.clipboard")[L"Persist"] == L"1",
                "configuration and encrypted snapshot agree");
        Require(std::any_of(saved->begin(), saved->end(), [](const auto &item) { return item.item.pinned; }),
                "pin state is included in encrypted persistence");
        first.Stop();
        Require(std::filesystem::exists(vault) && modules::ClipboardModule::Empty(),
                "ordinary app shutdown retains opted-in history and releases every native resource");
        modules::Registry restored;
        Prepare(restored, root, store);
        Require(restored.View(L"clipboard", L"Fixture clipboard text")
                    ->rows.front()
                    .action.starts_with(L"clipboard.preview-"),
                "next instance restores the encrypted synthetic records");
        Require(restored.Invoke(L"clipboard.fixture-lock", nullptr), "simulate session lock for synthetic data only");
        Require(Until(restored,
                      [&] {
                          return !std::filesystem::exists(vault) &&
                                 restored.View(L"clipboard", L"Fixture clipboard text")->rows.front().action.empty();
                      }),
                "lock clears memory, pins and encrypted storage");
        Require(restored.Disable(L"clipboard") && modules::ClipboardModule::Empty(), "locked module unloads cleanly");
        Require(store.Update(L"Module.clipboard", {{L"Persist", L"0"}}), "reset persistence for removal test");
        modules::Registry removed;
        Prepare(removed, root, store);
        Require(removed.Invoke(L"clipboard.persist-on", nullptr, L"", true) &&
                    Until(removed, [&] { return std::filesystem::exists(vault); }),
                "create a new synthetic snapshot");
        Require(removed.Disable(L"clipboard") && !std::filesystem::exists(vault),
                "feature removal erases its saved history");
        Require(store.Update(L"Module.clipboard", {{L"Persist", L"0"}}), "reset for shutdown policy tests");
        modules::Registry stoppingOff;
        Prepare(stoppingOff, root, store);
        Require(stoppingOff.Invoke(L"clipboard.persist-on", nullptr, L"", true) &&
                    Until(stoppingOff, [&] { return std::filesystem::exists(vault); }),
                "create snapshot before immediate off/shutdown");
        Require(stoppingOff.Invoke(L"clipboard.persist-off", nullptr, L"", true), "request restrictive off policy");
        stoppingOff.Stop();
        Require(!std::filesystem::exists(vault), "shutdown honors off even when its worker message is still queued");
        modules::Registry stoppingClear;
        Prepare(stoppingClear, root, store);
        Require(stoppingClear.Invoke(L"clipboard.persist-on", nullptr, L"", true) &&
                    Until(stoppingClear, [&] { return std::filesystem::exists(vault); }),
                "create snapshot before immediate clear/shutdown");
        Require(stoppingClear.Invoke(L"clipboard.clear", nullptr), "request clear before shutdown");
        stoppingClear.Stop();
        Require(!std::filesystem::exists(vault), "shutdown cannot save a snapshot after clear was requested");
        modules::Registry denied;
        Require(store.Update(L"Module.clipboard", {{L"Persist", L"0"}}), "reset persistence for IO denial test");
        Prepare(denied, root, store);
        Require(denied.Invoke(L"clipboard.persist-on", nullptr, L"", true) &&
                    Until(denied, [&] { return std::filesystem::exists(vault); }),
                "create snapshot for deliberate erasure denial");
        HANDLE lock = CreateFileW(vault.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(lock != INVALID_HANDLE_VALUE, "lock only the generated encrypted file");
        Require(denied.Invoke(L"clipboard.clear", nullptr), "request history clearing during a file lock");
        bool restricted = Until(denied, [&] { return store.Get(L"Module.clipboard")[L"Persist"] == L"0"; });
        CloseHandle(lock);
        Require(restricted && denied.View(L"clipboard", L"Fixture clipboard text")->rows.front().action.empty(),
                "erasure failure clears memory and persists a restrictive off policy");
        Require(denied.Invoke(L"clipboard.clear", nullptr) &&
                    Until(denied, [&] { return !std::filesystem::exists(vault); }),
                "explicit clear retries erasure after the lock is released");
        denied.Stop();
        Require(modules::ClipboardModule::Empty(), "all storage fixture workers/windows/timers close");
        std::puts("PASS synthetic-only encrypted history opt-in, restart, lock, removal and IO-failure policy; no real "
                  "clipboard IO");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
