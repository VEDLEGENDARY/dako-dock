#include "FileSearchModule.h"
#include "ModuleStrings.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <cwctype>

namespace modules {
bool FileSearchModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) {
    ledger_ = &ledger; descriptor_ = descriptor;
    fixture_ = ledger.Fixture() || ledger.Diagnostic(); launch_ = !fixture_;
    status_ = IDS_FILES_HINT;
    return true;
}
void FileSearchModule::Stop() noexcept {
    launch_ = false;
    task_.Stop();
    if (token_ && ledger_) ledger_->Release(token_);
    token_ = 0; ledger_ = nullptr;
    std::vector<files::Result>().swap(results_);
    query_.clear(); viewQuery_.clear();
}
void FileSearchModule::Search(std::wstring query) {
    while (!query.empty() && std::iswspace(query.front())) query.erase(query.begin());
    while (!query.empty() && std::iswspace(query.back())) query.pop_back();
    if (query.size() > 256) query.clear();
    if (query == query_) return;
    query_ = std::move(query);
    generation_ = ++sequence_; results_.clear(); task_.Cancel();
    status_ = IDS_FILES_HINT;
    if (!files::Sql(query_)) return;
    if (fixture_) {
        results_ = {{L"Synthetic " + query_ + L".txt", L"C:\\DakoFixture\\report.txt"}};
        status_ = IDS_FILES_FIXTURE;
    } else if (!task_.Busy()) StartQuery();
    else status_ = IDS_FILES_LOADING;
}
void FileSearchModule::StartQuery() {
    if (!ledger_ || fixture_ || task_.Busy() || !files::Sql(query_)) return;
    token_ = ledger_->Acquire(descriptor_, L"file-index-read");
    runningGeneration_ = generation_;
    if (!task_.Start(clipboard::TransferOp::SearchFiles, clipboard::TextPayload(query_), ledger_->Owner(), descriptor_.id, 5000, 200)) {
        ledger_->Release(token_); token_ = 0; status_ = IDS_FILES_ERROR; return;
    }
    status_ = IDS_FILES_LOADING;
}
void FileSearchModule::Poll() {
    if (auto reply = task_.Consume()) {
        if (token_) ledger_->Release(token_);
        token_ = 0;
        if (runningGeneration_ == generation_ && !query_.empty()) {
            auto results = reply->status == clipboard::TransferStatus::Ok ? files::Decode(reply->payload.bytes) : std::nullopt;
            if (results) { results_ = std::move(*results); status_ = results_.empty() ? IDS_FILES_EMPTY : 0; }
            else status_ = IDS_FILES_ERROR;
        }
        if (runningGeneration_ != generation_ && files::Sql(query_)) StartQuery();
    }
}
void FileSearchModule::Query(const std::wstring &query) {
    auto prefix = String(IDS_FILES_KEYWORD) + L" ";
    if (query.size() >= prefix.size() && !_wcsnicmp(query.c_str(), prefix.c_str(), prefix.size())) Search(query.substr(prefix.size()));
    else Search(L"");
}
void FileSearchModule::ViewQuery(const std::wstring &query) {
    if (query != viewQuery_) { viewQuery_ = query; Search(query); }
}
bool FileSearchModule::InvokeWithText(const std::wstring &id, HWND owner, const std::wstring &input) {
    if (id == L"files.search") { viewQuery_.clear(); Search(input); return true; }
    return input.empty() && Invoke(id, owner);
}
bool FileSearchModule::Invoke(const std::wstring &id, HWND owner) {
    if (id == L"files.open") return true;
    if (!launch_) return false;
    if (id == L"files.pick") {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
        dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(String(IDS_FILES_PICK).c_str());
        if (FAILED(dialog->Show(owner))) return true;
        Microsoft::WRL::ComPtr<IShellItem> selected;
        PWSTR path = nullptr;
        if (FAILED(dialog->GetResult(&selected)) || FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
        std::wstring file(path); CoTaskMemFree(path);
        if (!files::LocalPath(file)) return false;
        auto explanation = String(IDS_FILES_RUN_WARNING) + L"\n\n" + file;
        if (files::ConfirmOpen(file) && MessageBoxW(owner, explanation.c_str(), String(IDS_FILES_PICK).c_str(),
            MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return true;
        return launch_ && reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
    for (size_t index = 0; index < results_.size(); ++index) {
        auto action = L"files.result-" + std::to_wstring(generation_) + L"-" + std::to_wstring(index);
        if (id != action) continue;
        const auto path = results_[index].path; // Shell providers can pump messages and remove the module.
        return files::LocalPath(path) && reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
    return false;
}
std::vector<Action> FileSearchModule::Actions() const {
    std::vector<Action> actions;
    for (size_t index = 0; index < results_.size(); ++index) {
        const auto &result = results_[index];
        Action action{L"files.result-" + std::to_wstring(generation_) + L"-" + std::to_wstring(index), 0,
            String(IDS_FILES_KEYWORD) + L" " + query_ + L" · " + result.name + L" · " + result.path, false, false,
            files::ConfirmOpen(result.path), IDS_FILES_RUN_WARNING};
        action.details = result.path;
        actions.push_back(std::move(action));
    }
    return actions;
}
Panel FileSearchModule::BuildPanel(const std::wstring &) const {
    Panel panel{IDS_FILES, {}};
    for (size_t index = 0; index < results_.size(); ++index)
        panel.rows.push_back({results_[index].name + L" · " + results_[index].path,
            L"files.result-" + std::to_wstring(generation_) + L"-" + std::to_wstring(index), launch_});
    if (status_) panel.rows.push_back({String(status_), L""});
    panel.rows.push_back({String(IDS_FILES_SEARCH), L"files.search"});
    panel.rows.push_back({String(IDS_FILES_PICK), L"files.pick", launch_});
    return panel;
}
size_t FileSearchModule::OwnedBytes() const noexcept {
    size_t bytes = sizeof(*this) + DescriptorAllocations(descriptor_) + task_.Bytes() +
        (query_.capacity() + viewQuery_.capacity()) * sizeof(wchar_t) + results_.capacity() * sizeof(files::Result);
    for (const auto &result : results_) bytes += (result.name.capacity() + result.path.capacity()) * sizeof(wchar_t);
    return bytes;
}
void RegisterFiles(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"files"; descriptor.name = IDS_FILES; descriptor.summary = IDS_FILES_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM; descriptor.needs = {L"file-index-read"}; descriptor.budget = {72, 0};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"files.open", IDS_FILES, L"", true},
        {L"files.search", IDS_FILES_SEARCH, L"", true, true, false, IDS_FILES_INPUT}, {L"files.pick", IDS_FILES_PICK}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<FileSearchModule>(); });
}
}
