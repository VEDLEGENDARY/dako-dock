#pragma once
#include "ModuleRegistry.h"
#include "ClipboardTask.h"
#include "SystemActionModel.h"

namespace modules {
class RecycleBinModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    clipboard::Task task_;
    systemactions::BinTotals totals_;
    UINT status_ = 0;
    uint64_t token_ = 0;
    bool fixture_ = true, canEmpty_ = false, ready_ = false, active_ = false;
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    bool Invoke(const std::wstring &, HWND) override;
    std::vector<Action> Actions() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override { return task_.Cpu(); }
};
void RegisterRecycleBin(Registry &);
}
