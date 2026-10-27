#pragma once
#include "ModuleRegistry.h"
#include "ClipboardTask.h"
#include "FileSearchModel.h"

namespace modules {
class FileSearchModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    clipboard::Task task_;
    std::vector<files::Result> results_;
    std::wstring query_, viewQuery_;
    uint64_t generation_ = 0, runningGeneration_ = 0, token_ = 0;
    inline static std::atomic<uint64_t> sequence_ = 0;
    bool fixture_ = true, launch_ = false;
    UINT status_ = 0;
    void Search(std::wstring);
    void StartQuery();
public:
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    void Query(const std::wstring &) override;
    void ViewQuery(const std::wstring &) override;
    bool Invoke(const std::wstring &, HWND) override;
    bool InvokeWithText(const std::wstring &, HWND, const std::wstring &) override;
    std::vector<Action> Actions() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override { return task_.Cpu(); }
};
void RegisterFiles(Registry &);
}
