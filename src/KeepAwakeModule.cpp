#include "KeepAwakeModule.h"
#include "ModuleStrings.h"

namespace modules {
bool KeepAwakeModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &settings) {
    ledger_ = &ledger;
    descriptor_ = descriptor;
    inert_ = ledger.Fixture() || ledger.Preview() || ledger.Diagnostic();
    status_ = IDS_AWAKE_STOPPED;
    auto get = [&](const wchar_t *key, const wchar_t *fallback) {
        auto at = settings.find(key);
        return at == settings.end() ? std::wstring(fallback) : at->second;
    };
    rules_.ac = get(L"RuleAC", L"1") == L"1";
    rules_.external = get(L"RuleExternal", L"0") == L"1";
    rules_.display = get(L"RuleDisplay", L"0") == L"1";
    auto apps = awake::ParseApps(get(L"RuleApps", L""));
    if (apps)
        rules_.apps = std::move(*apps);
    else {
        rules_.ac = rules_.external = false;
        settingStatus_ = IDS_AWAKE_RULE_INVALID;
    }
    return true;
}
void KeepAwakeModule::Release() noexcept {
    ruleWorker_.Stop();
    if (ledger_)
        for (auto token : ruleTokens_)
            ledger_->Release(token);
    std::vector<uint64_t>().swap(ruleTokens_);
    if (stop_)
        SetEvent(stop_);
    if (thread_) {
        WaitForSingleObject(thread_, INFINITE);
        releasedCpu_ = BackgroundCpu100ns() - ruleWorker_.Cpu();
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_)
        CloseHandle(stop_);
    stop_ = nullptr;
    if (token_ && ledger_)
        ledger_->Release(token_);
    token_ = 0;
    deadline_ = 0;
}
void KeepAwakeModule::Stop() noexcept {
    Release();
    ledger_ = nullptr;
}
DWORD WINAPI KeepAwakeModule::Run(void *value) {
    SetThreadDescription(GetCurrentThread(), L"Dako.Module.KeepAwake");
    auto *self = static_cast<KeepAwakeModule *>(value);
    privacy::Write request(L"keep-awake", L"power-state", L"current worker execution request", L"explicit-keep-awake");
    EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
    if (self->display_)
        flags |= ES_DISPLAY_REQUIRED;
    EXECUTION_STATE previous = SetThreadExecutionState(flags);
    request.Complete(previous != 0);
    if (previous) {
        self->status_ = self->display_ ? IDS_AWAKE_DISPLAY_ACTIVE : IDS_AWAKE_ACTIVE;
        if (self->ledger_->Owner())
            PostMessageW(self->ledger_->Owner(), Changed, 0, 0);
        WaitForSingleObject(self->stop_, self->duration_);
        privacy::Write restore(L"keep-awake", L"power-state", L"current worker execution request",
                               L"restore-worker-state");
        bool restored = SetThreadExecutionState(ES_CONTINUOUS | previous) != 0;
        restore.Complete(restored);
        self->status_ = restored ? IDS_AWAKE_STOPPED : IDS_AWAKE_FAILED;
    } else
        self->status_ = IDS_AWAKE_FAILED;
    if (self->ledger_->Owner())
        PostMessageW(self->ledger_->Owner(), Changed, 0, 0);
    return 0;
}
void KeepAwakeModule::Poll() {
    if (thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0)
        Release();
    if (ruleWorker_.Finished()) {
        Release();
        status_ = IDS_AWAKE_FAILED;
    }
}
bool KeepAwakeModule::InvokeWithText(const std::wstring &id, HWND owner, const std::wstring &input) {
    if (id == L"keep-awake.rule-apps") {
        if (!ledger_ || thread_ || ruleWorker_.Running() || dirty_)
            return false;
        auto apps = awake::ParseApps(input);
        if (!apps) {
            settingStatus_ = IDS_AWAKE_RULE_INVALID;
            return false;
        }
        pending_ = rules_;
        pending_.apps = std::move(*apps);
        dirty_ = true;
        return true;
    }
    return input.empty() && Invoke(id, owner);
}
bool KeepAwakeModule::Invoke(const std::wstring &id, HWND) {
    Poll();
    if (!ledger_)
        return false;
    if (id == L"keep-awake.open")
        return true;
    if (id == L"keep-awake.stop") {
        bool hadRule = ruleWorker_.Running();
        Release();
        if (hadRule && ruleWorker_.Status() == awake::State::Failed)
            status_ = IDS_AWAKE_FAILED;
        else if (status_ != IDS_AWAKE_FAILED)
            status_ = IDS_AWAKE_STOPPED;
        return status_ != IDS_AWAKE_FAILED;
    }
    if (id == L"keep-awake.rule-ac" || id == L"keep-awake.rule-external" || id == L"keep-awake.rule-display") {
        if (thread_ || ruleWorker_.Running() || dirty_)
            return false;
        pending_ = rules_;
        if (id == L"keep-awake.rule-ac")
            pending_.ac = !pending_.ac;
        if (id == L"keep-awake.rule-external")
            pending_.external = !pending_.external;
        if (id == L"keep-awake.rule-display")
            pending_.display = !pending_.display;
        dirty_ = true;
        return true;
    }
    if (id == L"keep-awake.rules-start") {
        if (inert_ || dirty_ || !rules_.HasCondition() || settingStatus_ == IDS_AWAKE_RULE_INVALID)
            return false;
        Release();
        ruleTokens_.push_back(ledger_->Acquire(descriptor_, L"power-state"));
        ruleTokens_.push_back(ledger_->Acquire(descriptor_, L"session-events"));
        if (!rules_.apps.empty()) {
            ruleTokens_.push_back(ledger_->Acquire(descriptor_, L"process-query"));
            ruleTokens_.push_back(ledger_->Acquire(descriptor_, L"window-event-hook"));
        }
        if (!ruleWorker_.Start(rules_, ledger_->Owner())) {
            Release();
            status_ = IDS_AWAKE_FAILED;
            return false;
        }
        settingStatus_ = 0;
        status_ = IDS_AWAKE_RULE_STARTING;
        return true;
    }
    DWORD duration = id == L"keep-awake.15m" || id == L"keep-awake.display-15m" ? 15 * 60 * 1000
                     : id == L"keep-awake.1h"                                   ? 60 * 60 * 1000
                     : id == L"keep-awake.until-stop"                           ? INFINITE
                                                                                : 0;
    if (!duration || inert_)
        return false;
    Release();
    duration_ = duration;
    display_ = id == L"keep-awake.display-15m";
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_)
        return false;
    token_ = ledger_->Acquire(descriptor_, L"power-state");
    deadline_ = duration == INFINITE ? UINT64_MAX : GetTickCount64() + duration;
    thread_ = CreateThread(nullptr, 0, Run, this, 0, nullptr);
    if (!thread_) {
        Release();
        return false;
    }
    return true;
}
Panel KeepAwakeModule::BuildPanel(const std::wstring &) const {
    UINT status = status_.load();
    if (ruleWorker_.Running()) {
        switch (ruleWorker_.Status()) {
        case awake::State::Active:
            status = rules_.display ? IDS_AWAKE_RULE_DISPLAY_ACTIVE : IDS_AWAKE_RULE_ACTIVE;
            break;
        case awake::State::Waiting:
            status = IDS_AWAKE_RULE_WAITING;
            break;
        case awake::State::Failed:
            status = IDS_AWAKE_FAILED;
            break;
        default:
            status = IDS_AWAKE_RULE_STARTING;
            break;
        }
    }
    bool editing = !thread_ && !ruleWorker_.Running() && !dirty_;
    auto choice = [&](UINT label, bool value) {
        return String(label) + L": " + String(value ? IDS_AWAKE_RULE_ON : IDS_AWAKE_RULE_OFF);
    };
    Panel panel{IDS_AWAKE,
                {{String(status), L""},
                 {String(IDS_AWAKE_15), L"keep-awake.15m", !inert_},
                 {String(IDS_AWAKE_60), L"keep-awake.1h", !inert_},
                 {String(IDS_AWAKE_DISPLAY), L"keep-awake.display-15m", !inert_},
                 {String(IDS_AWAKE_UNTIL_STOP), L"keep-awake.until-stop", !inert_},
                 {String(IDS_AWAKE_STOP), L"keep-awake.stop", thread_ != nullptr || ruleWorker_.Running()},
                 {String(IDS_AWAKE_RULES), L""},
                 {choice(IDS_AWAKE_RULE_AC, rules_.ac), L"keep-awake.rule-ac", editing},
                 {choice(IDS_AWAKE_RULE_EXTERNAL, rules_.external), L"keep-awake.rule-external", editing},
                 {choice(IDS_AWAKE_RULE_DISPLAY, rules_.display), L"keep-awake.rule-display", editing},
                 {String(IDS_AWAKE_RULE_APPS) + L": " +
                      (rules_.apps.empty() ? String(IDS_AWAKE_RULE_ANY) : awake::EncodeApps(rules_.apps)),
                  L"keep-awake.rule-apps", editing},
                 {String(IDS_AWAKE_RULE_START), L"keep-awake.rules-start", !inert_ && rules_.HasCondition() && !dirty_},
                 {String(IDS_AWAKE_RULE_SCOPE), L""},
                 {String(IDS_AWAKE_SCOPE), L""}}};
    if (settingStatus_)
        panel.rows.push_back({String(settingStatus_), L""});
    if (deadline_ && deadline_ != UINT64_MAX) {
        uint64_t now = GetTickCount64();
        panel.rows.push_back({String(IDS_AWAKE_MINUTES) + L" " +
                                  std::to_wstring(deadline_ > now ? (deadline_ - now + 59999) / 60000 : 0),
                              L""});
    }
    return panel;
}
uint64_t KeepAwakeModule::BackgroundCpu100ns() const noexcept {
    FILETIME c{}, e{}, k{}, u{};
    if (!thread_ || !GetThreadTimes(thread_, &c, &e, &k, &u))
        return releasedCpu_ + ruleWorker_.Cpu();
    ULARGE_INTEGER kernel{}, user{};
    kernel.LowPart = k.dwLowDateTime;
    kernel.HighPart = k.dwHighDateTime;
    user.LowPart = u.dwLowDateTime;
    user.HighPart = u.dwHighDateTime;
    return releasedCpu_ + kernel.QuadPart + user.QuadPart + ruleWorker_.Cpu();
}
size_t KeepAwakeModule::OwnedBytes() const noexcept {
    size_t bytes = sizeof(*this) + DescriptorAllocations(descriptor_) + ruleWorker_.Bytes() +
                   ruleTokens_.capacity() * sizeof(uint64_t);
    for (auto *list : {&rules_.apps, &pending_.apps}) {
        bytes += list->capacity() * sizeof(std::wstring);
        for (const auto &app : *list)
            bytes += app.capacity() * sizeof(wchar_t);
    }
    return bytes;
}
std::optional<SettingsStore::Section> KeepAwakeModule::PendingSettings() const {
    if (!dirty_)
        return {};
    return SettingsStore::Section{{L"RuleAC", pending_.ac ? L"1" : L"0"},
                                  {L"RuleExternal", pending_.external ? L"1" : L"0"},
                                  {L"RuleDisplay", pending_.display ? L"1" : L"0"},
                                  {L"RuleApps", awake::EncodeApps(pending_.apps)}};
}
void KeepAwakeModule::SettingsSaved(bool saved) {
    if (saved)
        rules_ = std::move(pending_);
    pending_ = {};
    dirty_ = false;
    settingStatus_ = saved ? IDS_AWAKE_RULE_SAVED : IDS_SAVE_FAILED;
}
void RegisterKeepAwake(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"keep-awake";
    descriptor.name = IDS_AWAKE;
    descriptor.summary = IDS_AWAKE_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM;
    descriptor.needs = {L"power-state", L"session-events", L"process-query", L"window-event-hook",
                        L"file-write:settings"};
    descriptor.budget = {4, 0};
    descriptor.settings = {{L"RuleAC", IDS_AWAKE_RULE_AC, SettingType::Boolean, L"1"},
                           {L"RuleExternal", IDS_AWAKE_RULE_EXTERNAL, SettingType::Boolean, L"0"},
                           {L"RuleDisplay", IDS_AWAKE_RULE_DISPLAY, SettingType::Boolean, L"0"},
                           {L"RuleApps", IDS_AWAKE_RULE_APPS, SettingType::Text, L"", 0, 0, 654}};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {
        {L"keep-awake.open", IDS_AWAKE, L"", true},
        {L"keep-awake.15m", IDS_AWAKE_15, L"", true},
        {L"keep-awake.1h", IDS_AWAKE_60, L"", true},
        {L"keep-awake.display-15m", IDS_AWAKE_DISPLAY, L"", true},
        {L"keep-awake.until-stop", IDS_AWAKE_UNTIL_STOP, L"", true},
        {L"keep-awake.stop", IDS_AWAKE_STOP},
        {L"keep-awake.rule-ac", IDS_AWAKE_RULE_AC},
        {L"keep-awake.rule-external", IDS_AWAKE_RULE_EXTERNAL},
        {L"keep-awake.rule-display", IDS_AWAKE_RULE_DISPLAY},
        {L"keep-awake.rule-apps", IDS_AWAKE_RULE_APPS, L"", false, true, false, IDS_AWAKE_RULE_APPS_HELP},
        {L"keep-awake.rules-start", IDS_AWAKE_RULE_START, L"", true}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<KeepAwakeModule>(); });
}
} // namespace modules
