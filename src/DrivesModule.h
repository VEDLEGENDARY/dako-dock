#pragma once
#include "ModuleRegistry.h"
#include "ClipboardTask.h"
#include "SystemActionModel.h"
#include <atomic>

namespace modules {
class DrivesModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    clipboard::Task task_;
    std::optional<privacy::Write> audit_;
    std::vector<systemactions::Drive> drives_;
    uint64_t token_ = 0, generation_ = 0;
    inline static std::atomic<uint64_t> sequence_ = 0;
    UINT status_ = 0;
    DWORD error_ = 0;
    bool active_ = false, fixture_ = true, canEject_ = false, ejecting_ = false;
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
void RegisterDrives(Registry &);
}
