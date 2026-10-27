#pragma once
#include "ModuleRegistry.h"
#include "ClipboardTask.h"
#include "SnippetModel.h"

namespace modules {
class SnippetsModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    clipboard::Task task_;
    uint64_t token_ = 0, next_ = 0;
    bool mayCopy_ = false, dirty_ = false, readable_ = true;
    UINT status_ = 0;
    std::vector<snippets::Entry> entries_, previous_;
    void BeginEdit();
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    bool Invoke(const std::wstring &, HWND) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    std::vector<Action> Actions() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override { return task_.Cpu(); }
    std::optional<SettingsStore::Section> PendingSettings() const override;
    void SettingsSaved(bool) override;
};
void RegisterSnippets(Registry &);
}
