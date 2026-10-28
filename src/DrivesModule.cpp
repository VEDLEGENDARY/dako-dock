#include "DrivesModule.h"
#include "ModuleStrings.h"
#include <shlobj.h>
#include <shellapi.h>

namespace modules {
namespace {
std::wstring EjectId(uint64_t generation, size_t index) {
    return L"drives.eject-" + std::to_wstring(generation) + L"-" + std::to_wstring(index);
}
std::wstring Label(const systemactions::Drive &drive) { return drive.roots + L"· " + drive.label; }
} // namespace
bool DrivesModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) {
    ledger_ = &ledger;
    descriptor_ = descriptor;
    active_ = true;
    fixture_ = ledger.Fixture() || ledger.Diagnostic();
    canEject_ = !fixture_ && !ledger.Preview();
    status_ = IDS_DRIVES_HINT;
    generation_ = ++sequence_;
    if (fixture_)
        drives_ = {{L"DAKO\\FIXTURE\\REMOVABLE", String(IDS_DRIVE_FIXTURE), L"Z:\\ ", 1}};
    return true;
}
void DrivesModule::Stop() noexcept {
    active_ = false;
    canEject_ = false;
    task_.Stop();
    if (audit_) {
        audit_->Complete(false);
        audit_.reset();
    }
    if (token_ && ledger_)
        ledger_->Release(token_);
    token_ = 0;
    ledger_ = nullptr;
    std::vector<systemactions::Drive>().swap(drives_);
}
void DrivesModule::Poll() {
    if (auto reply = task_.Consume()) {
        if (token_ && ledger_)
            ledger_->Release(token_);
        token_ = 0;
        error_ = reply->error;
        generation_ = ++sequence_;
        if (ejecting_) {
            if (audit_) {
                audit_->Complete(reply->status == clipboard::TransferStatus::Ok);
                audit_.reset();
            }
            drives_.clear();
            status_ = reply->status == clipboard::TransferStatus::Ok ? IDS_DRIVES_EJECTED : IDS_DRIVES_FAILED;
        } else {
            auto drives = reply->status == clipboard::TransferStatus::Ok ? systemactions::DecodeDrives(reply->payload)
                                                                         : std::nullopt;
            if (drives) {
                drives_ = std::move(*drives);
                status_ = drives_.empty() ? IDS_DRIVES_EMPTY : IDS_DRIVES_SNAPSHOT;
            } else {
                drives_.clear();
                status_ = IDS_DRIVES_FAILED;
            }
        }
        ejecting_ = false;
    }
}
bool DrivesModule::Invoke(const std::wstring &id, HWND owner) {
    if (!active_)
        return false;
    if (id == L"drives.open")
        return true;
    if (id == L"drives.review") {
        if (fixture_)
            return false;
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &item)))
            return false;
        SHELLEXECUTEINFOW action{sizeof(action)};
        action.fMask = SEE_MASK_IDLIST;
        action.hwnd = owner;
        action.lpVerb = L"open";
        action.lpIDList = item;
        action.nShow = SW_SHOWNORMAL;
        bool opened = active_ && ShellExecuteExW(&action);
        CoTaskMemFree(item);
        return opened;
    }
    if (id == L"drives.refresh") {
        if (fixture_) {
            generation_ = ++sequence_;
            status_ = IDS_DRIVES_SNAPSHOT;
            drives_ = {{L"DAKO\\FIXTURE\\REMOVABLE", String(IDS_DRIVE_FIXTURE), L"Z:\\ ", 1}};
            return true;
        }
        if (task_.Busy())
            return true;
        drives_.clear();
        generation_ = ++sequence_;
        error_ = 0;
        ejecting_ = false;
        token_ = ledger_->Acquire(descriptor_, L"device-control");
        if (!task_.Start(clipboard::TransferOp::QueryDrives, {}, ledger_->Owner(), descriptor_.id, 4000)) {
            ledger_->Release(token_);
            token_ = 0;
            return false;
        }
        status_ = IDS_SYSTEM_LOADING;
        return true;
    }
    if (!canEject_ || task_.Busy())
        return false;
    for (size_t index = 0; index < drives_.size(); ++index) {
        if (id != EjectId(generation_, index))
            continue;
        auto payload = systemactions::Encode(std::vector<systemactions::Drive>{drives_[index]});
        if (!payload)
            return false;
        token_ = ledger_->Acquire(descriptor_, L"device-control");
        audit_.emplace(descriptor_.id, L"device-control", drives_[index].roots,
                       L"explicit-confirmed-safe-removal-request");
        if (!task_.Start(clipboard::TransferOp::EjectDrive, std::move(*payload), ledger_->Owner(), descriptor_.id,
                         10000)) {
            audit_->Complete(false);
            audit_.reset();
            ledger_->Release(token_);
            token_ = 0;
            return false;
        }
        ejecting_ = true;
        status_ = IDS_DRIVES_EJECTING;
        error_ = 0;
        return true;
    }
    return false;
}
std::vector<Action> DrivesModule::Actions() const {
    std::vector<Action> actions;
    if (task_.Busy())
        return actions;
    for (size_t index = 0; index < drives_.size(); ++index) {
        Action action{EjectId(generation_, index),
                      0,
                      String(IDS_DRIVE_EJECT) + L" · " + Label(drives_[index]),
                      true,
                      false,
                      true,
                      IDS_DRIVE_WARNING};
        action.details = Label(drives_[index]);
        actions.push_back(std::move(action));
    }
    return actions;
}
Panel DrivesModule::BuildPanel(const std::wstring &query) const {
    Panel panel{IDS_DRIVES,
                {{String(IDS_DRIVES_REFRESH), L"drives.refresh", !task_.Busy()},
                 {String(IDS_DRIVES_REVIEW), L"drives.review", !fixture_}}};
    for (size_t index = 0; index < drives_.size(); ++index) {
        auto label = Label(drives_[index]);
        if (query.empty() || FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE, label.c_str(),
                                             -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0) >= 0)
            panel.rows.push_back({std::move(label), EjectId(generation_, index), canEject_ && !task_.Busy()});
    }
    panel.rows.push_back({String(status_), L""});
    if (error_)
        panel.rows.push_back({String(IDS_SYSTEM_ERROR) + L" " + std::to_wstring(error_), L""});
    panel.rows.push_back({String(IDS_DRIVES_SCOPE), L""});
    return panel;
}
size_t DrivesModule::OwnedBytes() const noexcept {
    size_t bytes = sizeof(*this) + DescriptorAllocations(descriptor_) + task_.Bytes() +
                   drives_.capacity() * sizeof(systemactions::Drive);
    for (const auto &drive : drives_)
        bytes += (drive.id.capacity() + drive.label.capacity() + drive.roots.capacity()) * sizeof(wchar_t);
    return bytes;
}
void RegisterDrives(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"drives";
    descriptor.name = IDS_DRIVES;
    descriptor.summary = IDS_DRIVES_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM;
    descriptor.needs = {L"device-control"};
    descriptor.budget = {72, 0};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"drives.open", IDS_DRIVES_COMMAND, L"", true},
                          {L"drives.refresh", IDS_DRIVES_REFRESH, L"", true},
                          {L"drives.review", IDS_DRIVES_REVIEW}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<DrivesModule>(); });
}
} // namespace modules
