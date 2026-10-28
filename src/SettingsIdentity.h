#pragma once
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace v4 {
inline std::wstring UserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    bool ok = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes) != FALSE;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid))
        return {};
    std::wstring result(sid);
    LocalFree(sid);
    return result;
}
inline std::wstring EventName() { return L"Local\\Dako.SettingsChanged." + UserSid(); }
inline std::wstring PipeName() { return L"\\\\.\\pipe\\Dako.SettingsPreview." + UserSid(); }
struct UserSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    UserSecurity() {
        auto sid = UserSid();
        if (!sid.empty() &&
            ConvertStringSecurityDescriptorToSecurityDescriptorW((L"D:P(A;;GA;;;SY)(A;;GA;;;" + sid + L")").c_str(),
                                                                 SDDL_REVISION_1, &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~UserSecurity() {
        if (descriptor)
            LocalFree(descriptor);
    }
};
class StoreLock {
    HANDLE mutex_ = nullptr;
    bool acquired_ = false;

  public:
    StoreLock() {
        UserSecurity security;
        if (!security.descriptor)
            return;
        auto name = L"Local\\Dako.SettingsStore." + UserSid();
        mutex_ = CreateMutexW(&security.attributes, FALSE, name.c_str());
        if (mutex_) {
            DWORD result = WaitForSingleObject(mutex_, 5000);
            acquired_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~StoreLock() {
        if (acquired_)
            ReleaseMutex(mutex_);
        if (mutex_)
            CloseHandle(mutex_);
    }
    explicit operator bool() const { return acquired_; }
};
} // namespace v4
