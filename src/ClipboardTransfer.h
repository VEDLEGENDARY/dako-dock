#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace clipboard {
enum class TransferOp : uint32_t { Read = 1, Write = 2, SearchFiles = 3, ReadDrop = 4, QueryRecycleBin = 5,
                                 QueryDrives = 6, EjectDrive = 7, FixtureEcho = 100, FixtureHang = 101,
                                 FixtureFiles = 102, FixtureDrop = 103 };
enum class TransferStatus : uint32_t { Pending, Ok, Excluded, Unsupported, Changed, Failed, Cancelled, TimedOut };
struct Payload {
    UINT format = CF_UNICODETEXT;
    DWORD sequence = 0;
    std::vector<std::byte> bytes;
};
using ExtraHandler = TransferStatus (*)(TransferOp, Payload &, DWORD &);
void SetExtraHandler(ExtraHandler handler);
struct Reply {
    TransferStatus status = TransferStatus::Failed;
    Payload payload;
    SIZE_T peakBytes = 0;
    uint64_t cpu100ns = 0;
    DWORD error = 0;
    bool childExited = true;
};
inline constexpr size_t TransferLimit = 2 * 1024 * 1024;
inline constexpr size_t HelperCommitLimit = 64 * 1024 * 1024;
class TransferActivity {
    mutable std::mutex mutex_;
    HANDLE process_ = nullptr;
    uint64_t completedCpu_ = 0;
public:
    TransferActivity() = default;
    TransferActivity(const TransferActivity &) = delete;
    ~TransferActivity();
    void Attach(HANDLE);
    void Detach();
    size_t CommittedBytes() const;
    uint64_t Cpu() const;
};
// Run on a module worker. A private inherited mapping is the only payload transport.
Reply Transfer(TransferOp operation, const Payload &input, HANDLE cancel, DWORD timeout = 1000,
               const std::wstring &owner = L"clipboard", TransferActivity *activity = nullptr);
int TransferHelper(const wchar_t *handleText);
bool SessionUnlocked();
Payload TextPayload(const std::wstring &text);
std::wstring PayloadText(const Payload &payload);
bool CheckTransfer(const std::wstring &reportPath);
}
