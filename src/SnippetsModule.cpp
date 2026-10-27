#include "SnippetsModule.h"
#include "ModuleStrings.h"

namespace modules {
namespace {
uint64_t ItemId(const std::wstring &id, const wchar_t *prefix) {
    std::wstring_view value(id), start(prefix);
    if (!value.starts_with(start)) return 0;
    value.remove_prefix(start.size());
    if (value.empty() || value.size() > 20 || value.find_first_not_of(L"0123456789") != std::wstring_view::npos) return 0;
    try { return std::stoull(std::wstring(value)); } catch (...) { return 0; }
}
}
bool SnippetsModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &settings) {
    ledger_ = &ledger; descriptor_ = descriptor;
    mayCopy_ = !ledger.Fixture() && !ledger.Preview() && !ledger.Diagnostic();
    auto found = settings.find(L"Entries");
    auto decoded = snippets::Decode(found == settings.end() ? L"1" : found->second);
    if (!decoded) { readable_ = false; status_ = IDS_SNIPPETS_CORRUPT; return true; }
    entries_ = std::move(*decoded);
    for (const auto &entry : entries_) next_ = std::max(next_, entry.id);
    return true;
}
void SnippetsModule::Stop() noexcept {
    task_.Stop();
    if (token_ && ledger_) ledger_->Release(token_);
    token_ = 0; ledger_ = nullptr; mayCopy_ = false;
    std::vector<snippets::Entry>().swap(entries_); std::vector<snippets::Entry>().swap(previous_);
}
void SnippetsModule::Poll() {
    if (auto reply = task_.Consume()) {
        status_ = reply->status == clipboard::TransferStatus::Ok ? IDS_SNIPPET_COPIED : IDS_COPY_FAILED;
        if (token_) ledger_->Release(token_);
        token_ = 0;
    }
}
void SnippetsModule::BeginEdit() { previous_ = entries_; dirty_ = true; }
bool SnippetsModule::InvokeWithText(const std::wstring &id, HWND owner, const std::wstring &input) {
    if (id == L"snippets.add") {
        if (!readable_ || entries_.size() >= 64 || next_ == UINT64_MAX) return false;
        auto entry = snippets::Parse(input, next_ + 1);
        if (!entry) return false;
        BeginEdit(); ++next_; entries_.push_back(std::move(*entry)); return true;
    }
    if (auto target = ItemId(id, L"snippets.edit-")) {
        auto entry = snippets::Parse(input, target);
        if (!entry) return false;
        for (auto &existing : entries_) if (existing.id == target) {
            BeginEdit(); existing = std::move(*entry); return true;
        }
        return false;
    }
    return input.empty() && Invoke(id, owner);
}
bool SnippetsModule::Invoke(const std::wstring &id, HWND) {
    Poll();
    if (id == L"snippets.open") return true;
    if (id == L"snippets.reset") { BeginEdit(); entries_.clear(); return true; }
    if (auto target = ItemId(id, L"snippets.remove-")) {
        if (std::none_of(entries_.begin(), entries_.end(), [=](const auto &entry) { return entry.id == target; })) return false;
        BeginEdit(); std::erase_if(entries_, [=](const auto &entry) { return entry.id == target; }); return true;
    }
    if (auto target = ItemId(id, L"snippets.copy-")) {
        if (!mayCopy_ || task_.Busy()) return false;
        for (const auto &entry : entries_) if (entry.id == target) {
            token_ = ledger_->Acquire(descriptor_, L"clipboard-write");
            if (!task_.Start(clipboard::TransferOp::Write, clipboard::TextPayload(entry.text), ledger_->Owner(), descriptor_.id)) {
                ledger_->Release(token_); token_ = 0; return false;
            }
            status_ = IDS_COPYING_RESULT; return true;
        }
    }
    return false;
}
std::vector<Action> SnippetsModule::Actions() const {
    std::vector<Action> result;
    for (const auto &entry : entries_) {
        auto suffix = std::to_wstring(entry.id);
        result.push_back({L"snippets.copy-" + suffix, 0, String(IDS_SNIPPET_COPY) + L" · " + entry.title});
        result.push_back({L"snippets.edit-" + suffix, 0, String(IDS_SNIPPET_EDIT) + L" · " + entry.title,
            true, true, false, IDS_SNIPPETS_HELP});
        result.push_back({L"snippets.remove-" + suffix, 0, String(IDS_SNIPPET_REMOVE) + L" · " + entry.title,
            true, false, true});
    }
    return result;
}
Panel SnippetsModule::BuildPanel(const std::wstring &query) const {
    Panel panel{IDS_SNIPPETS, {{String(IDS_SNIPPET_ADD), L"snippets.add", readable_}, {String(IDS_SNIPPETS_HELP), L""}}};
    if (!readable_) panel.rows.push_back({String(IDS_SNIPPETS_RESET), L"snippets.reset"});
    if (status_) panel.rows.push_back({String(status_), L""});
    for (const auto &entry : entries_) {
        auto searchable = entry.title + L" " + entry.text;
        if (!query.empty() && FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE,
            searchable.c_str(), -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0) < 0) continue;
        auto suffix = std::to_wstring(entry.id);
        panel.rows.push_back({String(IDS_SNIPPET_COPY) + L" · " + entry.title, L"snippets.copy-" + suffix,
            mayCopy_ && !task_.Busy()});
        panel.rows.push_back({String(IDS_SNIPPET_EDIT) + L" · " + entry.title, L"snippets.edit-" + suffix});
        panel.rows.push_back({String(IDS_SNIPPET_REMOVE) + L" · " + entry.title, L"snippets.remove-" + suffix});
    }
    return panel;
}
size_t SnippetsModule::OwnedBytes() const noexcept {
    size_t bytes = sizeof(*this) + DescriptorAllocations(descriptor_) + task_.Bytes() +
        (entries_.capacity() + previous_.capacity()) * sizeof(snippets::Entry);
    for (const auto *list : {&entries_, &previous_}) for (const auto &entry : *list)
        bytes += sizeof(wchar_t) * (entry.title.capacity() + entry.text.capacity());
    return bytes;
}
std::optional<SettingsStore::Section> SnippetsModule::PendingSettings() const {
    if (!dirty_) return {};
    return SettingsStore::Section{{L"Entries", snippets::Encode(entries_)}};
}
void SnippetsModule::SettingsSaved(bool saved) {
    if (!saved) entries_ = std::move(previous_);
    previous_.clear(); dirty_ = false;
    if (saved) readable_ = true;
    status_ = saved ? IDS_SNIPPETS_SAVED : IDS_SAVE_FAILED;
}
void RegisterSnippets(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"snippets"; descriptor.name = IDS_SNIPPETS; descriptor.summary = IDS_SNIPPETS_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM; descriptor.budget = {72, 0};
    descriptor.needs = {L"clipboard-write", L"file-write:settings"};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.settings = {{L"Entries", IDS_SNIPPETS, SettingType::Text, L"1", 0, 0, 600000}};
    descriptor.actions = {{L"snippets.open", IDS_SNIPPETS, L"", true},
        {L"snippets.reset", IDS_SNIPPETS_RESET, L"", true, false, true},
        {L"snippets.add", IDS_SNIPPET_ADD, L"", true, true, false, IDS_SNIPPETS_HELP}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<SnippetsModule>(); });
}
}
