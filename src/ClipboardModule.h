#pragma once
#include "ModuleRegistry.h"
#include "ClipboardData.h"
#include "ClipboardVault.h"
#include <atomic>
#include <mutex>
#include <optional>

namespace modules {
inline constexpr UINT OpenClipboardHistory = WM_APP + 72;
class ClipboardModule final : public Module {
    Ledger *ledger_ = nullptr;
    Descriptor descriptor_;
    uint64_t listenerToken_ = 0, writeToken_ = 0, selected_ = 0;
    HANDLE thread_ = nullptr, ready_ = nullptr, cancel_ = nullptr;
    DWORD threadId_ = 0;
    std::atomic<HWND> window_ = nullptr;
    HWND owner_ = nullptr;
    bool started_ = false, inert_ = true, diagnostic_ = false, unlocked_ = false;
    bool synthetic_ = false;
    std::atomic<bool> syntheticUnlocked_ = true;
    DWORD pendingSequence_ = 0, lastSequence_ = 0;
    std::atomic<uint64_t> retention_ = 600000;
    size_t configuredCapacity_ = 25;
    std::vector<std::wstring> ignoredApps_;
    uint64_t epoch_ = 0;
    mutable std::mutex mutex_;
    clipboard::History history_;
    std::optional<clipboard::Payload> write_;
    HWND pasteTarget_ = nullptr;
    std::atomic<bool> writing_ = false, stopping_ = false;
    std::atomic<bool> clearing_ = false;
    std::atomic<UINT> status_ = 0;
    clipboard::TransferActivity activity_;
    std::atomic<size_t> transferBytes_ = 0;
    bool timer_ = false;
    std::filesystem::path vaultPath_;
    uint64_t fileToken_ = 0;
    bool desiredPersist_ = false, configDirty_ = false, persistEffective_ = false, vaultTouched_ = false;
    std::atomic<bool> persistRequested_ = false, policyPending_ = false, vaultFailed_ = false, saveOnExit_ = false,
                      eraseRequested_ = false;
    std::atomic<size_t> diskBytes_ = 0;
    inline static std::atomic<unsigned> workers_ = 0, windows_ = 0, listeners_ = 0, sessions_ = 0,
                                        powerSubscriptions_ = 0, timers_ = 0, classes_ = 0, cleanupFailures_ = 0;
    static DWORD WINAPI Run(void *);
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    void Loop();
    void Work();
    void Clear(bool locked);
    void Notify() const;
    bool Expiry(bool save = true);
    void CancelExpiry();
    bool QueueCopy(uint64_t, bool plain, HWND pasteTarget = nullptr);
    void LoadPersistence();
    void ApplyPersistence();
    void PersistSnapshot() noexcept;
    void PersistenceFailure() noexcept;
    void ErasePersistence() noexcept;
    void FinishPersistence() noexcept;
    bool SessionUnlockedNow() const;

  public:
    static bool Empty() {
        return workers_ == 0 && windows_ == 0 && listeners_ == 0 && sessions_ == 0 && powerSubscriptions_ == 0 &&
               timers_ == 0 && classes_ == 0 && cleanupFailures_ == 0;
    }
    bool Start(Ledger &, const Descriptor &, const SettingsStore::Section &) override;
    void Stop() noexcept override;
    void Poll() override;
    bool Invoke(const std::wstring &, HWND) override;
    std::vector<Action> Actions() const override;
    std::vector<clipboard::Item> HistoryRecords() const override;
    Panel BuildPanel(const std::wstring &) const override;
    size_t OwnedBytes() const noexcept override;
    uint64_t BackgroundCpu100ns() const noexcept override;
    std::optional<SettingsStore::Section> PendingSettings() const override;
    void SettingsSaved(bool) override;
};
void RegisterClipboard(Registry &);
} // namespace modules
