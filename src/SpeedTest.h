#pragma once
#include "TodayNetwork.h"
#include <vector>

class SpeedTest {
  public:
    struct Result {
        double down = 0, up = 0;
        bool valid = false, busy = false;
        std::wstring message;
    };

  private:
    std::mutex mutex_;
    Result result_;
    std::atomic<bool> stop_ = false;
    std::thread worker_;
    HWND owner_;
    bool Transfer(bool upload, double &rate) {
        privacy::Network access(L"speed.cloudflare.com", upload ? L"speed-test-upload" : L"speed-test-download");
        if (!access.Allowed() || stop_)
            return false;
        today::InternetHandle session(
            WinHttpOpen(L"Dako/4.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
        if (!session.value)
            return false;
        WinHttpSetTimeouts(session.value, 2500, 2500, 2500, 2500);
        today::InternetHandle connection(
            WinHttpConnect(session.value, L"speed.cloudflare.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.value)
            return false;
        today::InternetHandle request(WinHttpOpenRequest(
            connection.value, upload ? L"POST" : L"GET", upload ? L"/__up" : L"/__down?bytes=8388608", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.value)
            return false;
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)))
            return false;
        std::vector<BYTE> payload(upload ? 1048576 : 0, 0);
        auto begin = std::chrono::steady_clock::now();
        if (stop_ || !access.Allowed() ||
            !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                payload.empty() ? WINHTTP_NO_REQUEST_DATA : payload.data(),
                                static_cast<DWORD>(payload.size()), static_cast<DWORD>(payload.size()), 0) ||
            !WinHttpReceiveResponse(request.value, nullptr))
            return false;
        DWORD status = 0, length = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) ||
            status != 200)
            return false;
        BYTE buffer[65536];
        size_t total = 0;
        for (;;) {
            DWORD count = 0;
            if (stop_ || !access.Allowed() || std::chrono::steady_clock::now() - begin > std::chrono::seconds(15) ||
                !WinHttpReadData(request.value, buffer, sizeof(buffer), &count))
                return false;
            if (!count)
                break;
            total += count;
            if (total > 8388608)
                return false;
        }
        if (!upload && total != 8388608)
            return false;
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        rate = static_cast<double>(upload ? payload.size() : total) * 8 / std::max(.001, seconds) / 1000000;
        return access.Allowed() && !stop_;
    }

  public:
    explicit SpeedTest(HWND owner) : owner_(owner) {}
    ~SpeedTest() {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
    }
    Result Snapshot() {
        std::scoped_lock lock(mutex_);
        return result_;
    }
    void Start() {
        {
            std::scoped_lock lock(mutex_);
            if (result_.busy)
                return;
            result_.busy = true;
            result_.message = L"Testing…";
        }
        if (worker_.joinable())
            worker_.join();
        stop_ = false;
        worker_ = std::thread([this] {
            double down = 0, up = 0;
            bool ok = Transfer(false, down) && Transfer(true, up);
            {
                std::scoped_lock lock(mutex_);
                result_.busy = false;
                if (ok) {
                    result_.down = down;
                    result_.up = up;
                    result_.valid = true;
                }
                result_.message =
                    ok ? L"Cloudflare · 9 MiB sample" : L"Unavailable or cancelled. Previous result retained.";
            }
            PostMessageW(owner_, WM_APP + 85, 0, 0);
        });
    }
};
