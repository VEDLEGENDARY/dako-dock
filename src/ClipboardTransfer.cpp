#include "ClipboardTransfer.h"
#include "ClipboardPaste.h"
#include "PrivacyRuntime.h"
#include <wtsapi32.h>
#include <psapi.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <atomic>
#include <fstream>
#include <thread>

namespace clipboard {
namespace {
constexpr uint32_t Magic = 0x444b434c, Version = 2;
std::atomic<ExtraHandler> extraHandler = nullptr;
struct Packet {
    uint32_t magic = Magic, version = Version;
    TransferOp operation = TransferOp::Read;
    TransferStatus status = TransferStatus::Pending;
    UINT format = CF_UNICODETEXT;
    DWORD sequence = 0, bytes = 0, error = 0;
    HWND pasteTarget = nullptr;
    DWORD targetProcess = 0;
    std::byte data[TransferLimit]{};
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
struct View {
    Packet *value = nullptr;
    explicit View(HANDLE mapping) {
        value = static_cast<Packet *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Packet)));
    }
    ~View() {
        if (value)
            UnmapViewOfFile(value);
    }
};
struct Formats {
    UINT excluded = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
    UINT history = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    UINT cloud = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
    UINT own = RegisterClipboardFormatW(L"Dako.Clipboard.Internal");
    bool Valid() const { return excluded && history && cloud && own; }
};
struct ClipboardLock {
    bool open;
    explicit ClipboardLock(HWND window) : open(OpenClipboard(window) != FALSE) {}
    ~ClipboardLock() {
        if (open)
            CloseClipboard();
    }
};
HGLOBAL Memory(const void *bytes, size_t size) {
    HGLOBAL result = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!result)
        return nullptr;
    void *target = GlobalLock(result);
    if (!target) {
        GlobalFree(result);
        return nullptr;
    }
    std::memcpy(target, bytes, size);
    GlobalUnlock(result);
    return result;
}
TransferStatus Read(Packet &packet) {
    if (!SessionUnlocked())
        return TransferStatus::Failed;
    Formats formats;
    if (!formats.Valid())
        return TransferStatus::Failed;
    ClipboardLock clipboard(nullptr);
    if (!clipboard.open)
        return TransferStatus::Failed;
    DWORD sequence = GetClipboardSequenceNumber();
    if (packet.sequence && packet.sequence != sequence)
        return TransferStatus::Changed;
    if (IsClipboardFormatAvailable(formats.excluded) || IsClipboardFormatAvailable(formats.own))
        return TransferStatus::Excluded;
    if (IsClipboardFormatAvailable(formats.history)) {
        HANDLE value = GetClipboardData(formats.history);
        if (!value || GlobalSize(value) != sizeof(DWORD))
            return TransferStatus::Excluded;
        const void *data = GlobalLock(value);
        if (!data)
            return TransferStatus::Excluded;
        DWORD permission = 0;
        std::memcpy(&permission, data, sizeof(permission));
        GlobalUnlock(value);
        if (permission != 1)
            return TransferStatus::Excluded;
    }
    const UINT choices[]{CF_HDROP, CF_UNICODETEXT, CF_DIBV5, CF_DIB};
    UINT selected = 0;
    for (UINT format : choices)
        if (IsClipboardFormatAvailable(format)) {
            selected = format;
            break;
        }
    if (!selected)
        return TransferStatus::Unsupported;
    HANDLE value = GetClipboardData(selected);
    SIZE_T size = value ? GlobalSize(value) : 0;
    if (!size || size > TransferLimit)
        return TransferStatus::Unsupported;
    const void *data = GlobalLock(value);
    if (!data)
        return TransferStatus::Failed;
    bool valid = true;
    if (selected == CF_UNICODETEXT) {
        valid = size % sizeof(wchar_t) == 0;
        if (valid) {
            const auto *text = static_cast<const wchar_t *>(data);
            const auto *end = std::wmemchr(text, 0, size / sizeof(wchar_t));
            valid = end != nullptr;
            if (end)
                size = static_cast<size_t>(end - text + 1) * sizeof(wchar_t);
        }
    }
    if (valid)
        std::memcpy(packet.data, data, size);
    GlobalUnlock(value);
    if (!valid)
        return TransferStatus::Unsupported;
    if (sequence != GetClipboardSequenceNumber())
        return TransferStatus::Changed;
    if (!SessionUnlocked()) {
        SecureZeroMemory(packet.data, size);
        return TransferStatus::Cancelled;
    }
    packet.sequence = sequence;
    packet.format = selected;
    packet.bytes = static_cast<DWORD>(size);
    return TransferStatus::Ok;
}
TransferStatus Write(Packet &packet) {
    if (!SessionUnlocked() || !packet.bytes || packet.bytes > TransferLimit)
        return TransferStatus::Failed;
    if (packet.format != CF_UNICODETEXT && packet.format != CF_DIB && packet.format != CF_DIBV5 &&
        packet.format != CF_HDROP)
        return TransferStatus::Unsupported;
    if (packet.format == CF_UNICODETEXT) {
        if (packet.bytes % sizeof(wchar_t))
            return TransferStatus::Unsupported;
        const auto *text = reinterpret_cast<const wchar_t *>(packet.data);
        size_t length = packet.bytes / sizeof(wchar_t);
        if (!length || text[length - 1] || std::wmemchr(text, 0, length - 1))
            return TransferStatus::Unsupported;
    }
    Formats formats;
    if (!formats.Valid())
        return TransferStatus::Failed;
    DWORD zero = 0, one = 1;
    std::array<UINT, 5> types{formats.excluded, formats.history, formats.cloud, formats.own, packet.format};
    std::array<HGLOBAL, 5> memory{Memory(&one, sizeof(one)), Memory(&zero, sizeof(zero)), Memory(&zero, sizeof(zero)),
                                  Memory(&one, sizeof(one)), Memory(packet.data, packet.bytes)};
    bool allocated = std::all_of(memory.begin(), memory.end(), [](HGLOBAL value) { return value != nullptr; });
    HWND owner = allocated ? CreateWindowExW(0, L"STATIC", L"Dako", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                             GetModuleHandleW(nullptr), nullptr)
                           : nullptr;
    bool success = false;
    if (owner) {
        ClipboardLock clipboard(owner);
        if (clipboard.open && SessionUnlocked() && EmptyClipboard()) {
            success = true;
            for (size_t i = 0; i < memory.size(); ++i) {
                if (!success || !SetClipboardData(types[i], memory[i])) {
                    success = false;
                    continue;
                }
                memory[i] = nullptr;
            }
            if (success)
                packet.sequence = GetClipboardSequenceNumber();
            if (!success)
                EmptyClipboard();
        }
    }
    if (owner)
        DestroyWindow(owner);
    for (auto value : memory)
        if (value)
            GlobalFree(value);
    return success ? TransferStatus::Ok : TransferStatus::Failed;
}
uint64_t ProcessCpu(HANDLE process) {
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &create, &exit, &kernel, &user))
        return 0;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}
} // namespace
bool SessionUnlocked() {
    LPWSTR data = nullptr;
    DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &data, &bytes))
        return false;
    bool unlocked = false;
    if (bytes >= sizeof(WTSINFOEXW)) {
        const auto *info = reinterpret_cast<const WTSINFOEXW *>(data);
        unlocked = info->Level == 1 && info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
    }
    WTSFreeMemory(data);
    return unlocked;
}
TransferActivity::~TransferActivity() { Detach(); }
void TransferActivity::Attach(HANDLE child) {
    std::lock_guard lock(mutex_);
    if (process_)
        CloseHandle(process_);
    process_ = nullptr;
    DuplicateHandle(GetCurrentProcess(), child, GetCurrentProcess(), &process_,
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, 0);
}
void TransferActivity::Detach() {
    std::lock_guard lock(mutex_);
    if (process_) {
        completedCpu_ += ProcessCpu(process_);
        CloseHandle(process_);
        process_ = nullptr;
    }
}
size_t TransferActivity::CommittedBytes() const {
    std::lock_guard lock(mutex_);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    return process_ &&
                   GetProcessMemoryInfo(process_, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))
               ? memory.PrivateUsage
               : 0;
}
uint64_t TransferActivity::Cpu() const {
    std::lock_guard lock(mutex_);
    return completedCpu_ + (process_ ? ProcessCpu(process_) : 0);
}
Payload TextPayload(const std::wstring &text) {
    Payload result;
    if (text.size() >= TransferLimit / sizeof(wchar_t) || text.find(L'\0') != std::wstring::npos)
        return result;
    result.bytes.resize((text.size() + 1) * sizeof(wchar_t));
    std::memcpy(result.bytes.data(), text.c_str(), result.bytes.size());
    return result;
}
std::wstring PayloadText(const Payload &payload) {
    if (payload.format != CF_UNICODETEXT || payload.bytes.size() < sizeof(wchar_t) ||
        payload.bytes.size() % sizeof(wchar_t) || payload.bytes.size() > TransferLimit)
        return {};
    std::wstring text(payload.bytes.size() / sizeof(wchar_t), 0);
    std::memcpy(text.data(), payload.bytes.data(), payload.bytes.size());
    if (text.back() != 0)
        return {};
    text.pop_back();
    return text.find(L'\0') == std::wstring::npos ? text : std::wstring{};
}
int TransferHelper(const wchar_t *handleText) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!*handleText || std::wcsspn(handleText, L"0123456789") != std::wcslen(handleText))
        return 2;
    wchar_t *end = nullptr;
    auto number = _wcstoui64(handleText, &end, 10);
    if (!number || !end || *end || number > UINTPTR_MAX)
        return 2;
    View view(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(number)));
    if (!view.value || view.value->magic != Magic || view.value->version != Version ||
        view.value->bytes > TransferLimit)
        return 3;
    auto &packet = *view.value;
    if (packet.operation == TransferOp::FixtureHang) {
        Sleep(INFINITE);
        return 4;
    }
    if (packet.operation == TransferOp::FixtureEcho)
        packet.status = TransferStatus::Ok;
    else if (packet.operation == TransferOp::Read)
        packet.status = Read(packet);
    else if (packet.operation == TransferOp::Write) {
        if (packet.pasteTarget && !PasteTarget(packet.pasteTarget, packet.targetProcess))
            return 5;
        packet.status = Write(packet);
        if (packet.status == TransferStatus::Ok && packet.pasteTarget &&
            !PasteFocused(packet.pasteTarget, packet.targetProcess, packet.sequence))
            packet.status = TransferStatus::Failed;
    } else if (auto extra = extraHandler.load()) {
        try {
            Payload payload{packet.format, packet.sequence, {packet.data, packet.data + packet.bytes}};
            packet.status = extra(packet.operation, payload, packet.error);
            if (packet.status == TransferStatus::Ok && payload.bytes.size() <= TransferLimit) {
                packet.format = payload.format;
                packet.sequence = payload.sequence;
                packet.bytes = static_cast<DWORD>(payload.bytes.size());
                std::memcpy(packet.data, payload.bytes.data(), payload.bytes.size());
            } else if (packet.status == TransferStatus::Ok)
                packet.status = TransferStatus::Unsupported;
        } catch (...) {
            packet.status = TransferStatus::Failed;
            packet.error = ERROR_UNHANDLED_EXCEPTION;
        }
        return 0;
    } else
        packet.status = TransferStatus::Unsupported;
    packet.error = packet.status == TransferStatus::Failed ? GetLastError() : 0;
    return 0;
}
void SetExtraHandler(ExtraHandler handler) { extraHandler = handler; }
Reply Transfer(TransferOp operation, const Payload &input, HANDLE cancel, DWORD timeout, const std::wstring &owner,
               TransferActivity *activity, HWND pasteTarget) {
    Reply reply;
    if (!cancel || !timeout || timeout > 10000 || input.bytes.size() > TransferLimit)
        return reply;
    std::optional<privacy::Write> audit;
    if (operation == TransferOp::Write)
        audit.emplace(owner, L"clipboard-write", L"Windows clipboard", L"explicit-user-copy");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(Packet), nullptr));
    if (!mapping.value) {
        reply.error = GetLastError();
        return reply;
    }
    View view(mapping.value);
    if (!view.value) {
        reply.error = GetLastError();
        return reply;
    }
    auto *packet = new (view.value) Packet{};
    packet->operation = operation;
    packet->pasteTarget = pasteTarget;
    if (pasteTarget)
        GetWindowThreadProcessId(pasteTarget, &packet->targetProcess);
    packet->sequence = input.sequence;
    packet->format = input.format;
    packet->bytes = static_cast<DWORD>(input.bytes.size());
    if (!input.bytes.empty())
        std::memcpy(packet->data, input.bytes.data(), input.bytes.size());
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.ProcessMemoryLimit = HelperCommitLimit;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        reply.error = GetLastError();
        return reply;
    }
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<std::byte> attributeStorage(attributeSize);
    auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeSize))
        return reply;
    struct AttributeCleanup {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(list); }
    } cleanup{list};
    HANDLE inherited = mapping.value;
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited), nullptr,
                                   nullptr))
        return reply;
    wchar_t executable[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768)
        return reply;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --clipboard-transfer=" +
                           std::to_wstring(reinterpret_cast<uintptr_t>(mapping.value));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                        &startup.StartupInfo, &process)) {
        reply.error = GetLastError();
        return reply;
    }
    Handle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value)) {
        reply.error = GetLastError();
        TerminateProcess(child.value, 3);
        WaitForSingleObject(child.value, 2000);
        DWORD exit = STILL_ACTIVE;
        reply.childExited = GetExitCodeProcess(child.value, &exit) && exit != STILL_ACTIVE;
        return reply;
    }
    struct ActivityScope {
        TransferActivity *activity;
        ~ActivityScope() {
            if (activity)
                activity->Detach();
        }
    } activityScope{activity};
    if (activity)
        activity->Attach(child.value);
    if (ResumeThread(thread.value) == MAXDWORD) {
        reply.error = GetLastError();
        TerminateJobObject(job.value, 3);
        WaitForSingleObject(child.value, 2000);
        DWORD exit = STILL_ACTIVE;
        reply.childExited = GetExitCodeProcess(child.value, &exit) && exit != STILL_ACTIVE;
        return reply;
    }
    HANDLE waits[]{cancel, child.value};
    ULONGLONG deadline = GetTickCount64() + timeout;
    DWORD wait = WAIT_TIMEOUT;
    bool quit = false;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        wait =
            MsgWaitForMultipleObjectsEx(2, waits, static_cast<DWORD>(deadline - now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait != WAIT_OBJECT_0 + 2)
            break;
        MSG message{};
        for (int count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                quit = true;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (quit) {
            wait = WAIT_OBJECT_0;
            break;
        }
    }
    if (wait != WAIT_OBJECT_0 + 1) {
        reply.status = wait == WAIT_OBJECT_0 ? TransferStatus::Cancelled : TransferStatus::TimedOut;
        TerminateJobObject(job.value, 4);
        WaitForSingleObject(child.value, 2000);
    } else {
        DWORD exit = 0;
        GetExitCodeProcess(child.value, &exit);
        if (!exit && packet->magic == Magic && packet->version == Version && packet->bytes <= TransferLimit &&
            static_cast<uint32_t>(packet->status) <= static_cast<uint32_t>(TransferStatus::TimedOut)) {
            reply.status = packet->status;
            reply.error = packet->error;
            if (reply.status == TransferStatus::Ok && operation != TransferOp::Write) {
                reply.payload.format = packet->format;
                reply.payload.sequence = packet->sequence;
                reply.payload.bytes.assign(packet->data, packet->data + packet->bytes);
            }
        }
    }
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(child.value, &memory, sizeof(memory)))
        reply.peakBytes = memory.PeakWorkingSetSize;
    reply.cpu100ns = ProcessCpu(child.value);
    DWORD exit = STILL_ACTIVE;
    reply.childExited = GetExitCodeProcess(child.value, &exit) && exit != STILL_ACTIVE;
    SecureZeroMemory(packet->data, sizeof(packet->data));
    if (audit)
        audit->Complete(reply.status == TransferStatus::Ok);
    return reply;
}
bool CheckTransfer(const std::wstring &reportPath) {
    privacy::Write audit(L"diagnostics", L"file-write", reportPath, L"synthetic-clipboard-helper-check");
    std::ofstream report(reportPath, std::ios::trunc);
    if (!report)
        return false;
    Handle cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!cancel.value)
        return false;
    auto payload = TextPayload(L"Synthetic helper fixture 日本語");
    auto echo = Transfer(TransferOp::FixtureEcho, payload, cancel.value, 3000, L"diagnostics");
    bool echoed =
        echo.status == TransferStatus::Ok && echo.childExited && PayloadText(echo.payload) == PayloadText(payload);
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    TransferActivity activity;
    std::atomic<bool> done = false;
    std::atomic<size_t> maximum = 0;
    std::thread sampler([&] {
        SetThreadDescription(GetCurrentThread(), L"Dako.Module.HelperCheck");
        ULONGLONG start = GetTickCount64(), observed = 0;
        while (!done && GetTickCount64() - start < 3000) {
            auto committed = activity.CommittedBytes();
            maximum = std::max(maximum.load(), committed);
            if (committed && !observed)
                observed = GetTickCount64();
            if (observed && GetTickCount64() - observed >= 200)
                break;
            Sleep(2);
        }
        SetEvent(cancel.value);
    });
    auto hung = Transfer(TransferOp::FixtureHang, {}, cancel.value, 4000, L"diagnostics", &activity);
    done = true;
    sampler.join();
    GetProcessHandleCount(GetCurrentProcess(), &after);
    bool cleared = hung.status == TransferStatus::Cancelled && hung.childExited && activity.CommittedBytes() == 0;
    bool bounded = maximum > 0 && maximum <= HelperCommitLimit;
    report << "fixture_echo=" << echoed << "\nhelper_cancelled_and_exited=" << cleared
           << "\nobserved_peak_private_commit=" << maximum.load() << "\nhelper_commit_limit=" << HelperCommitLimit
           << "\nhelper_cpu_100ns=" << activity.Cpu() << "\nhandles_before=" << before << "\nhandles_after=" << after
           << "\nclipboard_contents_read=0\nclipboard_writes=0\n";
    report.flush();
    return audit.Complete(echoed && cleared && bounded && before == after && static_cast<bool>(report));
}
} // namespace clipboard
