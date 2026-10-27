#include "../src/ModuleRegistry.h"
#include "../src/ModuleStrings.h"
#include <cstdio>
#include <stdexcept>
#include "../src/SettingsPageModel.h"

namespace {
void Require(bool ok, const char *label) {
    if (!ok) throw std::runtime_error(label);
}
class Failing final : public modules::Module {
    modules::Ledger *ledger_ = nullptr;
    uint64_t token_ = 0;
public:
    bool Start(modules::Ledger &ledger, const modules::Descriptor &descriptor,
               const SettingsStore::Section &settings) override {
        Require(settings.at(L"value") == L"retained", "module settings retained");
        Require(settings.at(L"flag") == L"0", "invalid saved field falls back to the schema default");
        ledger_ = &ledger;
        token_ = ledger.Acquire(descriptor, L"test");
        throw std::runtime_error("partial startup failure");
    }
    void Stop() noexcept override { if (ledger_) ledger_->Release(token_); ledger_ = nullptr; }
    bool Invoke(const std::wstring &, HWND) override { return false; }
    size_t OwnedBytes() const noexcept override { return sizeof(*this); }
};
class Reentrant final : public modules::Module {
    modules::Registry &registry_;
    bool cancelStart_;
    int &stops_, &destroyed_;
public:
    Reentrant(modules::Registry &registry, bool cancelStart, int &stops, int &destroyed) :
        registry_(registry), cancelStart_(cancelStart), stops_(stops), destroyed_(destroyed) {}
    ~Reentrant() override { ++destroyed_; }
    bool Start(modules::Ledger &, const modules::Descriptor &, const SettingsStore::Section &) override {
        if (cancelStart_) registry_.Disable(L"reentrant");
        return true;
    }
    void Stop() noexcept override { ++stops_; }
    bool Invoke(const std::wstring &, HWND) override {
        registry_.Disable(L"reentrant");
        Require(destroyed_ == 0, "in-flight invocation retains its object until it returns");
        return true;
    }
    size_t OwnedBytes() const noexcept override { return sizeof(*this); }
};
class PendingState final : public modules::Module {
    bool &event_;
    std::wstring &started_;
    std::wstring value_ = L"1";
    bool dirty_ = false;
public:
    PendingState(bool &event, std::wstring &started) : event_(event), started_(started) {}
    bool Start(modules::Ledger &, const modules::Descriptor &, const SettingsStore::Section &settings) override {
        started_ = value_ = settings.at(L"Flag"); return true;
    }
    void Poll() override { if (event_) { value_ = L"0"; dirty_ = true; event_ = false; } }
    void Stop() noexcept override { value_ = L"0"; dirty_ = true; }
    bool Invoke(const std::wstring &id, HWND) override { if (id != L"async.reset") return false; value_ = L"1"; dirty_ = true; return true; }
    std::optional<SettingsStore::Section> PendingSettings() const override {
        if (!dirty_) return {}; return SettingsStore::Section{{L"Flag", value_}};
    }
    void SettingsSaved(bool) override { dirty_ = false; }
    size_t OwnedBytes() const noexcept override { return sizeof(*this); }
};
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    try {
        const auto root = std::filesystem::path(argv[1]) / L"module-tests";
        std::filesystem::create_directories(root);
        SettingsStore store((root / L"settings.ini").wstring());
        Require(store.Update(L"Modules", {{L"Configured", L"1"}, {L"future", L"1"}}), "seed settings");
        Require(store.Update(L"Module.fail", {{L"value", L"retained"}, {L"flag", L"invalid"}}), "seed module settings");
        Require(store.Update(L"Dako", {{L"layoutMode", L"notch"}}), "seed dock preference");
        Require(store.Update(L"ModuleConsent", {}), "reset fixture capability consent");
        modules::Registry registry;
        modules::RegisterBuiltins(registry);
        unsigned constructed = 0;
        modules::Descriptor descriptor;
        descriptor.id = L"fail";
        descriptor.needs = {L"test"};
        descriptor.settings = {{L"flag", IDS_FEATURES, modules::SettingType::Boolean, L"0"}};
        Require(registry.Add(descriptor, [&] { ++constructed; return std::make_unique<Failing>(); }), "register");
        Require(registry.Load(store) && constructed == 0, "disabled modules never constructed");
        Require(registry.NeedsConsent(L"fail") && !registry.Enable(L"fail") && constructed == 0,
                "capability consent precedes construction");
        registry.Grant(L"fail");
        Require(!registry.Enable(L"fail") && constructed == 1, "failed startup reported");
        Require(!registry.Enabled(L"fail") && registry.Capabilities().Active().empty(), "partial startup unwound");
        Require(registry.ApplyPreset(modules::Preset::Taskbar) && registry.FindAction(L"windows.display") &&
            registry.FindAction(L"windows.signin") && registry.FindAction(L"windows.page"), "Taskbar preset exposes Settings pages");
        Require(registry.Invoke(L"windows.open", nullptr) && registry.TakeView() == L"windows" &&
                    registry.View(L"windows", L"Display")->rows.size() == 1, "shared island surface request and query");
        Require(registry.Save(store), "save enabled state");
        Require(store.Get(L"Modules")[L"future"] == L"1", "unknown module state retained");
        Require(store.Get(L"Dako")[L"layoutMode"] == L"notch", "preset preserves dock preferences");
        Require(store.Get(L"Module.fail")[L"value"] == L"retained", "module settings survive disable");
        modules::Registry restored;
        modules::RegisterBuiltins(restored);
        Require(restored.Load(store) && restored.Enabled(L"windows") && !restored.FirstRun(), "enabled round trip");
        Require(restored.Disable(L"windows") && restored.Actions().empty(), "actions removed on disable");
        Require(!restored.Invoke(L"windows.display", nullptr), "disabled action cannot execute");
        Require(!restored.View(L"windows", L""), "disabled module surface cannot open");
        Require(restored.ApplyPreset(modules::Preset::Minimal) && restored.Save(store), "Minimal saved");
        Require(store.Get(L"Modules")[L"windows"] == L"0", "disabled round trip");
        Require(!restored.Enable(L"unknown") && !restored.Invoke(L"unknown.action", nullptr), "unknown rejected");
        Require(!modules::String(IDS_FEATURES).empty() && modules::String(IDS_FEATURES) != L"5000", "resources linked");
        modules::Setting boolean{L"enabled", IDS_FEATURES, modules::SettingType::Boolean, L"0"};
        modules::Setting integer{L"minutes", IDS_FEATURES, modules::SettingType::Integer, L"5", 1, 60};
        modules::Setting text{L"title", IDS_FEATURES, modules::SettingType::Text, L"", 0, 0, 10};
        Require(boolean.Valid(L"1") && !boolean.Valid(L"yes") && integer.Valid(L"60") &&
                    !integer.Valid(L"61") && !integer.Valid(L"5bad") && !text.Valid(L"bad\ntext") &&
                    !text.Valid(L"a long fixture title"), "typed setting schemas validate bounds and text");
        for (bool duringStart : {false, true}) {
            int stops = 0, destroyed = 0;
            modules::Registry nested;
            modules::Descriptor reentrant;
            reentrant.id = L"reentrant";
            reentrant.actions = {{L"reentrant.stop", IDS_FEATURES}};
            reentrant.surfaces = {modules::Surface::DockGlyph};
            Require(!nested.Add(reentrant, [] { return std::make_unique<Failing>(); }),
                    "unimplemented surfaces are rejected instead of silently accepted");
            reentrant.surfaces = {modules::Surface::Palette};
            Require(nested.Add(reentrant, [&] {
                return std::make_unique<Reentrant>(nested, duringStart, stops, destroyed);
            }), "register reentrant lifecycle fixture");
            if (duringStart) Require(!nested.Enable(L"reentrant"), "cancelled Start cannot resurrect a module");
            else Require(nested.Enable(L"reentrant") && !nested.Invoke(L"reentrant.stop", nullptr),
                         "reentrant Stop invalidates the action result");
            Require(stops == 1 && destroyed == 1 && nested.Actions().empty(), "reentrant cleanup runs once");
        }
        modules::Registry tools;
        tools.Mode(true, false);
        modules::RegisterBuiltins(tools);
        tools.Grant(L"utilities"); tools.Grant(L"clipboard");
        Require(tools.Enable(L"utilities"), "enable calculator in inert fixture");
        tools.Query(L"2*(3+4)");
        auto dynamic = tools.Actions();
        auto result = std::find_if(dynamic.begin(), dynamic.end(), [](const modules::Action &action) {
            return action.id.starts_with(L"utilities.result-");
        });
        Require(result != dynamic.end() && result->text == L"2*(3+4) = 14", "query feeds calculator action");
        auto stale = result->id;
        Require(tools.Invoke(stale, nullptr) && tools.TakeView() == L"utilities", "result opens shared island");
        Require(!tools.Invoke(L"utilities.copy", nullptr), "fixture cannot write the clipboard");
        tools.Query(L"5 km in m");
        Require(!tools.Invoke(stale, nullptr), "old result generations cannot copy newer private text");
        Require(tools.Invoke(L"utilities.clean-url", nullptr,
            L"https://fixture.invalid/?utm_source=x&id=2"), "manual Clean URL input");
        auto utilityPanel = tools.View(L"utilities", L"");
        Require(utilityPanel && utilityPanel->rows.front().label == L"https://fixture.invalid/?id=2",
                "Clean URL result is visible without clipboard IO");
        Require(tools.Enable(L"clipboard"), "inert clipboard fixture enabled");
        auto fixtureActions = tools.Actions();
        auto clip = std::find_if(fixtureActions.begin(), fixtureActions.end(), [](const modules::Action &action) {
            return action.id.starts_with(L"clipboard.preview-") && action.text.find(L"Fixture clipboard text") != std::wstring::npos;
        });
        Require(clip != fixtureActions.end() && tools.Invoke(clip->id, nullptr), "fixture clipboard search and preview");
        auto preview = tools.View(L"clipboard", L"");
        Require(preview && preview->rows.back().label == L"Fixture clipboard text", "clipboard preview text");
        Require(tools.Invoke(L"clipboard.clear", nullptr) && !tools.Invoke(clip->id, nullptr),
                "clearing history invalidates old item actions");
        Require(!tools.Invoke(L"clipboard.persist-on", nullptr) &&
                    tools.Invoke(L"clipboard.persist-on", nullptr, L"", true),
                "encrypted history opt-in requires confirmation even in an inert fixture");
        Require(tools.View(L"clipboard", L"")->rows.back().action == L"clipboard.persist-off" &&
                    tools.Capabilities().Active().empty(), "fixture changes policy without disk or clipboard IO");
        tools.Stop();
        Require(tools.Capabilities().Active().empty(), "fixtures never acquire OS clipboard resources");
        Require(store.Update(L"Module.snippets", {{L"Entries", L"1"}}), "reset saved snippet fixture");
        modules::Registry snippets;
        modules::RegisterBuiltins(snippets);
        Require(snippets.Load(store) && snippets.Grant(L"snippets") && snippets.Enable(L"snippets"),
                "snippet module uses the isolated settings store");
        Require(snippets.Invoke(L"snippets.add", nullptr, L"Greeting | First line\\nSecond line"),
                "explicit snippet creation commits local settings");
        Require(snippets.Save(store), "save snippet enablement and capability consent");
        Require(store.Get(L"Module.snippets")[L"Entries"].find(L"Greeting") == std::wstring::npos,
                "snippet encoding preserves arbitrary text without INI syntax injection");
        auto saved = snippets.Actions();
        auto edit = std::find_if(saved.begin(), saved.end(), [](const auto &action) { return action.id.starts_with(L"snippets.edit-"); });
        Require(edit != saved.end(), "named snippet action is registered");
        HANDLE locked = CreateFileW(store.Path().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "lock only this test's settings file");
        bool rejectedEdit = !snippets.Invoke(edit->id, nullptr, L"Changed | Should roll back");
        CloseHandle(locked);
        Require(rejectedEdit && snippets.FindAction(edit->id)->text.find(L"Greeting") != std::wstring::npos,
                "failed settings commit rolls back the in-memory snippet edit");
        snippets.Stop();
        modules::Registry loadedSnippets;
        modules::RegisterBuiltins(loadedSnippets);
        Require(loadedSnippets.Load(store) && loadedSnippets.Enable(L"snippets"), "reload persisted snippets");
        Require(loadedSnippets.FindAction(edit->id).has_value(), "snippet IDs survive restart");
        loadedSnippets.Stop();
        SettingsStore corruptSnippets((root / L"corrupt-snippets.ini").wstring());
        Require(corruptSnippets.Update(L"Module.snippets", {{L"Entries", L"corrupt fixture"}}), "seed unreadable snippet data");
        modules::Registry damaged;
        modules::RegisterBuiltins(damaged);
        Require(damaged.Load(corruptSnippets) && damaged.Grant(L"snippets") && damaged.Enable(L"snippets"),
                "unreadable snippets are shown without rewriting them");
        Require(!damaged.Invoke(L"snippets.add", nullptr, L"New | Must not replace unreadable data") &&
                    corruptSnippets.Get(L"Module.snippets")[L"Entries"] == L"corrupt fixture",
                "new snippet cannot silently clobber an unreadable list");
        Require(!damaged.Invoke(L"snippets.reset", nullptr) && damaged.Invoke(L"snippets.reset", nullptr, L"", true) &&
                    corruptSnippets.Get(L"Module.snippets")[L"Entries"] == L"1", "reset requires explicit confirmation");
        modules::Registry awake;
        Require(modules::String(IDS_AWAKE_RULES).find(L'\u2014') != std::wstring::npos &&
            modules::String(IDS_AWAKE_RULE_STARTING).find(L'\u2026') != std::wstring::npos,
            "module resource text decodes as Unicode rather than the system ANSI code page");
        awake.Mode(true, false); modules::RegisterBuiltins(awake); awake.Grant(L"keep-awake");
        Require(awake.Enable(L"keep-awake") && !awake.Invoke(L"keep-awake.15m", nullptr) &&
                    awake.Capabilities().Active().empty(), "preview cannot create a Windows execution request");
        Require(!awake.Invoke(L"keep-awake.rules-start", nullptr) && awake.Capabilities().Active().empty(),
                "fixtures cannot install rule monitoring or power requests");
        Require(awake.Invoke(L"keep-awake.rule-apps", nullptr, L"editor.exe; player.exe") &&
            awake.Invoke(L"keep-awake.rule-external", nullptr), "rule configuration uses module settings transaction");
        auto awakePanel = awake.View(L"keep-awake", L"");
        Require(awakePanel && std::any_of(awakePanel->rows.begin(), awakePanel->rows.end(), [](const auto &row) {
            return row.label.find(L"editor.exe; player.exe") != std::wstring::npos;
        }), "selected executable names remain visible and accessible");
        Require(!awake.Invoke(L"keep-awake.rule-apps", nullptr, L"C:\\unsafe.exe") &&
            !awake.Invoke(L"keep-awake.rules-start", nullptr) && awake.Capabilities().Active().empty(),
            "malformed names and synthetic rules remain inert");
        modules::Registry indexed;
        indexed.Mode(true, false); modules::RegisterBuiltins(indexed); indexed.Grant(L"files");
        Require(indexed.Enable(L"files"), "indexed files fixture enabled");
        indexed.Query(L"files report");
        auto fileActions = indexed.Actions();
        auto foundFile = std::find_if(fileActions.begin(), fileActions.end(), [](const auto &action) {
            return action.id.starts_with(L"files.result-");
        });
        Require(foundFile != fileActions.end() && foundFile->text.find(L"report") != std::wstring::npos &&
                    !indexed.Invoke(foundFile->id, nullptr), "synthetic indexed result cannot launch a real file");
        auto oldFile = foundFile->id;
        indexed.Query(L"files changed");
        Require(!indexed.Invoke(oldFile, nullptr), "old file result generations cannot open a newer target");
        Require(indexed.Disable(L"files") && indexed.Enable(L"files"), "re-enable indexed source");
        indexed.Query(L"files report");
        Require(!indexed.FindAction(oldFile), "file result IDs never repeat across module instances");
        indexed.Query(L"files " + std::wstring(257, L'x'));
        auto oversizedResults = indexed.Actions();
        Require(std::none_of(oversizedResults.begin(), oversizedResults.end(), [](const auto &action) {
            return action.id.starts_with(L"files.result-");
        }), "oversized filename prefixes cancel instead of silently truncating");
        Require(indexed.View(L"files", L"island query")->rows.front().label.find(L"island query") != std::wstring::npos &&
                    indexed.Capabilities().Active().empty(), "shared island search uses synthetic data without an index request");
        indexed.Stop();
        Require(settingspage::Uri(L" SOUND ") == L"ms-settings:sound" &&
            settingspage::Uri(L"MS-SETTINGS:privacy-webcam") == L"ms-settings:privacy-webcam" &&
            settingspage::Uri(L"ms-settings:") == L"ms-settings:", "bounded Settings identifiers normalize without rewriting other schemes");
        Require(!settingspage::Uri(L"https://fixture.invalid") && !settingspage::Uri(L"ms-settings:sound?endpointId=private") &&
            !settingspage::Uri(L"ms-settings:display\n") && !settingspage::Uri(L"display#other") &&
            !settingspage::Uri(std::wstring(97, L'x')) && !settingspage::Uri(L"display%20other"),
            "schemes, unsupported parameters, base-page escapes, control characters and oversized Settings IDs rejected");
        modules::Registry settings;
        settings.Mode(true, false); modules::RegisterBuiltins(settings);
        Require(settings.Enable(L"windows"), "Settings fixture starts without background resources");
        settings.Query(L"ms-settings:privacy-webcam");
        auto settingsActions = settings.Actions();
        auto settingsResult = std::find_if(settingsActions.begin(), settingsActions.end(), [](const auto &action) {
            return action.id.starts_with(L"windows.page-result-");
        });
        Require(settingsResult != settingsActions.end() && settingsResult->confirmation &&
            settingsResult->details == L"ms-settings:privacy-webcam" &&
            !settings.Invoke(settingsResult->id, nullptr) && !settings.Invoke(settingsResult->id, nullptr, L"", true),
            "typed Settings link carries its full URI confirmation and cannot launch in fixtures");
        auto oldSettings = settingsResult->id;
        settings.Query(L"ms-settings:network-wifi");
        Require(!settings.FindAction(oldSettings), "typed Settings link generations reject stale targets");
        Require(settings.Disable(L"windows") && settings.Enable(L"windows"), "Settings source re-enables");
        settings.Query(L"ms-settings:privacy-webcam");
        Require(!settings.FindAction(oldSettings) && settings.Capabilities().Active().empty(),
            "Settings IDs never repeat across instances and install no subscription");
        settings.Query(L"ms-settings:defaultapps?registeredAUMID=Example!App");
        auto parameterActions = settings.Actions();
        auto parameter = std::find_if(parameterActions.begin(), parameterActions.end(), [](const auto &action) {
            return action.id.starts_with(L"windows.page-result-");
        });
        Require(parameter != parameterActions.end() && parameter->confirmation &&
            parameter->details == L"ms-settings:defaultapps?registeredAUMID=Example%21App" &&
            !settings.Invoke(parameter->id, nullptr, L"", true), "parameter targets carry canonical confirmation and remain inert");
        auto oldParameter = parameter->id;
        settings.Query(L"ms-settings:defaultapps?registeredAUMID=Example%21App");
        Require(settings.FindAction(oldParameter).has_value(), "equivalent escapes preserve the same transient target identity");
        settings.Query(L"ms-settings:defaultapps?registeredAUMID=Other%21App");
        Require(!settings.FindAction(oldParameter), "changing a parameter invalidates the old target identity");
        modules::Registry shelf;
        shelf.Mode(true, false); modules::RegisterBuiltins(shelf); shelf.Grant(L"shelf");
        Require(shelf.Enable(L"shelf") && shelf.Invoke(L"shelf.add", nullptr, L"Parked fixture 日本語"), "shelf supports explicit keyboard text input");
        auto shelfActions = shelf.Actions();
        auto parked = std::find_if(shelfActions.begin(), shelfActions.end(), [](const auto &action) {
            return action.id.starts_with(L"shelf.preview-") && action.text.find(L"日本語") != std::wstring::npos;
        });
        Require(parked != shelfActions.end() && shelf.Invoke(parked->id, nullptr) && shelf.TakeView() == L"shelf",
                "parked item opens inside the shared island");
        auto oldShelf = parked->id;
        auto panel = shelf.View(L"shelf", L"");
        Require(panel && panel->rows.back().label == L"Parked fixture 日本語" &&
            !shelf.Invoke(panel->rows.front().action, nullptr) && !shelf.Drag(panel->rows.front().action, nullptr),
            "shelf fixtures cannot write clipboard or begin a real drag");
        shelf.Visible(L"shelf");
        Require(shelf.Capabilities().Active().empty(), "inert shelf owns no native subscription or drop target");
        for (int item = 0; item < 32; ++item)
            Require(shelf.Invoke(L"shelf.add", nullptr, L"Bounded parked fixture " + std::to_wstring(item)), "keyboard shelf input reports its result in the feature");
        auto fullShelf = shelf.View(L"shelf", L"");
        auto fullActions = shelf.Actions();
        Require(fullShelf && fullShelf->rows.front().label == modules::String(IDS_SHELF_FAILED) &&
            shelf.FindAction(oldShelf) && std::count_if(fullActions.begin(), fullActions.end(), [](const auto &action) {
                return action.id.starts_with(L"shelf.preview-");
            }) == 25, "a full shelf reports failure and retains all existing parked records");
        Require(shelf.Invoke(L"shelf.clear", nullptr) && !shelf.Invoke(oldShelf, nullptr), "cleared shelf IDs cannot address a later item");
        Require(shelf.Disable(L"shelf") && shelf.Sample(L"shelf").ownedBytes == 0, "shelf unload releases its model");
        modules::Registry system;
        system.Mode(true, false); modules::RegisterBuiltins(system);
        system.Grant(L"recycle-bin"); system.Grant(L"drives");
        Require(system.Enable(L"recycle-bin") && system.Enable(L"drives"), "system metadata fixtures enable lazily");
        auto emptyBin = system.FindAction(L"recycle-bin.empty");
        Require(emptyBin && emptyBin->confirmation && emptyBin->details.find(L"98304") != std::wstring::npos &&
            !system.Invoke(emptyBin->id, nullptr) && !system.Invoke(emptyBin->id, nullptr, L"", true),
            "emptying requires target confirmation and is rejected before any fixture deletion");
        auto systemActions = system.Actions();
        auto eject = std::find_if(systemActions.begin(), systemActions.end(), [](const auto &action) { return action.id.starts_with(L"drives.eject-"); });
        Require(eject != systemActions.end() && eject->confirmation && eject->details.find(L"Z:\\") != std::wstring::npos &&
            !system.Invoke(eject->id, nullptr) && !system.Invoke(eject->id, nullptr, L"", true),
            "drive removal requires confirmation and fixture paths never reach native ejection");
        auto oldEject = eject->id;
        Require(system.Invoke(L"drives.refresh", nullptr) && !system.FindAction(oldEject), "drive refresh invalidates old target IDs");
        auto currentDrives = system.Actions();
        auto currentEject = std::find_if(currentDrives.begin(), currentDrives.end(), [](const auto &action) { return action.id.starts_with(L"drives.eject-"); });
        Require(currentEject != currentDrives.end(), "synthetic refreshed drive remains available");
        auto previousInstance = currentEject->id;
        Require(system.Disable(L"drives") && system.Enable(L"drives") && !system.FindAction(previousInstance),
            "drive action IDs do not repeat across instances");
        Require(system.Capabilities().Active().empty() && system.Disable(L"recycle-bin") && system.Disable(L"drives") &&
            system.Sample(L"recycle-bin").ownedBytes == 0 && system.Sample(L"drives").ownedBytes == 0,
            "system fixtures perform no native query or mutation and release their resources");
        bool backgroundChange = false;
        std::wstring restartedValue;
        modules::Registry pending;
        modules::Descriptor pendingDescriptor;
        pendingDescriptor.id = L"async";
        pendingDescriptor.settings = {{L"Flag", IDS_FEATURES, modules::SettingType::Boolean, L"1"}};
        pendingDescriptor.actions = {{L"async.reset", IDS_FEATURES}};
        Require(pending.Add(pendingDescriptor, [&] { return std::make_unique<PendingState>(backgroundChange, restartedValue); }),
                "register asynchronous settings fixture");
        SettingsStore pendingStore((root / L"pending-settings.ini").wstring());
        Require(pendingStore.Update(L"Module.async", {{L"Flag", L"1"}}) && pending.Load(pendingStore) &&
                    pending.Enable(L"async"), "enable asynchronous settings fixture");
        backgroundChange = true;
        pending.Poll();
        Require(pendingStore.Get(L"Module.async")[L"Flag"] == L"0", "worker-driven restrictive settings persist on UI polling");
        Require(pending.Invoke(L"async.reset", nullptr) && pendingStore.Get(L"Module.async")[L"Flag"] == L"1",
                "explicit settings actions use the same transaction");
        Require(pending.Disable(L"async") && pendingStore.Get(L"Module.async")[L"Flag"] == L"0" &&
                    pending.Enable(L"async") && restartedValue == L"0", "stop-time settings update the store and next instance");
        Require(modules::Check((root / L"module-check.txt").wstring()), "50-cycle native unload diagnostic");
        std::puts("PASS module lifecycle, startup rollback, presets, settings, action gating, resources and 50 cycles");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
