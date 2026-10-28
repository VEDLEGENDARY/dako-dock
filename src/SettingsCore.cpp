#include "SettingsCore.h"
#include "SettingsIdentity.h"
#include <mutex>
#include <thread>

namespace v4 {
Values ReadValues(const std::wstring &path) { return Values(SettingsStore(path).Get(L"V4")); }
bool SaveValues(const Values &values, const std::wstring &path) {
    for (const auto &[key, value] : values.data) {
        auto setting = Find(key);
        if (!setting || !Valid(*setting, value))
            return false;
    }
    SettingsStore store(path);
    auto privacy = store.Get(L"Privacy");
    privacy[L"Offline"] = values.Text(L"Offline");
    auto today = store.Get(L"Today");
    today[L"WeatherCity"] = values.Text(L"WeatherCity");
    bool startupChanged = path.empty();
    HKEY startup = nullptr;
    std::wstring previous;
    bool hadPrevious = false;
    if (startupChanged) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &startup, nullptr) != ERROR_SUCCESS)
            return false;
        wchar_t buffer[32768]{};
        DWORD length = sizeof(buffer), type = 0;
        hadPrevious = RegQueryValueExW(startup, L"Dako", nullptr, &type, reinterpret_cast<BYTE *>(buffer), &length) ==
                          ERROR_SUCCESS &&
                      type == REG_SZ;
        if (hadPrevious)
            previous = buffer;
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto command = L"\"" + (std::filesystem::path(executable).parent_path() / L"Dako.exe").wstring() + L"\"";
        bool needsWrite = values.Bool(L"Startup") ? !hadPrevious || previous != command : hadPrevious;
        LSTATUS result =
            !needsWrite ? ERROR_SUCCESS
            : values.Bool(L"Startup")
                ? RegSetValueExW(startup, L"Dako", 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
                                 static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)))
                : RegDeleteValueW(startup, L"Dako");
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            RegCloseKey(startup);
            return false;
        }
    }
    bool committed = store.UpdateSections({{L"V4", values.data}, {L"Privacy", privacy}, {L"Today", today}});
    if (startup) {
        if (!committed) {
            if (hadPrevious)
                RegSetValueExW(startup, L"Dako", 0, REG_SZ, reinterpret_cast<const BYTE *>(previous.c_str()),
                               static_cast<DWORD>((previous.size() + 1) * sizeof(wchar_t)));
            else
                RegDeleteValueW(startup, L"Dako");
        }
        RegCloseKey(startup);
    }
    if (!committed)
        return false;
    if (path.empty())
        SignalSettings();
    return true;
}
void SignalSettings(const std::wstring &channel) {
    UserSecurity security;
    if (!security.descriptor)
        return;
    HANDLE event = CreateEventW(&security.attributes, FALSE, FALSE, (EventName() + channel).c_str());
    if (event) {
        SetEvent(event);
        CloseHandle(event);
    }
}
bool SendPreview(const Preview &packet, const std::wstring &channel) {
    HANDLE pipe = CreateFileW((PipeName() + channel).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(pipe, &packet, sizeof(packet), &written, nullptr) && written == sizeof(packet);
    CloseHandle(pipe);
    return ok;
}
struct SettingsWatch::Impl {
    HWND owner;
    std::wstring channel;
    HANDLE clientProcess = nullptr, clientWait = nullptr;
    HANDLE changed = nullptr, wait = nullptr, stop = nullptr;
    std::mutex mutex;
    std::optional<Preview> preview;
    std::thread worker;
    explicit Impl(HWND window, std::wstring suffix) : owner(window), channel(std::move(suffix)) {
        UserSecurity security;
        if (!security.descriptor)
            return;
        changed = CreateEventW(&security.attributes, FALSE, FALSE, (EventName() + channel).c_str());
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!changed || !stop)
            return;
        RegisterWaitForSingleObject(
            &wait, changed,
            [](PVOID context, BOOLEAN) {
                auto self = static_cast<Impl *>(context);
                PostMessageW(self->owner, SettingsChanged, 0, 0);
            },
            this, INFINITE, WT_EXECUTEDEFAULT);
        worker = std::thread([this] { Run(); });
    }
    bool Await(HANDLE pipe, OVERLAPPED &operation) {
        HANDLE events[]{stop, operation.hEvent};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            DWORD count = 0;
            GetOverlappedResult(pipe, &operation, &count, TRUE);
            return false;
        }
        return true;
    }
    void Run() {
        UserSecurity security;
        if (!security.descriptor)
            return;
        while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
            HANDLE pipe =
                CreateNamedPipeW((PipeName() + channel).c_str(),
                                 PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                 sizeof(Preview), sizeof(Preview), 0, &security.attributes);
            if (pipe == INVALID_HANDLE_VALUE)
                return;
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!operation.hEvent) {
                CloseHandle(pipe);
                return;
            }
            bool connected = ConnectNamedPipe(pipe, &operation) != FALSE;
            DWORD error = connected ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                connected = true;
                SetEvent(operation.hEvent);
            } else if (error == ERROR_IO_PENDING)
                connected = Await(pipe, operation);
            if (connected && WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
                ResetEvent(operation.hEvent);
                Preview packet{};
                DWORD count = 0;
                bool read = ReadFile(pipe, &packet, sizeof(packet), &count, &operation) != FALSE;
                if (!read && GetLastError() == ERROR_IO_PENDING && Await(pipe, operation))
                    read = GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
                if (read && count == sizeof(packet) && packet.magic == 0x44414B34 && packet.command <= 1 &&
                    packet.key[63] == 0 && packet.value[1024] == 0) {
                    auto setting = Find(packet.key);
                    if (packet.command == 1 || (setting && setting->live && Valid(*setting, packet.value))) {
                        if (clientWait) {
                            UnregisterWaitEx(clientWait, INVALID_HANDLE_VALUE);
                            clientWait = nullptr;
                        }
                        if (clientProcess) {
                            CloseHandle(clientProcess);
                            clientProcess = nullptr;
                        }
                        {
                            std::scoped_lock lock(mutex);
                            preview = packet;
                        }
                        PostMessageW(owner, SettingsChanged, 1, 0);
                        if (packet.command == 0) {
                            ULONG process = 0;
                            if (GetNamedPipeClientProcessId(pipe, &process))
                                clientProcess = OpenProcess(SYNCHRONIZE, FALSE, process);
                            if (clientProcess)
                                RegisterWaitForSingleObject(
                                    &clientWait, clientProcess,
                                    [](PVOID context, BOOLEAN) {
                                        auto self = static_cast<Impl *>(context);
                                        PostMessageW(self->owner, SettingsChanged, 2, 0);
                                    },
                                    this, INFINITE, WT_EXECUTEONLYONCE);
                            else
                                PostMessageW(owner, SettingsChanged, 2, 0);
                        }
                    }
                }
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(operation.hEvent);
            CloseHandle(pipe);
        }
    }
    ~Impl() {
        if (stop)
            SetEvent(stop);
        if (wait)
            UnregisterWaitEx(wait, INVALID_HANDLE_VALUE);
        if (worker.joinable())
            worker.join();
        if (clientWait)
            UnregisterWaitEx(clientWait, INVALID_HANDLE_VALUE);
        if (clientProcess)
            CloseHandle(clientProcess);
        if (changed)
            CloseHandle(changed);
        if (stop)
            CloseHandle(stop);
    }
};
SettingsWatch::SettingsWatch(HWND owner, std::wstring channel)
    : impl_(std::make_unique<Impl>(owner, std::move(channel))) {}
SettingsWatch::~SettingsWatch() = default;
std::optional<Preview> SettingsWatch::Consume() {
    std::scoped_lock lock(impl_->mutex);
    auto value = impl_->preview;
    impl_->preview.reset();
    return value;
}
} // namespace v4
