#include "ModuleRegistry.h"
#include <algorithm>
#include <stdexcept>

namespace modules {
static uint64_t ThreadCpu();
bool Setting::Valid(const std::wstring &value) const {
    if (value.size() > limit || value.find_first_of(L"\r\n") != std::wstring::npos ||
        value.find(L'\0') != std::wstring::npos)
        return false;
    if (type == SettingType::Boolean)
        return value == L"0" || value == L"1";
    if (type == SettingType::Integer) {
        try {
            size_t end = 0;
            auto number = std::stoll(value, &end);
            return end == value.size() && number >= minimum && number <= maximum;
        } catch (...) {
            return false;
        }
    }
    return true;
}
uint64_t Ledger::Acquire(const Descriptor &owner, const std::wstring &capability) {
    if (std::find(owner.needs.begin(), owner.needs.end(), capability) == owner.needs.end())
        throw std::logic_error("undeclared module capability");
    auto token = privacy::Policy().Reserve();
    active_.push_back({owner.id, capability, token});
    privacy::Policy().Watch(L"module-capability", static_cast<uintptr_t>(token), owner.id, capability);
    return token;
}
void Ledger::Release(uint64_t token) noexcept {
    privacy::Policy().Unwatch(L"module-capability", static_cast<uintptr_t>(token));
    std::erase_if(active_, [=](const Usage &item) { return item.token == token; });
}
size_t Ledger::Count(const std::wstring &module) const {
    return static_cast<size_t>(
        std::count_if(active_.begin(), active_.end(), [&](const Usage &item) { return item.module == module; }));
}
Registry::Entry *Registry::Find(const std::wstring &id) {
    for (auto &entry : entries_)
        if (entry.descriptor.id == id)
            return &entry;
    return nullptr;
}
Registry::~Registry() { Stop(); }
bool Registry::Add(Descriptor descriptor, Factory factory) {
    if (descriptor.id.empty() || !factory || Find(descriptor.id))
        return false;
    for (const auto &entry : entries_)
        if (entry.live || entry.starting)
            return false;
    if (descriptor.id.size() > 64 ||
        descriptor.id.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-") != std::wstring::npos)
        return false;
    for (auto surface : descriptor.surfaces)
        if (surface != Surface::Palette && surface != Surface::Island)
            return false;
    for (const auto &setting : descriptor.settings) {
        if (setting.key.empty() || !setting.label || !setting.Valid(setting.initial))
            return false;
        if (std::count_if(descriptor.settings.begin(), descriptor.settings.end(),
                          [&](const Setting &other) { return other.key == setting.key; }) != 1)
            return false;
        descriptor.defaults[setting.key] = setting.initial;
    }
    for (const auto &action : descriptor.actions) {
        if (!action.id.starts_with(descriptor.id + L".") || !action.label)
            return false;
        for (const auto &entry : entries_)
            for (const auto &existing : entry.descriptor.actions)
                if (existing.id == action.id)
                    return false;
        if (std::count_if(descriptor.actions.begin(), descriptor.actions.end(),
                          [&](const Action &other) { return other.id == action.id; }) != 1)
            return false;
    }
    auto defaults = descriptor.defaults;
    entries_.push_back({std::move(descriptor), std::move(factory), nullptr, std::move(defaults), 0});
    return true;
}
bool Registry::Enable(const std::wstring &id) {
    auto *entry = Find(id);
    if (!entry)
        return false;
    if (NeedsConsent(id))
        return false;
    if (entry->live)
        return true;
    if (entry->starting)
        return false;
    entry->starting = true;
    const auto transition = entry->transition;
    std::unique_ptr<Module> instance;
    try {
        instance = entry->factory();
        if (instance && instance->Start(ledger_, entry->descriptor, entry->settings) &&
            transition == entry->transition) {
            entry->live = std::move(instance);
            entry->starting = false;
            return true;
        }
    } catch (...) {
    }
    if (instance)
        instance->Stop();
    entry->starting = false;
    return false;
}
bool Registry::NeedsConsent(const std::wstring &id) const {
    for (const auto &entry : entries_)
        if (entry.descriptor.id == id)
            return !entry.descriptor.needs.empty() && !entry.consented;
    return false;
}
bool Registry::Grant(const std::wstring &id) {
    auto *entry = Find(id);
    if (!entry)
        return false;
    entry->consented = true;
    return true;
}
bool Registry::Disable(const std::wstring &id) noexcept {
    auto *entry = Find(id);
    if (!entry)
        return false;
    ++entry->transition;
    auto active = std::exchange(entry->live, {});
    if (active) {
        active->Stop();
        try {
            Persist(*entry, active, true);
        } catch (...) {
        }
        if (ledger_.Owner() && !ledger_.Fixture())
            PostMessageW(ledger_.Owner(), Changed, 0, 0);
    }
    if (requestedView_ == id)
        requestedView_.clear();
    entry->actionCpu100ns = 0;
    return ledger_.Count(id) == 0;
}
void Registry::Stop() noexcept {
    bool previous = ledger_.ShuttingDown();
    ledger_.ShuttingDown(true);
    for (auto at = entries_.rbegin(); at != entries_.rend(); ++at)
        Disable(at->descriptor.id);
    ledger_.ShuttingDown(previous);
}
bool Registry::Enabled(const std::wstring &id) const {
    for (const auto &entry : entries_)
        if (entry.descriptor.id == id)
            return entry.live != nullptr;
    return false;
}
std::vector<const Descriptor *> Registry::Descriptors() const {
    std::vector<const Descriptor *> result;
    for (const auto &entry : entries_)
        result.push_back(&entry.descriptor);
    return result;
}
std::vector<Action> Registry::Actions() const {
    std::vector<Action> result;
    for (const auto &entry : entries_)
        if (entry.live) {
            const auto before = ThreadCpu();
            result.insert(result.end(), entry.descriptor.actions.begin(), entry.descriptor.actions.end());
            auto dynamic = entry.live->Actions();
            for (auto &action : dynamic) {
                if (result.size() >= 1024)
                    break;
                if (action.id.starts_with(entry.descriptor.id + L".") && (action.label || !action.text.empty()))
                    result.push_back(std::move(action));
            }
            const auto after = ThreadCpu();
            if (after >= before)
                entry.actionCpu100ns += after - before;
        }
    return result;
}
void Registry::Query(const std::wstring &query) {
    for (auto &entry : entries_)
        if (auto active = entry.live) {
            const auto before = ThreadCpu();
            active->Query(query);
            const auto after = ThreadCpu();
            if (entry.live == active && after >= before)
                entry.actionCpu100ns += after - before;
        }
}
void Registry::Poll() {
    for (auto &entry : entries_)
        if (auto active = entry.live) {
            active->Poll();
            if (entry.live == active)
                Persist(entry, active);
        }
}
void Registry::Visible(const std::wstring &id) {
    for (auto &entry : entries_)
        if (auto active = entry.live)
            active->Visible(entry.descriptor.id == id);
}
bool Registry::Drag(const std::wstring &id, HWND owner) {
    auto action = FindAction(id);
    if (!action || action->confirmation || action->input)
        return false;
    for (auto &entry : entries_)
        if (entry.live && id.starts_with(entry.descriptor.id + L".")) {
            auto active = entry.live;
            const auto before = ThreadCpu();
            bool result = false;
            try {
                result = active->Drag(id, owner);
            } catch (...) {
            }
            const auto after = ThreadCpu();
            if (entry.live == active && after >= before)
                entry.actionCpu100ns += after - before;
            return result;
        }
    return false;
}
bool Registry::Persist(Entry &entry, const std::shared_ptr<Module> &active, bool stopped) {
    if (stopped ? entry.live != nullptr : entry.live != active)
        return false;
    auto pending = active->PendingSettings();
    if (!pending)
        return true;
    auto settings = entry.settings;
    for (const auto &[key, value] : *pending)
        settings[key] = value;
    bool valid = true;
    for (const auto &setting : entry.descriptor.settings)
        valid = setting.Valid(settings[setting.key]) && valid;
    bool saved = valid && ((ledger_.Fixture() && !ledger_.FixtureStorageIo()) || ledger_.Preview() ||
                           (!ledger_.StorePath().empty() &&
                            SettingsStore(ledger_.StorePath()).Update(L"Module." + entry.descriptor.id, settings)));
    active->SettingsSaved(saved);
    if (stopped ? entry.live != nullptr : entry.live != active)
        return false;
    if (saved || stopped)
        entry.settings = std::move(settings);
    return saved;
}
static uint64_t ThreadCpu() {
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exit, &kernel, &user))
        return 0;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}
std::optional<Action> Registry::FindAction(const std::wstring &id) const {
    for (auto &action : Actions())
        if (action.id == id)
            return action;
    return {};
}
std::optional<Panel> Registry::View(const std::wstring &id, const std::wstring &query) const {
    for (const auto &entry : entries_)
        if (entry.descriptor.id == id && entry.live &&
            std::find(entry.descriptor.surfaces.begin(), entry.descriptor.surfaces.end(), Surface::Island) !=
                entry.descriptor.surfaces.end()) {
            auto active = entry.live;
            const auto before = ThreadCpu();
            active->ViewQuery(query);
            auto panel = active->BuildPanel(query);
            const auto after = ThreadCpu();
            if (entry.live != active)
                return {};
            if (after >= before)
                entry.actionCpu100ns += after - before;
            return panel;
        }
    return {};
}
bool Registry::Invoke(const std::wstring &id, HWND owner, const std::wstring &input, bool confirmed) {
    auto action = FindAction(id);
    if (!action || (action->confirmation && !confirmed) || input.size() > 4096)
        return false;
    for (auto &entry : entries_) {
        if (!entry.live || !id.starts_with(entry.descriptor.id + L"."))
            continue;
        auto active = entry.live;
        const auto before = ThreadCpu();
        bool ok = false;
        try {
            ok = active->InvokeWithText(id, owner, input);
        } catch (...) {
        }
        if (entry.live != active)
            return false;
        if (ok)
            ok = Persist(entry, active);
        const auto after = ThreadCpu();
        if (after >= before)
            entry.actionCpu100ns += after - before;
        if (ok && action->opensView)
            requestedView_ = entry.descriptor.id;
        return ok;
    }
    return false;
}
Cost Registry::Sample(const std::wstring &id) {
    for (auto &entry : entries_)
        if (entry.descriptor.id == id && entry.live) {
            auto cpu = entry.actionCpu100ns + entry.live->BackgroundCpu100ns();
            auto now = GetTickCount64();
            SYSTEM_INFO machine{};
            GetSystemInfo(&machine);
            double percent = entry.sampleTime && now > entry.sampleTime && cpu >= entry.sampleCpu
                                 ? 100.0 * (cpu - entry.sampleCpu) / 10000.0 / (now - entry.sampleTime) /
                                       std::max(1UL, machine.dwNumberOfProcessors)
                                 : -1;
            entry.sampleCpu = cpu;
            entry.sampleTime = now;
            return {entry.live->OwnedBytes(), cpu, percent, entry.live->HookMicroseconds()};
        }
    return {};
}
void Registry::ResetSamples() {
    for (auto &entry : entries_) {
        entry.sampleTime = 0;
        entry.sampleCpu = 0;
        if (entry.live)
            Sample(entry.descriptor.id);
    }
}
std::vector<std::wstring> Registry::PresetIds(Preset preset) {
    switch (preset) {
    case Preset::Minimal:
        return {};
    case Preset::Taskbar:
        return {L"windows"};
    case Preset::PowerUser:
        return {L"windows", L"clipboard", L"snippets", L"shelf", L"keep-awake"};
    case Preset::Creator:
        return {L"windows", L"clipboard", L"snippets", L"shelf", L"keep-awake", L"capture", L"color"};
    }
    return {};
}
bool Registry::ApplyPreset(Preset preset) {
    if (static_cast<unsigned>(preset) > static_cast<unsigned>(Preset::Creator))
        return false;
    auto selected = PresetIds(preset);
    std::vector<std::wstring> previous;
    for (const auto &entry : entries_) {
        if (entry.live)
            previous.push_back(entry.descriptor.id);
        if (std::find(selected.begin(), selected.end(), entry.descriptor.id) != selected.end() &&
            NeedsConsent(entry.descriptor.id))
            return false;
    }
    bool ok = true;
    for (auto &entry : entries_) {
        bool enable = std::find(selected.begin(), selected.end(), entry.descriptor.id) != selected.end();
        ok = (enable ? Enable(entry.descriptor.id) : Disable(entry.descriptor.id)) && ok;
        if (!ok)
            break;
    }
    if (!ok) {
        for (auto &entry : entries_) {
            bool restore = std::find(previous.begin(), previous.end(), entry.descriptor.id) != previous.end();
            if (restore)
                Enable(entry.descriptor.id);
            else
                Disable(entry.descriptor.id);
        }
        return false;
    }
    preset_ = preset;
    firstRun_ = false;
    return true;
}
static std::wstring ConsentKey(const Descriptor &descriptor) {
    std::wstring result = L"1";
    for (const auto &capability : descriptor.needs) {
        result += L"|" + std::to_wstring(capability.size()) + L":" + capability;
    }
    return result;
}
bool Registry::Load(const SettingsStore &store) {
    ledger_.StorePath(store.Path());
    const auto saved = store.Get(L"Modules");
    firstRun_ = !saved.contains(L"Configured");
    preset_ = Preset::Minimal;
    if (auto at = saved.find(L"Preset");
        at != saved.end() && at->second.size() == 1 && at->second[0] >= L'0' && at->second[0] <= L'3')
        preset_ = static_cast<Preset>(at->second[0] - L'0');
    bool ok = true;
    auto consent = store.Get(L"ModuleConsent");
    for (auto &entry : entries_) {
        entry.consented = consent[entry.descriptor.id] == ConsentKey(entry.descriptor);
        entry.settings = entry.descriptor.defaults;
        for (const auto &[key, value] : store.Get(L"Module." + entry.descriptor.id))
            entry.settings[key] = value;
        for (const auto &setting : entry.descriptor.settings)
            if (!setting.Valid(entry.settings[setting.key]))
                entry.settings[setting.key] = setting.initial;
        auto at = saved.find(entry.descriptor.id);
        if (at != saved.end() && at->second == L"1" && !NeedsConsent(entry.descriptor.id))
            ok = Enable(entry.descriptor.id) && ok;
        else
            ok = Disable(entry.descriptor.id) && ok;
    }
    return ok;
}
bool Registry::Save(const SettingsStore &store) {
    auto saved = store.Get(L"Modules");
    saved[L"Configured"] = L"1";
    saved[L"Preset"] = std::to_wstring(static_cast<int>(preset_));
    auto consent = store.Get(L"ModuleConsent");
    for (const auto &entry : entries_) {
        saved[entry.descriptor.id] = entry.live ? L"1" : L"0";
        if (entry.consented)
            consent[entry.descriptor.id] = ConsentKey(entry.descriptor);
        else
            consent.erase(entry.descriptor.id);
    }
    if (!store.UpdateSections({{L"Modules", std::move(saved)}, {L"ModuleConsent", std::move(consent)}}))
        return false;
    firstRun_ = false;
    return true;
}
} // namespace modules
