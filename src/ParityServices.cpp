#include "ParityServices.h"
#include "PrivacyRuntime.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <wrl/client.h>
#include <appmodel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace parity {
using Microsoft::WRL::ComPtr;
RecentDocuments::RecentDocuments(HWND owner) : owner_(owner), worker_([this] { Run(); }) {}
RecentDocuments::~RecentDocuments() {
    {
        std::scoped_lock lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    worker_.join();
}
void RecentDocuments::Request(std::wstring appId) {
    {
        std::scoped_lock lock(mutex_);
        appId_ = std::move(appId);
        pending_ = true;
        documents_.clear();
        message_ = L"Loading recent documents…";
    }
    wake_.notify_one();
}
std::vector<Document> RecentDocuments::Read(std::wstring &message) {
    std::scoped_lock lock(mutex_);
    message = message_;
    return documents_;
}
void RecentDocuments::Run() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return;
    for (;;) {
        std::wstring appId;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || pending_; });
            if (stop_)
                break;
            appId = appId_;
            pending_ = false;
        }
        std::vector<Document> documents;
        std::wstring message;
        ComPtr<IApplicationDocumentLists> lists;
        HRESULT result =
            CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&lists));
        if (appId.empty() || FAILED(result) || FAILED(lists->SetAppID(appId.c_str())))
            message = L"This application does not expose public document lists.";
        else
            for (auto kind : {ADLT_RECENT, ADLT_FREQUENT}) {
                ComPtr<IObjectArray> items;
                if (FAILED(lists->GetList(kind, 12, IID_PPV_ARGS(&items))))
                    continue;
                UINT count = 0;
                items->GetCount(&count);
                for (UINT i = 0; i < count && documents.size() < 12; ++i) {
                    {
                        std::scoped_lock lock(mutex_);
                        if (stop_ || pending_)
                            break;
                    }
                    ComPtr<IUnknown> object;
                    if (FAILED(items->GetAt(i, IID_PPV_ARGS(&object))))
                        continue;
                    Document document;
                    ComPtr<IShellItem> item;
                    ComPtr<IShellLinkW> link;
                    if (SUCCEEDED(object.As(&item))) {
                        PWSTR name = nullptr, target = nullptr;
                        item->GetDisplayName(SIGDN_NORMALDISPLAY, &name);
                        item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &target);
                        document.name = name ? name : L"";
                        document.target = target ? target : L"";
                        CoTaskMemFree(name);
                        CoTaskMemFree(target);
                    } else if (SUCCEEDED(object.As(&link))) {
                        wchar_t target[32768]{}, args[32768]{}, directory[32768]{};
                        link->GetPath(target, 32768, nullptr, SLGP_RAWPATH);
                        link->GetArguments(args, 32768);
                        link->GetWorkingDirectory(directory, 32768);
                        document.target = target;
                        document.arguments = args;
                        document.directory = directory;
                        ComPtr<IPropertyStore> properties;
                        PROPVARIANT title{};
                        if (SUCCEEDED(object.As(&properties)) && SUCCEEDED(properties->GetValue(PKEY_Title, &title)) &&
                            title.vt == VT_LPWSTR && title.pwszVal)
                            document.name = title.pwszVal;
                        PropVariantClear(&title);
                        if (document.name.empty())
                            document.name = std::filesystem::path(document.target).filename().wstring();
                    }
                    if (document.name.empty() || document.target.empty())
                        continue;
                    if (std::none_of(documents.begin(), documents.end(), [&](const auto &old) {
                            return old.target == document.target && old.arguments == document.arguments;
                        }))
                        documents.push_back(std::move(document));
                }
            }
        if (documents.empty() && message.empty())
            message = L"No public recent or frequent documents. Custom Jump List tasks are not exposed by this API.";
        {
            std::scoped_lock lock(mutex_);
            if (stop_)
                break;
            if (pending_ || appId_ != appId)
                continue;
            documents_ = std::move(documents);
            message_ = std::move(message);
        }
        PostMessageW(owner_, RecentChanged, 0, 0);
    }
    CoUninitialize();
}
void CapabilityReport(const std::wstring &path) {
    privacy::Write audit(L"diagnostics", L"file-write", path, L"native-capability-report");
    std::ofstream report(std::filesystem::path(path), std::ios::trunc);
    UINT32 count = 0;
    LONG package = GetCurrentPackageFullName(&count, nullptr);
    report << "package_identity=" << (package == APPMODEL_ERROR_NO_PACKAGE ? "absent" : "present_or_unknown") << "\n";
    try {
        auto listener = winrt::Windows::UI::Notifications::Management::UserNotificationListener::Current();
        auto status = listener.GetAccessStatus();
        report << "notification_access=" << static_cast<int>(status) << "\n";
        if (status == winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus::Allowed) {
            try {
                auto token = listener.NotificationChanged([](auto &&, auto &&) {});
                listener.NotificationChanged(token);
                report << "notification_events=supported\n";
            } catch (const winrt::hresult_error &error) {
                report << "notification_events=unavailable\nnotification_event_hresult=" << std::hex
                       << static_cast<unsigned>(error.code().value) << std::dec << "\n";
            }
        }
    } catch (const winrt::hresult_error &error) {
        report << "notification_access=unavailable\nnotification_hresult=" << std::hex
               << static_cast<unsigned>(error.code().value) << std::dec << "\n";
    }
    report << "notification_permission_requested=0\nnotification_contents_read=0\n"
           << "third_party_badge_progress_observer=no_public_getter_in_ITaskbarList3\n"
           << "custom_jump_list_tasks=no_public_third_party_enumeration\n"
           << "recent_frequent_documents=IApplicationDocumentLists\n"
           << "tray_strategy=native_status_popover_no_private_Explorer_mirror\n";
    ComPtr<IApplicationDocumentLists> lists;
    HRESULT created =
        CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&lists));
    report << "recent_api_create_hresult=" << std::hex << static_cast<unsigned>(created) << std::dec << "\n";
    if (SUCCEEDED(created)) {
        HRESULT selected = lists->SetAppID(L"Dako.Native.NonexistentFixture");
        ComPtr<IObjectArray> items;
        HRESULT query = SUCCEEDED(selected) ? lists->GetList(ADLT_RECENT, 12, IID_PPV_ARGS(&items)) : selected;
        UINT documentCount = 0;
        if (items)
            items->GetCount(&documentCount);
        report << "recent_fixture_query_hresult=" << std::hex << static_cast<unsigned>(query) << std::dec
               << "\nrecent_fixture_count=" << documentCount << "\n";
    }
    report.flush();
    audit.Complete(static_cast<bool>(report));
}
} // namespace parity
