#include "RecycleBinModule.h"
#include "ModuleStrings.h"
#include <shellapi.h>
#include <shlobj.h>

namespace modules {
bool RecycleBinModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) {
    ledger_ = &ledger; descriptor_ = descriptor; active_ = true;
    fixture_ = ledger.Fixture() || ledger.Diagnostic();
    canEmpty_ = !fixture_ && !ledger.Preview();
    status_ = IDS_RECYCLE_HINT;
    if (fixture_) { totals_ = {3, 98304}; ready_ = true; }
    return true;
}
void RecycleBinModule::Stop() noexcept {
    active_ = false; canEmpty_ = false;
    task_.Stop();
    if (token_ && ledger_) ledger_->Release(token_);
    token_ = 0; ledger_ = nullptr; ready_ = false; totals_ = {};
}
void RecycleBinModule::Poll() {
    if (auto reply = task_.Consume()) {
        if (token_ && ledger_) ledger_->Release(token_);
        token_ = 0;
        auto totals = reply->status == clipboard::TransferStatus::Ok ? systemactions::DecodeTotals(reply->payload) : std::nullopt;
        ready_ = totals.has_value();
        if (totals) totals_ = *totals;
        status_ = totals ? IDS_RECYCLE_SNAPSHOT : IDS_RECYCLE_FAILED;
    }
}
bool RecycleBinModule::Invoke(const std::wstring &id, HWND owner) {
    if (!active_) return false;
    if (id == L"recycle-bin.open") return true;
    if (id == L"recycle-bin.refresh") {
        if (fixture_) { totals_ = {3, 98304}; ready_ = true; status_ = IDS_RECYCLE_SNAPSHOT; return true; }
        if (task_.Busy()) return true;
        token_ = ledger_->Acquire(descriptor_, L"recycle-bin");
        if (!task_.Start(clipboard::TransferOp::QueryRecycleBin, {}, ledger_->Owner(), descriptor_.id, 3000)) {
            ledger_->Release(token_); token_ = 0; return false;
        }
        status_ = IDS_SYSTEM_LOADING; return true;
    }
    if (id == L"recycle-bin.review") {
        if (fixture_) return false;
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &item))) return false;
        SHELLEXECUTEINFOW action{sizeof(action)};
        action.fMask = SEE_MASK_IDLIST; action.hwnd = owner; action.lpVerb = L"open"; action.lpIDList = item; action.nShow = SW_SHOWNORMAL;
        bool opened = active_ && ShellExecuteExW(&action);
        CoTaskMemFree(item); return opened;
    }
    if (id != L"recycle-bin.empty" || !canEmpty_ || !ready_ || task_.Busy() || !clipboard::SessionUnlocked()) return false;
    auto *ledger = ledger_;
    uint64_t token = ledger->Acquire(descriptor_, L"recycle-bin");
    token_ = token;
    privacy::Write audit(descriptor_.id, L"file-delete", String(IDS_RECYCLE_TARGET), L"explicit-confirmed-empty-bin");
    // The action registry requires Dako's full-target confirmation before this handler. Never expose an unconfirmed route.
    HRESULT result = SHEmptyRecycleBinW(owner, nullptr, SHERB_NOCONFIRMATION);
    audit.Complete(SUCCEEDED(result));
    ledger->Release(token);
    if (token_ == token) token_ = 0;
    ready_ = false; totals_ = {};
    status_ = SUCCEEDED(result) ? IDS_RECYCLE_DONE : IDS_RECYCLE_FAILED;
    return SUCCEEDED(result);
}
std::vector<Action> RecycleBinModule::Actions() const {
    if (!ready_ || !totals_.items) return {};
    Action empty{L"recycle-bin.empty", IDS_RECYCLE_EMPTY, L"", true, false, true, IDS_RECYCLE_WARNING};
    empty.details = String(IDS_RECYCLE_ITEMS) + L" " + std::to_wstring(totals_.items) + L"\n" +
        String(IDS_RECYCLE_BYTES) + L" " + std::to_wstring(totals_.bytes);
    return {std::move(empty)};
}
Panel RecycleBinModule::BuildPanel(const std::wstring &) const {
    Panel panel{IDS_RECYCLE, {{String(IDS_RECYCLE_REVIEW), L"recycle-bin.review", !fixture_},
        {String(IDS_RECYCLE_REFRESH), L"recycle-bin.refresh", !task_.Busy()}}};
    if (ready_) {
        panel.rows.push_back({String(IDS_RECYCLE_ITEMS) + L" " + std::to_wstring(totals_.items), L""});
        panel.rows.push_back({String(IDS_RECYCLE_BYTES) + L" " + std::to_wstring(totals_.bytes), L""});
        if (totals_.items) panel.rows.push_back({String(IDS_RECYCLE_EMPTY), L"recycle-bin.empty", canEmpty_ && !task_.Busy()});
    }
    panel.rows.push_back({String(status_), L""});
    panel.rows.push_back({String(IDS_RECYCLE_SCOPE), L""});
    return panel;
}
size_t RecycleBinModule::OwnedBytes() const noexcept { return sizeof(*this) + DescriptorAllocations(descriptor_) + task_.Bytes(); }
void RegisterRecycleBin(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"recycle-bin"; descriptor.name = IDS_RECYCLE; descriptor.summary = IDS_RECYCLE_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM; descriptor.needs = {L"recycle-bin"}; descriptor.budget = {72, 0};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"recycle-bin.open", IDS_RECYCLE_COMMAND, L"", true}, {L"recycle-bin.refresh", IDS_RECYCLE_REFRESH, L"", true},
        {L"recycle-bin.review", IDS_RECYCLE_REVIEW}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<RecycleBinModule>(); });
}
}
