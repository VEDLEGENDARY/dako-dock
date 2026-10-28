#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include "SettingsStore.h"
#include "ClipboardModel.h"

namespace clipboard {
struct Image;
}
namespace modules {
struct Budget {
    size_t ramMiB = 1;
    double idleCpuPercent = 0, callbackMicroseconds = 1000;
};
struct Action {
    std::wstring id;
    UINT label = 0;
    std::wstring text;
    bool opensView = false, input = false, confirmation = false;
    UINT help = 0;
    std::wstring details;
};
enum class SettingType { Boolean, Integer, Text };
struct Setting {
    std::wstring key;
    UINT label = 0;
    SettingType type = SettingType::Text;
    std::wstring initial;
    int64_t minimum = 0, maximum = 100;
    size_t limit = 2048;
    bool Valid(const std::wstring &value) const;
};
struct Row {
    std::wstring label, action;
    bool enabled = true;
};
struct Panel {
    UINT title = 0;
    std::vector<Row> rows;
    std::shared_ptr<const clipboard::Image> image;
};
inline constexpr UINT Changed = WM_APP + 71;
enum class Surface { Palette, Island, DockGlyph, Capsule };
struct Descriptor {
    std::wstring id;
    UINT name = 0, summary = 0, category = 0;
    std::vector<std::wstring> needs;
    Budget budget;
    std::vector<Action> actions;
    SettingsStore::Section defaults;
    std::vector<Setting> settings;
    std::vector<Surface> surfaces{Surface::Palette};
};
inline size_t DescriptorAllocations(const Descriptor &descriptor) {
    size_t bytes = descriptor.id.capacity() * sizeof(wchar_t) + descriptor.needs.capacity() * sizeof(std::wstring) +
                   descriptor.actions.capacity() * sizeof(Action) + descriptor.settings.capacity() * sizeof(Setting) +
                   descriptor.surfaces.capacity() * sizeof(Surface);
    for (const auto &need : descriptor.needs)
        bytes += need.capacity() * sizeof(wchar_t);
    for (const auto &action : descriptor.actions)
        bytes += (action.id.capacity() + action.text.capacity() + action.details.capacity()) * sizeof(wchar_t);
    for (const auto &setting : descriptor.settings)
        bytes += (setting.key.capacity() + setting.initial.capacity()) * sizeof(wchar_t);
    for (const auto &[key, value] : descriptor.defaults)
        bytes += sizeof(SettingsStore::Section::value_type) + (key.capacity() + value.capacity()) * sizeof(wchar_t);
    return bytes;
}
struct Usage {
    std::wstring module, capability;
    uint64_t token = 0;
};
// Registry and ledger belong to the UI thread; workers marshal usage changes to that owner.
class Ledger {
    std::vector<Usage> active_;
    HWND owner_ = nullptr;
    bool fixture_ = false, preview_ = false, diagnostic_ = false;
    std::wstring storePath_;
    std::wstring storageFixtureRoot_;
    bool shuttingDown_ = false;

  public:
    void Mode(bool fixture, bool preview) {
        fixture_ = fixture;
        preview_ = preview;
    }
    bool Fixture() const { return fixture_; }
    bool Preview() const { return preview_; }
    void Diagnostic(bool value) { diagnostic_ = value; }
    bool Diagnostic() const { return diagnostic_; }
    void StorePath(std::wstring value) { storePath_ = std::move(value); }
    const std::wstring &StorePath() const { return storePath_; }
    void StorageFixture(std::wstring root) { storageFixtureRoot_ = std::move(root); }
    const std::wstring &StorageFixtureRoot() const { return storageFixtureRoot_; }
    bool FixtureStorageIo() const {
        return fixture_ && !storageFixtureRoot_.empty() && !storePath_.empty() &&
               std::filesystem::absolute(storePath_).parent_path().lexically_normal() ==
                   std::filesystem::absolute(storageFixtureRoot_).lexically_normal();
    }
    void ShuttingDown(bool value) { shuttingDown_ = value; }
    bool ShuttingDown() const { return shuttingDown_; }
    void Owner(HWND window) { owner_ = window; }
    HWND Owner() const { return owner_; }
    uint64_t Acquire(const Descriptor &owner, const std::wstring &capability);
    void Release(uint64_t token) noexcept;
    size_t Count(const std::wstring &module) const;
    const std::vector<Usage> &Active() const { return active_; }
};
struct Cost {
    size_t ownedBytes = 0;
    uint64_t actionCpu100ns = 0;
    double cpuPercent = -1;
    double hookMicroseconds = 0;
};
class Module {
  public:
    virtual ~Module() = default;
    virtual bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) = 0;
    virtual void Stop() noexcept = 0;
    virtual void Poll() {}
    virtual std::optional<SettingsStore::Section> PendingSettings() const { return {}; }
    virtual void SettingsSaved(bool) {}
    virtual bool Invoke(const std::wstring &, HWND) = 0;
    virtual size_t OwnedBytes() const noexcept = 0;
    virtual uint64_t BackgroundCpu100ns() const noexcept { return 0; }
    virtual double HookMicroseconds() const noexcept { return 0; }
    virtual std::vector<Action> Actions() const { return {}; }
    virtual void Query(const std::wstring &) {}
    virtual void ViewQuery(const std::wstring &) {}
    virtual void Visible(bool) {}
    virtual bool Drag(const std::wstring &, HWND) { return false; }
    virtual std::vector<clipboard::Item> HistoryRecords() const { return {}; }
    virtual Panel BuildPanel(const std::wstring &) const { return {}; }
    virtual bool InvokeWithText(const std::wstring &action, HWND owner, const std::wstring &input) {
        return input.empty() && Invoke(action, owner);
    }
};
using Factory = std::function<std::unique_ptr<Module>()>;
enum class Preset { Minimal, Taskbar, PowerUser, Creator };
class Registry {
    struct Entry {
        Descriptor descriptor;
        Factory factory;
        std::shared_ptr<Module> live;
        SettingsStore::Section settings;
        mutable uint64_t actionCpu100ns = 0;
        uint64_t sampleCpu = 0, sampleTime = 0;
        bool consented = false;
        bool starting = false;
        uint64_t transition = 0;
    };
    Ledger ledger_;
    std::vector<Entry> entries_;
    bool firstRun_ = true;
    Preset preset_ = Preset::Minimal;
    std::wstring requestedView_;
    Entry *Find(const std::wstring &id);
    bool Persist(Entry &entry, const std::shared_ptr<Module> &active, bool stopped = false);

  public:
    void Owner(HWND window) { ledger_.Owner(window); }
    void Mode(bool fixture, bool preview) { ledger_.Mode(fixture, preview); }
    void Diagnostic(bool value) { ledger_.Diagnostic(value); }
    void StorageFixture(std::wstring root) { ledger_.StorageFixture(std::move(root)); }
    Registry() = default;
    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;
    ~Registry();
    bool Add(Descriptor descriptor, Factory factory);
    bool Enable(const std::wstring &id);
    bool NeedsConsent(const std::wstring &id) const;
    bool Grant(const std::wstring &id);
    bool Disable(const std::wstring &id) noexcept;
    void Stop() noexcept;
    bool Enabled(const std::wstring &id) const;
    std::vector<clipboard::Item> ClipboardRecords() const {
        for (const auto &entry : entries_)
            if (entry.descriptor.id == L"clipboard" && entry.live)
                return entry.live->HistoryRecords();
        return {};
    }
    std::vector<const Descriptor *> Descriptors() const;
    std::vector<Action> Actions() const;
    void Query(const std::wstring &query);
    void Poll();
    void Visible(const std::wstring &);
    bool Drag(const std::wstring &, HWND);
    bool Invoke(const std::wstring &id, HWND owner, const std::wstring &input = {}, bool confirmed = false);
    std::optional<Action> FindAction(const std::wstring &id) const;
    std::optional<Panel> View(const std::wstring &id, const std::wstring &query) const;
    std::wstring TakeView() { return std::exchange(requestedView_, {}); }
    Cost Sample(const std::wstring &id);
    void ResetSamples();
    const Ledger &Capabilities() const { return ledger_; }
    bool ApplyPreset(Preset preset);
    static std::vector<std::wstring> PresetIds(Preset preset);
    bool Load(const SettingsStore &store);
    bool Save(const SettingsStore &store);
    bool FirstRun() const { return firstRun_; }
    Preset SelectedPreset() const { return preset_; }
};
void RegisterBuiltins(Registry &registry);
// Diagnostic uses a thread-local message hook; it never observes global input.
bool Check(const std::wstring &reportPath);
bool Measure(const std::wstring &reportPath, unsigned seconds);
} // namespace modules
