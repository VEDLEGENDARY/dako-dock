#include "WindowsShortcuts.h"
#include "ModuleStrings.h"
#include "UtilityModule.h"
#include "ClipboardModule.h"
#include "SnippetsModule.h"
#include "KeepAwakeModule.h"
#include "FileSearchModule.h"
#include "ShelfModule.h"
#include "SettingsPageModel.h"
#include "RecycleBinModule.h"
#include "DrivesModule.h"
#include <shellapi.h>

namespace modules {
namespace {
struct Page {
    const wchar_t *action, *page;
    UINT label;
};
constexpr Page pages[]{{L"windows.display", L"display", IDS_DISPLAY_SETTINGS},
                       {L"windows.storage", L"storagesense", IDS_STORAGE_SETTINGS},
                       {L"windows.update", L"windowsupdate", IDS_UPDATE_SETTINGS},
                       {L"windows.sound", L"sound", IDS_SETTINGS_SOUND},
                       {L"windows.bluetooth", L"bluetooth", IDS_SETTINGS_BLUETOOTH},
                       {L"windows.wifi", L"network-wifi", IDS_SETTINGS_WIFI},
                       {L"windows.network", L"network-status", IDS_SETTINGS_NETWORK},
                       {L"windows.vpn", L"network-vpn", IDS_SETTINGS_VPN},
                       {L"windows.proxy", L"network-proxy", IDS_SETTINGS_PROXY},
                       {L"windows.apps", L"appsfeatures", IDS_SETTINGS_APPS},
                       {L"windows.startup", L"startupapps", IDS_SETTINGS_STARTUP},
                       {L"windows.defaults", L"defaultapps", IDS_SETTINGS_DEFAULTS},
                       {L"windows.power", L"powersleep", IDS_SETTINGS_POWER},
                       {L"windows.battery", L"batterysaver", IDS_SETTINGS_BATTERY},
                       {L"windows.notifications", L"notifications", IDS_SETTINGS_NOTIFICATIONS},
                       {L"windows.about", L"about", IDS_SETTINGS_ABOUT},
                       {L"windows.privacy", L"privacy", IDS_SETTINGS_PRIVACY},
                       {L"windows.narrator", L"easeofaccess-narrator", IDS_SETTINGS_NARRATOR},
                       {L"windows.keyboard", L"easeofaccess-keyboard", IDS_SETTINGS_KEYBOARD},
                       {L"windows.text", L"easeofaccess-display", IDS_SETTINGS_TEXT},
                       {L"windows.contrast", L"easeofaccess-highcontrast", IDS_SETTINGS_CONTRAST},
                       {L"windows.background", L"personalization-background", IDS_SETTINGS_BACKGROUND},
                       {L"windows.colors", L"personalization-colors", IDS_SETTINGS_COLORS},
                       {L"windows.themes", L"themes", IDS_SETTINGS_THEMES},
                       {L"windows.taskbar", L"taskbar", IDS_SETTINGS_TASKBAR},
                       {L"windows.recovery", L"recovery", IDS_SETTINGS_RECOVERY},
                       {L"windows.troubleshoot", L"troubleshoot", IDS_SETTINGS_TROUBLESHOOT},
                       {L"windows.time", L"dateandtime", IDS_SETTINGS_TIME},
                       {L"windows.language", L"regionlanguage", IDS_SETTINGS_LANGUAGE},
                       {L"windows.region", L"regionformatting", IDS_SETTINGS_REGION},
                       {L"windows.account", L"yourinfo", IDS_SETTINGS_ACCOUNT},
                       {L"windows.signin", L"signinoptions", IDS_SETTINGS_SIGNIN}};
bool Open(HWND owner, std::wstring uri) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
} // namespace
bool WindowsShortcuts::Start(Ledger &ledger, const Descriptor &, const SettingsStore::Section &) {
    launch_ = !ledger.Fixture() && !ledger.Diagnostic();
    return true;
}
void WindowsShortcuts::Stop() noexcept {
    launch_ = false;
    uri_.clear();
}
bool WindowsShortcuts::Invoke(const std::wstring &action, HWND owner) {
    if (action == L"windows.open")
        return true;
    if (!launch_)
        return false;
    if (!uri_.empty() && action == L"windows.page-result-" + std::to_wstring(generation_))
        return Open(owner, uri_);
    for (const auto &page : pages)
        if (action == page.action)
            return Open(owner, L"ms-settings:" + std::wstring(page.page));
    return false;
}
bool WindowsShortcuts::InvokeWithText(const std::wstring &id, HWND owner, const std::wstring &input) {
    if (id != L"windows.page")
        return input.empty() && Invoke(id, owner);
    auto uri = settingspage::Uri(input);
    if (!uri || !launch_)
        return false;
    auto explanation = String(IDS_SETTINGS_URI_CONFIRM) + L"\n\n" + *uri;
    if (MessageBoxW(owner, explanation.c_str(), String(IDS_SETTINGS_PAGE).c_str(),
                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
        return true;
    return launch_ && Open(owner, *uri);
}
void WindowsShortcuts::Query(const std::wstring &query) {
    auto uri = settingspage::Prefix(query) ? settingspage::Uri(query) : std::nullopt;
    auto next = uri ? *uri : std::wstring{};
    if (next != uri_) {
        uri_ = std::move(next);
        generation_ = ++sequence_;
    }
}
std::vector<Action> WindowsShortcuts::Actions() const {
    if (uri_.empty())
        return {};
    Action action{L"windows.page-result-" + std::to_wstring(generation_),
                  0,
                  uri_ + L" · " + String(IDS_SETTINGS_PAGE),
                  false,
                  false,
                  true,
                  IDS_SETTINGS_URI_CONFIRM};
    action.details = uri_;
    return {std::move(action)};
}
Panel WindowsShortcuts::BuildPanel(const std::wstring &query) const {
    Panel panel{IDS_SHORTCUTS, {}};
    for (const auto &action : Actions())
        panel.rows.push_back({action.text, action.id, launch_});
    for (const auto &page : pages) {
        auto text = String(page.label);
        if (query.empty() || FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE, text.c_str(),
                                             -1, query.c_str(), -1, nullptr, nullptr, nullptr, 0) >= 0)
            panel.rows.push_back({std::move(text), page.action});
    }
    if (query.empty()) {
        panel.rows.push_back({String(IDS_SETTINGS_PAGE), L"windows.page"});
        panel.rows.push_back({String(IDS_SETTINGS_SCOPE), L""});
    }
    return panel;
}
void RegisterBuiltins(Registry &registry) {
    Descriptor descriptor;
    descriptor.id = L"windows";
    descriptor.name = IDS_SHORTCUTS;
    descriptor.summary = IDS_SHORTCUTS_SUMMARY;
    descriptor.category = IDS_CATEGORY_SYSTEM;
    descriptor.budget = {1, 0};
    descriptor.surfaces = {Surface::Palette, Surface::Island};
    descriptor.actions = {{L"windows.open", IDS_SHORTCUTS, L"", true},
                          {L"windows.page", IDS_SETTINGS_PAGE, L"", false, true, false, IDS_SETTINGS_URI_INPUT}};
    for (const auto &page : pages)
        descriptor.actions.push_back({page.action, page.label});
    registry.Add(std::move(descriptor), [] { return std::make_unique<WindowsShortcuts>(); });
    RegisterUtilities(registry);
    RegisterClipboard(registry);
    RegisterSnippets(registry);
    RegisterKeepAwake(registry);
    RegisterFiles(registry);
    RegisterShelf(registry);
    RegisterRecycleBin(registry);
    RegisterDrives(registry);
}
} // namespace modules
