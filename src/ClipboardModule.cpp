#include "ClipboardModule.h"
#include "ModuleStrings.h"
#include <wtsapi32.h>

namespace modules {
namespace {
constexpr UINT Wake = WM_APP + 1;
constexpr uint64_t Lifetime = clipboard::RetentionMs;
uint64_t Tick() { return GetTickCount64() + Lifetime; }
std::wstring Label(const clipboard::Item &item) {
    UINT kind = item.kind == clipboard::Kind::Text ? IDS_CLIP_TEXT :
        item.kind == clipboard::Kind::Image ? IDS_CLIP_IMAGE : IDS_CLIP_FILES;
    auto text = item.text.substr(0, 100);
    std::replace(text.begin(), text.end(), L'\n', L' ');
    std::replace(text.begin(), text.end(), L'\r', L' ');
    return (item.pinned ? String(IDS_CLIP_PINNED) + L" · " : L"") + String(kind) + L" · " + text;
}
uint64_t Suffix(const std::wstring &id, const wchar_t *prefix) {
    std::wstring_view text(id), begin(prefix);
    if (!text.starts_with(begin)) return 0;
    text.remove_prefix(begin.size());
    if (text.empty() || text.size() > 20 || text.find_first_not_of(L"0123456789") != std::wstring_view::npos) return 0;
    try { return std::stoull(std::wstring(text)); } catch (...) { return 0; }
}
}
bool ClipboardModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &settings) {
    ledger_ = &ledger; descriptor_ = descriptor; owner_ = ledger.Owner();
    diagnostic_ = ledger.Diagnostic();
    synthetic_ = ledger.FixtureStorageIo();
    if (!ledger.StorageFixtureRoot().empty() && !synthetic_) return false;
    inert_ = (ledger.Fixture() || ledger.Preview()) && !synthetic_;
    auto persist = settings.find(L"Persist");
    desiredPersist_ = persist != settings.end() && persist->second == L"1";
    persistRequested_ = desiredPersist_;
    if (!ledger.StorePath().empty())
        vaultPath_ = std::filesystem::absolute(ledger.StorePath()).parent_path() / L"clipboard-history.dpapi";
    if (ledger.Fixture() && (!synthetic_ || !desiredPersist_)) {
        status_ = IDS_CLIP_INERT;
        {
            clipboard::Item item;
            item.text = L"Fixture clipboard text"; item.format = CF_UNICODETEXT;
            history_.Add(std::move(item), false, clipboard::HistoryFlag::Allowed, Tick());
            clipboard::Payload image{CF_DIB};
            BITMAPINFOHEADER header{};
            header.biSize = sizeof(header); header.biWidth = 128; header.biHeight = 64;
            header.biPlanes = 1; header.biBitCount = 32;
            image.bytes.resize(sizeof(header) + 128 * 64 * 4);
            std::memcpy(image.bytes.data(), &header, sizeof(header));
            for (size_t pixel = 0; pixel < 128 * 64; ++pixel) {
                DWORD color = ((pixel / 128 / 16) + (pixel % 128 / 16)) % 2 ? 0x00d66e35 : 0x00ece5d5;
                std::memcpy(image.bytes.data() + sizeof(header) + pixel * 4, &color, sizeof(color));
            }
            if (auto fixture = clipboard::ToItem(std::move(image)))
                history_.Add(std::move(*fixture), false, clipboard::HistoryFlag::Allowed, Tick());
        }
    }
    if (inert_) return true;
    if (desiredPersist_ && !diagnostic_) fileToken_ = ledger.Acquire(descriptor, L"file-write:clipboard-history");
    cancel_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!cancel_ || !ready_) return false;
    stopping_ = false;
    thread_ = CreateThread(nullptr, 0, Run, this, 0, &threadId_);
    if (!thread_ || WaitForSingleObject(ready_, 5000) != WAIT_OBJECT_0 || !started_) return false;
    if (!synthetic_) listenerToken_ = ledger.Acquire(descriptor, L"clipboard-listener");
    return true;
}
DWORD WINAPI ClipboardModule::Run(void *value) {
    SetThreadDescription(GetCurrentThread(), L"Dako.Module.Clipboard");
    ++workers_;
    struct WorkerScope { ~WorkerScope() { --workers_; } } scope;
    auto *self = static_cast<ClipboardModule *>(value);
    try { self->Loop(); } catch (...) { self->status_ = IDS_CLIP_FAILED; self->Notify(); }
    self->window_ = nullptr;
    SetEvent(self->ready_);
    return 0;
}
void ClipboardModule::Notify() const { if (owner_) PostMessageW(owner_, Changed, 0, 0); }
bool ClipboardModule::SessionUnlockedNow() const {
    return synthetic_ ? syntheticUnlocked_.load() : clipboard::SessionUnlocked();
}
void ClipboardModule::Clear(bool locked) {
    ++epoch_;
    pendingSequence_ = 0;
    lastSequence_ = synthetic_ ? 0 : GetClipboardSequenceNumber();
    SetEvent(cancel_);
    {
        std::lock_guard lock(mutex_);
        history_.Clear(); write_.reset();
    }
    writing_ = false;
    CancelExpiry();
    if (vaultTouched_ || persistEffective_) ErasePersistence();
    status_ = locked ? IDS_CLIP_LOCKED : IDS_CLIP_CLEARED;
    policyPending_ = false;
    clearing_ = false;
    Notify();
}
bool ClipboardModule::Expiry(bool save) {
    CancelExpiry();
    uint64_t next = UINT64_MAX, now = Tick();
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        size_t before = history_.Items().size();
        history_.Expire(now, Lifetime);
        changed = before != history_.Items().size();
        for (const auto &item : history_.Items()) if (!item.pinned)
            next = std::min(next, item.captured + Lifetime);
    }
    if (next != UINT64_MAX && SetTimer(window_, 1,
        static_cast<UINT>(std::max<uint64_t>(1, next > now ? next - now : 1)), nullptr)) { timer_ = true; ++timers_; }
    if (save && changed) PersistSnapshot();
    return changed;
}
void ClipboardModule::CancelExpiry() {
    if (timer_) {
        if (!KillTimer(window_, 1)) ++cleanupFailures_;
        timer_ = false; --timers_;
    }
}
LRESULT CALLBACK ClipboardModule::Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<ClipboardModule *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ClipboardModule *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    if (!self) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        if (!self->synthetic_ && !self->diagnostic_ && self->unlocked_ && !self->stopping_) {
            DWORD sequence = GetClipboardSequenceNumber();
            if (sequence && sequence != self->lastSequence_) self->pendingSequence_ = sequence;
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK || wp == WTS_SESSION_LOGOFF || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT) {
            self->unlocked_ = false; self->Clear(true);
        } else if (wp == WTS_SESSION_UNLOCK || wp == WTS_SESSION_LOGON || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
            self->unlocked_ = !self->diagnostic_ && self->SessionUnlockedNow();
            self->lastSequence_ = self->synthetic_ ? 0 : GetClipboardSequenceNumber();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) { self->unlocked_ = false; self->Clear(true); }
        else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            self->unlocked_ = !self->diagnostic_ && self->SessionUnlockedNow();
            self->lastSequence_ = self->synthetic_ ? 0 : GetClipboardSequenceNumber();
        }
        return TRUE;
    case WM_TIMER:
        if (wp == 1) { self->Expiry(); self->Notify(); }
        return 0;
    case WM_HOTKEY:
        if (self->owner_) PostMessageW(self->owner_, OpenClipboardHistory, 0, 0);
        return 0;
    case Wake:
        if (wp == 1) self->Clear(false);
        else if (wp == 2) { self->Expiry(false); self->PersistSnapshot(); }
        else if (wp == 3) self->ApplyPersistence();
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
void ClipboardModule::Loop() {
    std::wstring name = L"Dako.Clipboard." + std::to_wstring(reinterpret_cast<uintptr_t>(this));
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = name.c_str(); cls.lpfnWndProc = Proc;
    bool registered = RegisterClassW(&cls) != 0;
    if (registered) ++classes_;
    HWND window = registered ? CreateWindowExW(0, name.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                              nullptr, cls.hInstance, this) : nullptr;
    bool listener = window && !synthetic_ && AddClipboardFormatListener(window);
    if (window) ++windows_;
    if (listener) ++listeners_;
    bool session = listener && WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
    if (session) ++sessions_;
    HPOWERNOTIFY power = session ? RegisterSuspendResumeNotification(window, DEVICE_NOTIFY_WINDOW_HANDLE) : nullptr;
    if (power) ++powerSubscriptions_;
    bool hotkey = power && !diagnostic_ && !synthetic_ && RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'V');
    struct Cleanup {
        HWND window; HINSTANCE instance; const std::wstring &name;
        bool registered, listener, session, hotkey; HPOWERNOTIFY power;
        ClipboardModule *self;
        ~Cleanup() {
            self->FinishPersistence();
            self->CancelExpiry();
            if (hotkey) UnregisterHotKey(window, 1);
            if (power) { if (!UnregisterSuspendResumeNotification(power)) ++cleanupFailures_; --powerSubscriptions_; }
            if (session) { if (!WTSUnRegisterSessionNotification(window)) ++cleanupFailures_; --sessions_; }
            if (listener) { if (!RemoveClipboardFormatListener(window)) ++cleanupFailures_; --listeners_; }
            if (window) { if (!DestroyWindow(window)) ++cleanupFailures_; --windows_; }
            if (registered) { if (!UnregisterClassW(name.c_str(), instance)) ++cleanupFailures_; --classes_; }
        }
    } cleanup{window, cls.hInstance, name, registered, listener, session, hotkey, power, this};
    started_ = window && (synthetic_ || (listener && session && power));
    lastSequence_ = synthetic_ ? 0 : GetClipboardSequenceNumber();
    unlocked_ = started_ && !diagnostic_ && SessionUnlockedNow();
    status_ = !unlocked_ && !diagnostic_ ? IDS_CLIP_LOCKED : hotkey || diagnostic_ ? IDS_CLIP_READY : IDS_CLIP_NO_HOTKEY;
    if (started_ && !diagnostic_) LoadPersistence();
    if (synthetic_) Expiry(false);
    SetEvent(ready_);
    if (started_) {
        MSG message{};
        while (!stopping_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
            if (!stopping_ && !diagnostic_) Work();
        }
    }
}
void ClipboardModule::Work() {
    if (synthetic_ || !unlocked_ || stopping_ || clearing_) return;
    std::optional<clipboard::Payload> write;
    uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        write = std::move(write_); write_.reset();
        generation = history_.ClearGeneration();
    }
    DWORD sequence = std::exchange(pendingSequence_, 0UL);
    if (!write && !sequence) return;
    uint64_t epoch = epoch_;
    ResetEvent(cancel_);
    if (stopping_) return;
    transferBytes_ = clipboard::TransferLimit + (write ? write->bytes.capacity() : 0);
    clipboard::Payload input = write ? std::move(*write) : clipboard::Payload{CF_UNICODETEXT, sequence, {}};
    auto reply = clipboard::Transfer(write ? clipboard::TransferOp::Write : clipboard::TransferOp::Read,
                                      input, cancel_, 1000, L"clipboard", &activity_);
    transferBytes_ = 0;
    if (write) {
        writing_ = false;
        if (epoch == epoch_ && unlocked_)
            status_ = reply.status == clipboard::TransferStatus::Ok ? IDS_COPIED_RESULT : IDS_COPY_FAILED;
    } else if (epoch == epoch_ && unlocked_ && !stopping_) {
        bool added = false;
        lastSequence_ = sequence;
        if (reply.status == clipboard::TransferStatus::Ok) {
            if (auto item = clipboard::ToItem(std::move(reply.payload))) {
                std::lock_guard lock(mutex_);
                if (!stopping_ && !clearing_)
                    added = history_.AddIfUncleared(std::move(*item), false, clipboard::HistoryFlag::Allowed, Tick(), generation);
                status_ = added ? IDS_CLIP_READY : IDS_CLIP_LIMIT;
            } else status_ = IDS_CLIP_UNSUPPORTED;
        } else if (reply.status == clipboard::TransferStatus::Excluded) status_ = IDS_CLIP_EXCLUDED;
        else if (reply.status != clipboard::TransferStatus::Changed) status_ = IDS_CLIP_UNSUPPORTED;
        bool expired = Expiry(false);
        if (added || expired) PersistSnapshot();
    }
    Notify();
    // A newer clipboard event may have arrived while the bounded helper was running.
    if (pendingSequence_ || writing_) PostMessageW(window_, Wake, 0, 0);
}
void ClipboardModule::Stop() noexcept {
    saveOnExit_ = ledger_ && ledger_->ShuttingDown();
    stopping_ = true;
    if (cancel_) SetEvent(cancel_);
    if (thread_) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_); thread_ = nullptr;
    }
    if (ready_) CloseHandle(ready_);
    if (cancel_) CloseHandle(cancel_);
    ready_ = cancel_ = nullptr;
    if (vaultFailed_) { desiredPersist_ = false; configDirty_ = true; }
    if (ledger_) {
        if (listenerToken_) ledger_->Release(listenerToken_);
        if (writeToken_) ledger_->Release(writeToken_);
        if (fileToken_) ledger_->Release(fileToken_);
    }
    listenerToken_ = writeToken_ = fileToken_ = 0; ledger_ = nullptr;
    std::lock_guard lock(mutex_);
    history_.Clear(); write_.reset(); selected_ = 0; writing_ = false;
}
void ClipboardModule::Poll() {
    if (thread_ && WaitForSingleObject(thread_, 0) == WAIT_OBJECT_0) {
        status_ = IDS_CLIP_FAILED; writing_ = false;
        if (listenerToken_) { ledger_->Release(listenerToken_); listenerToken_ = 0; }
    }
    if (writeToken_ && !writing_) { ledger_->Release(writeToken_); writeToken_ = 0; }
    if (!configDirty_ && !policyPending_ && desiredPersist_ != persistRequested_.load()) {
        desiredPersist_ = persistRequested_.load(); configDirty_ = true;
    }
    if (fileToken_ && !persistRequested_ && !policyPending_) { ledger_->Release(fileToken_); fileToken_ = 0; }
}
void ClipboardModule::PersistenceFailure() noexcept {
    persistEffective_ = false;
    persistRequested_ = false;
    vaultFailed_ = true;
    Notify();
}
void ClipboardModule::ErasePersistence() noexcept {
    if (inert_ || diagnostic_ || vaultPath_.empty()) return;
    try {
        if (!clipboard::EraseVault(vaultPath_)) { PersistenceFailure(); return; }
        vaultTouched_ = false;
        vaultFailed_ = false;
    } catch (...) { PersistenceFailure(); }
}
void ClipboardModule::PersistSnapshot() noexcept {
    if (inert_ || diagnostic_ || !persistEffective_ || !persistRequested_ || vaultFailed_) return;
    try {
        if (vaultPath_.empty()) { PersistenceFailure(); return; }
        if (!unlocked_ || !SessionUnlockedNow()) {
            unlocked_ = false;
            Clear(true);
            return;
        }
        std::vector<clipboard::Item> snapshot;
        uint64_t generation = 0;
        {
            std::lock_guard lock(mutex_);
            if (clearing_) return;
            diskBytes_ = history_.Bytes() * 4;
            snapshot = history_.Items();
            generation = history_.Generation();
        }
        struct Scratch { std::atomic<size_t> &bytes; ~Scratch() { bytes = 0; } } scratch{diskBytes_};
        vaultTouched_ = true;
        bool saved = snapshot.empty() ? clipboard::EraseVault(vaultPath_) : clipboard::SaveVault(vaultPath_, snapshot, Tick());
        if (!saved) { PersistenceFailure(); return; }
        bool changed = false;
        { std::lock_guard lock(mutex_); changed = generation != history_.Generation(); }
        if (changed || clearing_ || !persistRequested_) { ErasePersistence(); return; }
        if (!SessionUnlockedNow()) { unlocked_ = false; Clear(true); }
    } catch (...) { diskBytes_ = 0; PersistenceFailure(); }
}
void ClipboardModule::LoadPersistence() {
    persistEffective_ = persistRequested_.load();
    if (!persistEffective_) return;
    vaultTouched_ = true;
    if (vaultPath_.empty()) { PersistenceFailure(); return; }
    if (!unlocked_) { ErasePersistence(); return; }
    std::error_code error;
    bool exists = std::filesystem::exists(vaultPath_, error);
    if (error) { PersistenceFailure(); return; }
    if (!exists) return;
    auto loaded = clipboard::LoadVault(vaultPath_);
    if (!loaded) { PersistenceFailure(); return; }
    if (!SessionUnlockedNow()) { unlocked_ = false; Clear(true); return; }
    {
        std::lock_guard lock(mutex_);
        auto now = Tick();
        for (auto item = loaded->rbegin(); item != loaded->rend(); ++item) {
            auto age = item->age;
            history_.Add(std::move(item->item), false, clipboard::HistoryFlag::Allowed, now - age);
        }
    }
    Expiry(false);
    Notify();
}
void ClipboardModule::ApplyPersistence() {
    bool requested = persistRequested_.load();
    bool erase = eraseRequested_.exchange(false);
    if (requested) {
        vaultFailed_ = false;
        persistEffective_ = true;
        PersistSnapshot();
    } else {
        persistEffective_ = false;
        if (erase || !vaultFailed_) ErasePersistence();
    }
    policyPending_ = false;
    Notify();
}
void ClipboardModule::FinishPersistence() noexcept {
    if (inert_ || diagnostic_ || !started_) return;
    if (!saveOnExit_) { ErasePersistence(); return; }
    if (!vaultTouched_ && !persistEffective_ && !persistRequested_ && !eraseRequested_) return;
    if (clearing_) { Clear(false); return; }
    if (eraseRequested_ || (!persistRequested_ && !vaultFailed_)) { ErasePersistence(); return; }
    if (saveOnExit_) {
        if (persistRequested_ && !vaultFailed_) { persistEffective_ = true; PersistSnapshot(); }
        else if (!unlocked_) ErasePersistence();
    } else ErasePersistence();
}
std::optional<SettingsStore::Section> ClipboardModule::PendingSettings() const {
    if (!configDirty_) return {};
    return SettingsStore::Section{{L"Persist", desiredPersist_ ? L"1" : L"0"}};
}
void ClipboardModule::SettingsSaved(bool saved) {
    configDirty_ = false;
    if (!saved) {
        status_ = IDS_SAVE_FAILED;
        if (desiredPersist_) { desiredPersist_ = persistRequested_.load(); return; }
        // A restrictive off request still takes effect for this session when settings are unwritable.
    }
    persistRequested_ = desiredPersist_;
    if (inert_ || diagnostic_ || !ledger_ || !thread_) return;
    if (desiredPersist_ && !fileToken_)
        fileToken_ = ledger_->Acquire(descriptor_, L"file-write:clipboard-history");
    policyPending_ = true;
    PostMessageW(window_, Wake, 3, 0);
}
bool ClipboardModule::QueueCopy(uint64_t id, bool plain) {
    Poll();
    if (inert_ || synthetic_ || diagnostic_ || writing_ || !thread_ || WaitForSingleObject(thread_, 0) != WAIT_TIMEOUT) return false;
    {
        std::lock_guard lock(mutex_);
        const auto *item = history_.Find(id);
        if (!item || (plain && item->kind == clipboard::Kind::Image)) return false;
        write_ = clipboard::FromItem(*item, plain);
        writing_ = true;
    }
    writeToken_ = ledger_->Acquire(descriptor_, L"clipboard-write");
    status_ = IDS_COPYING_RESULT;
    PostThreadMessageW(threadId_, Wake, 0, 0);
    return true;
}
bool ClipboardModule::Invoke(const std::wstring &id, HWND) {
    if (synthetic_ && id == L"clipboard.fixture-lock") {
        syntheticUnlocked_ = false;
        return PostMessageW(window_, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0) != FALSE;
    }
    if (id == L"clipboard.persist-on" || id == L"clipboard.persist-off") {
        desiredPersist_ = id == L"clipboard.persist-on";
        eraseRequested_ = !desiredPersist_;
        configDirty_ = true;
        return true;
    }
    if (id == L"clipboard.open" || id == L"clipboard.back") { selected_ = 0; return true; }
    if (id == L"clipboard.clear") {
        clearing_ = true;
        if (cancel_) SetEvent(cancel_);
        selected_ = 0;
        { std::lock_guard lock(mutex_); history_.Clear(); }
        if (thread_) {
            if (!inert_ && !diagnostic_ && !fileToken_)
                fileToken_ = ledger_->Acquire(descriptor_, L"file-write:clipboard-history");
            policyPending_ = true;
            PostMessageW(window_, Wake, 1, 0);
        }
        return true;
    }
    if (auto item = Suffix(id, L"clipboard.copy-")) return QueueCopy(item, false);
    if (auto item = Suffix(id, L"clipboard.plain-")) return QueueCopy(item, true);
    std::lock_guard lock(mutex_);
    if (auto item = Suffix(id, L"clipboard.preview-")) {
        if (!history_.Find(item)) return false;
        selected_ = item; return true;
    }
    if (auto item = Suffix(id, L"clipboard.pin-")) {
        bool ok = history_.Pin(item);
        if (thread_) PostMessageW(window_, Wake, 2, 0);
        return ok;
    }
    if (auto item = Suffix(id, L"clipboard.remove-")) {
        selected_ = 0;
        bool ok = history_.Remove(item);
        if (thread_) PostMessageW(window_, Wake, 2, 0);
        return ok;
    }
    return false;
}
std::vector<Action> ClipboardModule::Actions() const {
    std::lock_guard lock(mutex_);
    std::vector<Action> result;
    if (synthetic_) result.push_back({L"clipboard.fixture-lock", IDS_CLIP_LOCKED});
    for (const auto &item : history_.Items()) {
        auto suffix = std::to_wstring(item.id);
        result.push_back({L"clipboard.preview-" + suffix, 0, Label(item), true});
        result.push_back({L"clipboard.copy-" + suffix, IDS_COPY_RESULT});
        result.push_back({L"clipboard.plain-" + suffix, IDS_CLIP_PLAIN});
        result.push_back({L"clipboard.pin-" + suffix, IDS_CLIP_PIN});
        result.push_back({L"clipboard.remove-" + suffix, IDS_CLIP_REMOVE});
    }
    return result;
}
Panel ClipboardModule::BuildPanel(const std::wstring &query) const {
    std::lock_guard lock(mutex_);
    Panel panel{IDS_CLIPBOARD_HISTORY, {}};
    if (const auto *item = history_.Find(selected_)) {
        auto suffix = std::to_wstring(item->id);
        panel.rows = {{Label(*item), L""},
            {String(IDS_COPY_RESULT), L"clipboard.copy-" + suffix, !inert_ && !writing_},
            {String(IDS_CLIP_PLAIN), L"clipboard.plain-" + suffix, item->kind != clipboard::Kind::Image && !inert_ && !writing_},
            {String(item->pinned ? IDS_CLIP_UNPIN : IDS_CLIP_PIN), L"clipboard.pin-" + suffix},
            {String(IDS_CLIP_REMOVE), L"clipboard.remove-" + suffix}, {String(IDS_CLIP_BACK), L"clipboard.back"}};
        // Full private text remains a read-only accessible row, never an action ID.
        if (item->kind != clipboard::Kind::Image) {
            // Bound each accessible row; pagination exposes the complete retained text.
            for (size_t at = 0; at < item->text.size(); at += 120)
                panel.rows.push_back({item->text.substr(at, 120), L""});
        }
        else panel.image = clipboard::DecodeImage(clipboard::FromItem(*item));
    } else {
        for (const auto &entry : history_.Items()) {
            if (!query.empty() && FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE,
                entry.text.c_str(), -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0) < 0) continue;
            panel.rows.push_back({Label(entry), L"clipboard.preview-" + std::to_wstring(entry.id)});
        }
        panel.rows.push_back({String(status_ ? status_.load() : IDS_CLIP_READY), L""});
        if (vaultFailed_) panel.rows.push_back({String(IDS_PERSIST_FAILED), L""});
        if (desiredPersist_) panel.rows.push_back({String(IDS_PERSIST_ENABLED), L""});
        panel.rows.push_back({String(IDS_CLIP_CLEAR), L"clipboard.clear", !history_.Items().empty()});
        panel.rows.push_back({String(desiredPersist_ ? IDS_PERSIST_OFF : IDS_PERSIST_ON),
            desiredPersist_ ? L"clipboard.persist-off" : L"clipboard.persist-on", !inert_ && !diagnostic_});
    }
    return panel;
}
size_t ClipboardModule::OwnedBytes() const noexcept {
    std::lock_guard lock(mutex_);
    return sizeof(*this) + DescriptorAllocations(descriptor_) + history_.Bytes() + transferBytes_.load() +
        (write_ ? write_->bytes.capacity() : 0) + activity_.CommittedBytes() + diskBytes_.load();
}
uint64_t ClipboardModule::BackgroundCpu100ns() const noexcept {
    FILETIME c{}, e{}, k{}, u{};
    uint64_t own = 0;
    if (thread_ && GetThreadTimes(thread_, &c, &e, &k, &u)) {
        ULARGE_INTEGER kernel{}, user{};
        kernel.LowPart = k.dwLowDateTime; kernel.HighPart = k.dwHighDateTime;
        user.LowPart = u.dwLowDateTime; user.HighPart = u.dwHighDateTime;
        own = kernel.QuadPart + user.QuadPart;
    }
    return own + activity_.Cpu();
}
void RegisterClipboard(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"clipboard"; descriptor.name = IDS_CLIPBOARD_HISTORY;
    descriptor.summary = IDS_CLIPBOARD_SUMMARY; descriptor.category = IDS_CATEGORY_SYSTEM;
    descriptor.needs = {L"clipboard-listener", L"clipboard-write", L"file-write:clipboard-history"}; descriptor.budget = {80, .005};
    descriptor.settings = {{L"Persist", IDS_PERSIST_ON, SettingType::Boolean, L"0"}};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"clipboard.open", IDS_CLIPBOARD_HISTORY, L"", true},
        {L"clipboard.back", IDS_CLIP_BACK}, {L"clipboard.clear", IDS_CLIP_CLEAR},
        {L"clipboard.persist-on", IDS_PERSIST_ON, L"", false, false, true, IDS_PERSIST_WARNING},
        {L"clipboard.persist-off", IDS_PERSIST_OFF, L"", false, false, true, IDS_PERSIST_ERASE_WARNING}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<ClipboardModule>(); });
}
}
