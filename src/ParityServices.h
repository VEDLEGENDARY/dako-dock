#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
namespace parity {
inline constexpr UINT RecentChanged = WM_APP + 61;
struct Document {
    std::wstring name, target, arguments, directory;
};
class RecentDocuments {
    HWND owner_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, pending_ = false;
    std::wstring appId_, message_;
    std::vector<Document> documents_;
    std::thread worker_;
    void Run();

  public:
    explicit RecentDocuments(HWND owner);
    ~RecentDocuments();
    void Request(std::wstring appId);
    std::vector<Document> Read(std::wstring &message);
};
void CapabilityReport(const std::wstring &path);
}
