#pragma once
#include "ModuleRegistry.h"
#include "ClipboardTask.h"

namespace modules {
class UtilityModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    clipboard::Task task_;
    uint64_t token_ = 0, generation_ = 0;
    bool mayCopy_ = false;
    UINT status_ = 0;
    std::wstring query_, result_, expression_, panelResult_, panelExpression_;
    void Evaluate(const std::wstring &input, UINT kind);
    bool Copy(const std::wstring &value);
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    bool Invoke(const std::wstring &, HWND) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    void Query(const std::wstring &) override;
    std::vector<Action> Actions() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override { return task_.Cpu(); }
};
void RegisterUtilities(Registry &);
}
