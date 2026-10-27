#pragma once
#include "ModuleRegistry.h"
#include <atomic>

namespace modules {
class WindowsShortcuts final : public Module {
    bool launch_ = false;
    std::wstring uri_;
    uint64_t generation_ = 0;
    inline static std::atomic<uint64_t> sequence_ = 0;
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    bool Invoke(const std::wstring &action, HWND owner) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    void Query(const std::wstring &) override;
    void ViewQuery(const std::wstring &query) override { Query(query); }
    std::vector<Action> Actions() const override;
    size_t OwnedBytes() const noexcept override { return sizeof(*this) + uri_.capacity() * sizeof(wchar_t); }
    Panel BuildPanel(const std::wstring &query) const override;
};
}
