#pragma once
#include "ModuleRegistry.h"

namespace shelf { struct State; }
namespace modules {
class ShelfModule final : public Module {
    std::shared_ptr<shelf::State> state_;
    bool ole_ = false;
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    void Visible(bool) override;
    bool Invoke(const std::wstring &, HWND) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    bool Drag(const std::wstring &, HWND) override;
    std::vector<Action> Actions() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override;
    static bool Empty();
};
void RegisterShelf(Registry &);
}
