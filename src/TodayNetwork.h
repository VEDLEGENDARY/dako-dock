#pragma once
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "PrivacyRuntime.h"

namespace today {
constexpr UINT Changed = WM_APP + 41;
struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};
inline std::wstring Utf8(const std::string& bytes) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring value(n, L' ');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), value.data(), n);
    return value;
}
inline std::wstring Escape(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, ' ');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), n, nullptr, nullptr);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : utf8) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.')
            out += static_cast<wchar_t>(c);
        else { out += L'%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
// HTTPS only, bounded response and timeouts; never log private feed URLs or response bodies.
inline std::wstring Get(const std::wstring& url, const std::atomic<bool>& stop,
                        const std::wstring& reason = L"calendar-subscription") {
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (url.size() > 8192 || !WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength) return {};
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    privacy::Network access(host, reason);
    if (!access.Allowed() || stop) return {};
    InternetHandle session(WinHttpOpen(L"Dako/3.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) return {};
    WinHttpSetTimeouts(session.value, 2500, 2500, 2500, 2500);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value || stop || !access.Allowed()) return {};
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) return {};
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) return {};
    if (stop || !access.Allowed() || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return {};
    if (stop || !access.Allowed() || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return {};
    std::string bytes;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        char buffer[8192]; DWORD read = 0;
        if (stop || !access.Allowed() || std::chrono::steady_clock::now() > deadline ||
            !WinHttpReadData(request.value, buffer, sizeof(buffer), &read)) return {};
        if (!read) break;
        bytes.append(buffer, read);
        if (bytes.size() > 2 * 1024 * 1024) return {};
    }
    if (!access.Allowed()) return {};
    access.Succeeded();
    return Utf8(bytes);
}
// Request-only workers: no periodic wakeups. UI snapshots stay available during IO.
template<class State> class Fetcher {
    std::mutex mutex_;
    State state_;
    std::thread worker_;
    std::atomic<bool> busy_{false}, stop_{false};
public:
    ~Fetcher() { stop_ = true; if (worker_.joinable()) worker_.join(); }
    State Snapshot() { std::scoped_lock lock(mutex_); return state_; }
    bool Available() { std::scoped_lock lock(mutex_); return state_.available; }
    bool Busy() const { return busy_; }
    void Request(HWND window, std::function<State(const std::atomic<bool>&)> work) {
        if (busy_.exchange(true)) return;
        if (worker_.joinable()) worker_.join();
        worker_ = std::thread([this, window, work = std::move(work)] {
            try {
                auto state = work(stop_);
                { std::scoped_lock lock(mutex_); state_ = std::move(state); }
            } catch (...) {} // Keep the last usable snapshot on malformed or unavailable data.
            busy_ = false;
            if (!stop_) PostMessageW(window, Changed, 0, 0);
        });
    }
};
}
