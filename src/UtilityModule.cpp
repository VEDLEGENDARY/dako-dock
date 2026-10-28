#include "UtilityModule.h"
#include "CommandModel.h"
#include "ModuleStrings.h"
#include <iomanip>
#include <sstream>

namespace modules {
namespace {
std::wstring Number(double value) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << (value == 0 ? 0 : value);
    return stream.str();
}
std::wstring Result(const std::wstring &input, UINT kind) {
    if (!kind || kind == IDS_CALCULATE)
        if (auto value = command::Calculator::Evaluate(input))
            return Number(*value);
    if (!kind || kind == IDS_CONVERT)
        if (auto value = command::Convert(input))
            return Number(value->value) + L" " + value->unit;
    if (!kind || kind == IDS_CLEAN_URL) {
        auto cleaned = command::CleanUrl(input);
        if (cleaned.text != input)
            return cleaned.text;
    }
    return {};
}
} // namespace
bool UtilityModule::Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) {
    ledger_ = &ledger;
    descriptor_ = descriptor;
    mayCopy_ = !ledger.Fixture() && !ledger.Preview() && !ledger.Diagnostic();
    return true;
}
void UtilityModule::Stop() noexcept {
    task_.Stop();
    if (ledger_ && token_)
        ledger_->Release(token_);
    token_ = 0;
    ledger_ = nullptr;
    mayCopy_ = false;
    query_.clear();
    result_.clear();
    expression_.clear();
    panelResult_.clear();
    panelExpression_.clear();
}
void UtilityModule::Poll() {
    if (auto reply = task_.Consume()) {
        status_ = reply->status == clipboard::TransferStatus::Ok ? IDS_COPIED_RESULT : IDS_COPY_FAILED;
        if (token_)
            ledger_->Release(token_);
        token_ = 0;
    }
}
void UtilityModule::Query(const std::wstring &query) {
    if (query_ == query)
        return;
    query_ = query.substr(0, 4096);
    ++generation_;
    result_ = Result(query_, 0);
    expression_ = query_;
}
void UtilityModule::Evaluate(const std::wstring &input, UINT kind) {
    panelExpression_ = input;
    panelResult_ = Result(input, kind);
    status_ = panelResult_.empty() ? IDS_NO_RESULT : 0;
}
bool UtilityModule::Copy(const std::wstring &value) {
    Poll();
    if (!mayCopy_ || value.empty() || task_.Busy())
        return false;
    token_ = ledger_->Acquire(descriptor_, L"clipboard-write");
    if (!token_)
        return false;
    if (!task_.Start(clipboard::TransferOp::Write, clipboard::TextPayload(value), ledger_->Owner(), descriptor_.id)) {
        ledger_->Release(token_);
        token_ = 0;
        return false;
    }
    status_ = IDS_COPYING_RESULT;
    return true;
}
bool UtilityModule::Invoke(const std::wstring &id, HWND) {
    if (id == L"utilities.open")
        return true;
    if (id == L"utilities.copy")
        return Copy(panelResult_);
    if (id == L"utilities.result-" + std::to_wstring(generation_) && !result_.empty()) {
        panelExpression_ = expression_;
        panelResult_ = result_;
        status_ = 0;
        return true;
    }
    return false;
}
bool UtilityModule::InvokeWithText(const std::wstring &id, HWND owner, const std::wstring &input) {
    UINT kind = id == L"utilities.calculate"   ? IDS_CALCULATE
                : id == L"utilities.convert"   ? IDS_CONVERT
                : id == L"utilities.clean-url" ? IDS_CLEAN_URL
                                               : 0;
    if (!kind)
        return input.empty() && Invoke(id, owner);
    Evaluate(input, kind);
    return true;
}
std::vector<Action> UtilityModule::Actions() const {
    std::vector<Action> result;
    if (!result_.empty())
        result.push_back(
            {L"utilities.result-" + std::to_wstring(generation_), 0, expression_ + L" = " + result_, true});
    if (!panelResult_.empty())
        result.push_back({L"utilities.copy", IDS_COPY_RESULT});
    return result;
}
Panel UtilityModule::BuildPanel(const std::wstring &) const {
    Panel panel{IDS_UTILITIES, {}};
    if (!panelResult_.empty()) {
        panel.rows.push_back({panelResult_, L""});
        panel.rows.push_back({String(IDS_COPY_RESULT), L"utilities.copy", mayCopy_ && !task_.Busy()});
    }
    if (!panelExpression_.empty())
        panel.rows.push_back({panelExpression_, L""});
    panel.rows.push_back({String(IDS_CALCULATE), L"utilities.calculate"});
    panel.rows.push_back({String(IDS_CONVERT), L"utilities.convert"});
    panel.rows.push_back({String(IDS_CLEAN_URL), L"utilities.clean-url"});
    if (status_)
        panel.rows.push_back({String(status_), L""});
    if (panelExpression_.empty())
        panel.rows.push_back({String(IDS_UTILITIES_HINT), L""});
    return panel;
}
size_t UtilityModule::OwnedBytes() const noexcept {
    return sizeof(*this) + DescriptorAllocations(descriptor_) + task_.Bytes() +
           sizeof(wchar_t) * (query_.capacity() + result_.capacity() + expression_.capacity() +
                              panelExpression_.capacity() + panelResult_.capacity());
}
void RegisterUtilities(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"utilities";
    descriptor.name = IDS_UTILITIES;
    descriptor.summary = IDS_UTILITIES_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM;
    descriptor.needs = {L"clipboard-write"};
    descriptor.budget = {72, 0};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"utilities.open", IDS_UTILITIES, L"", true},
                          {L"utilities.calculate", IDS_CALCULATE, L"", true, true},
                          {L"utilities.convert", IDS_CONVERT, L"", true, true},
                          {L"utilities.clean-url", IDS_CLEAN_URL, L"", true, true}};
    registry.Add(std::move(descriptor), [] { return std::make_unique<UtilityModule>(); });
}
} // namespace modules
