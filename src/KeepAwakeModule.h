#pragma once
#include "ModuleRegistry.h"
#include "AwakeRules.h"
#include <atomic>

namespace modules {
class KeepAwakeModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    HANDLE thread_ = nullptr, stop_ = nullptr;
    uint64_t token_ = 0, deadline_ = 0;
    uint64_t releasedCpu_ = 0;
    DWORD duration_ = 0;
    bool display_ = false, inert_ = true;
    awake::Rules rules_, pending_;
    awake::Worker ruleWorker_;
    std::vector<uint64_t> ruleTokens_;
    bool dirty_ = false;
    UINT settingStatus_ = 0;
    std::atomic<UINT> status_ = 0;
    static DWORD WINAPI Run(void *);
    void Release() noexcept;
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    bool Invoke(const std::wstring &, HWND) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    std::optional<SettingsStore::Section> PendingSettings() const override;
    void SettingsSaved(bool) override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override;
    double HookMicroseconds() const noexcept override { return ruleWorker_.HookMicroseconds(); }
};
void RegisterKeepAwake(Registry &);
}
