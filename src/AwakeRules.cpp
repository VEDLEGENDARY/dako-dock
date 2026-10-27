#include "AwakeRules.h"
#include "ModuleRegistry.h"
#include <tlhelp32.h>
#include <dbt.h>
#include <wtsapi32.h>

namespace awake {
namespace {
constexpr UINT AppShown = WM_APP + 81, Facts = WM_APP + 82;
constexpr GUID AcSource{0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
bool Unlocked() {
    LPWSTR data = nullptr; DWORD bytes = 0;
    bool ok = WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
        WTSSessionInfoEx, &data, &bytes) && bytes >= sizeof(WTSINFOEXW);
    if (ok) {
        const auto &info = *reinterpret_cast<const WTSINFOEXW *>(data);
        ok = info.Level == 1 && info.Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
    }
    if (data) WTSFreeMemory(data);
    return ok;
}
}
thread_local Worker *Worker::current_ = nullptr;
bool Worker::Start(const Rules &rules, HWND owner, bool resourceCheck) {
    if (thread_ || !rules.HasCondition() || rules.apps.size() > 8) return false;
    auto apps = ParseApps(EncodeApps(rules.apps));
    if (!apps) return false;
    rules_ = rules; rules_.apps = std::move(*apps); owner_ = owner; check_ = resourceCheck;
    state_ = State::Starting;
    slow_ = false;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); frequency_ = frequency.QuadPart;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) { state_ = State::Failed; return false; }
    thread_ = CreateThread(nullptr, 0, Run, this, 0, nullptr);
    if (!thread_) { CloseHandle(stop_); stop_ = nullptr; state_ = State::Failed; return false; }
    return true;
}
void Worker::Stop() noexcept {
    if (stop_) SetEvent(stop_);
    if (thread_) {
        WaitForSingleObject(thread_, INFINITE);
        auto cpu = Cpu(); CloseHandle(thread_); thread_ = nullptr; releasedCpu_ = cpu;
    }
    if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
    Rules empty; std::swap(rules_, empty);
    owner_ = nullptr;
}
void Worker::Status(State state) {
    if (state_.exchange(state) != state && owner_) PostMessageW(owner_, modules::Changed, 0, 0);
}
uint64_t Worker::Cpu() const noexcept {
    FILETIME c{}, e{}, k{}, u{};
    if (!thread_ || !GetThreadTimes(thread_, &c, &e, &k, &u)) return releasedCpu_;
    ULARGE_INTEGER a{}, b{};
    a.LowPart = k.dwLowDateTime; a.HighPart = k.dwHighDateTime;
    b.LowPart = u.dwLowDateTime; b.HighPart = u.dwHighDateTime;
    return releasedCpu_ + a.QuadPart + b.QuadPart;
}
double Worker::HookMicroseconds() const noexcept {
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart ? hookTicks_.load() * 1000000.0 / frequency.QuadPart : 0;
}
size_t Worker::Bytes() const noexcept {
    size_t result = rules_.apps.capacity() * sizeof(std::wstring);
    for (const auto &app : rules_.apps) result += app.capacity() * sizeof(wchar_t);
    // Conservative ceiling for the bounded Toolhelp/display scratch and native stack while running.
    return result + (thread_ ? 2 * 1024 * 1024 : 0);
}
bool Worker::CheckFacts(Fact ac, Fact external, Fact app) {
    if (!check_) return false;
    return PostMessageW(window_.load(), Facts, static_cast<WPARAM>(ac),
        static_cast<LPARAM>(static_cast<unsigned>(external) | (static_cast<unsigned>(app) << 8))) != FALSE;
}
DWORD WINAPI Worker::Run(void *context) {
    SetThreadDescription(GetCurrentThread(), L"Dako.Module.KeepAwakeRules");
    auto &self = *static_cast<Worker *>(context);
    current_ = &self;
    try { self.Loop(); } catch (...) {
        self.Apply(false); self.Status(State::Failed);
    }
    current_ = nullptr;
    return 0;
}
void CALLBACK Worker::Event(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
    auto *self = current_;
    if (!self) return;
    LARGE_INTEGER begin{}, end{}; QueryPerformanceCounter(&begin);
    // No titles, accessibility objects, snapshots or process queries in this callback.
    if ((event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) && window && object == OBJID_WINDOW && !child)
        PostMessageW(self->window_.load(), AppShown, 0, reinterpret_cast<LPARAM>(window));
    self->callbacks_.fetch_add(1, std::memory_order_relaxed);
    QueryPerformanceCounter(&end);
    auto ticks = static_cast<uint64_t>(end.QuadPart - begin.QuadPart);
    if (ticks > self->hookTicks_.load(std::memory_order_relaxed)) self->hookTicks_.store(ticks, std::memory_order_relaxed);
    if (ticks * 1000 > static_cast<uint64_t>(self->frequency_)) self->slow_ = true;
}
LRESULT CALLBACK Worker::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *self = reinterpret_cast<Worker *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Worker *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) try {
        if (message == AppShown) {
            DWORD pid = 0;
            if (!self->check_ && GetWindowThreadProcessId(reinterpret_cast<HWND>(lparam), &pid)) self->ObserveProcess(pid);
            return 0;
        }
        if (message == Facts && self->check_) {
            self->ac_ = static_cast<Fact>(wparam);
            self->external_ = static_cast<Fact>(lparam & 255);
            self->checkApp_ = static_cast<Fact>((lparam >> 8) & 255);
            self->changed_ = true;
            return 0;
        }
        if (!self->check_) {
            if (message == WM_DISPLAYCHANGE || (message == WM_DEVICECHANGE && wparam == DBT_DEVNODES_CHANGED))
                self->ReadDisplays();
            if (message == WM_POWERBROADCAST) {
                if (wparam == PBT_APMSUSPEND) self->suspended_ = true;
                if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
                    self->suspended_ = !Unlocked(); self->ReadDisplays(); self->ScanProcesses();
                }
                self->ReadPower(); self->changed_ = true;
                return TRUE;
            }
            if (message == WM_WTSSESSION_CHANGE) {
                if (wparam == WTS_SESSION_LOCK) self->suspended_ = true;
                if (wparam == WTS_SESSION_UNLOCK) {
                    self->suspended_ = !Unlocked(); self->ReadPower(); self->ReadDisplays(); self->ScanProcesses();
                }
                self->changed_ = true;
                return 0;
            }
        }
    } catch (...) { self->failed_ = true; SetEvent(self->stop_); }
    return DefWindowProcW(window, message, wparam, lparam);
}
void Worker::ReadPower() {
    if (!rules_.ac) return;
    SYSTEM_POWER_STATUS status{};
    ac_ = GetSystemPowerStatus(&status) && status.ACLineStatus <= 1 ?
        (status.ACLineStatus ? Fact::Yes : Fact::No) : Fact::Unknown;
    changed_ = true;
}
void Worker::ReadDisplays() {
    if (!rules_.external) return;
    external_ = Fact::Unknown; changed_ = true;
    // The topology can change between size/query. Bound scratch and retries rather than spinning.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        UINT32 paths = 0, modes = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS ||
            paths > 128 || modes > 256) return;
        std::vector<DISPLAYCONFIG_PATH_INFO> path(paths);
        std::vector<DISPLAYCONFIG_MODE_INFO> mode(modes);
        LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, path.data(), &modes, mode.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        if (result != ERROR_SUCCESS) return;
        external_ = Fact::No;
        for (size_t i = 0; i < paths; ++i) {
            if (!path[i].targetInfo.targetAvailable) continue;
            switch (path[i].targetInfo.outputTechnology) {
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO: case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI: case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL: case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE:
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:
                external_ = Fact::Yes; return;
            default: break; // Embedded, unknown and virtual outputs never satisfy the rule.
            }
        }
        return;
    }
}
void Worker::ObserveProcess(DWORD pid) {
    if (!pid || rules_.apps.empty()) return;
    bool missing = false;
    for (size_t i = 0; i < rules_.apps.size(); ++i) missing = missing || !processes_[i];
    if (!missing) return;
    DWORD session = 0, ownSession = 0;
    if (!ProcessIdToSessionId(pid, &session) || !ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) ||
        session != ownSession) return;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return;
    wchar_t name[32768]{}; DWORD count = static_cast<DWORD>(std::size(name));
    if (QueryFullProcessImageNameW(process, 0, name, &count) && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        std::wstring_view path(name, count);
        auto base = path.substr(path.find_last_of(L"/\\") + 1);
        for (size_t i = 0; i < rules_.apps.size(); ++i) if (!processes_[i] && Equal(base, rules_.apps[i])) {
            processes_[i] = process; ++resources_; changed_ = true; return;
        }
    }
    CloseHandle(process);
}
void Worker::ScanProcesses() {
    if (rules_.apps.empty()) return;
    for (auto &process : processes_) if (process && WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        CloseHandle(process); process = nullptr; --resources_; changed_ = true;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    unsigned count = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 || ++count > 8192) break;
        if (std::any_of(rules_.apps.begin(), rules_.apps.end(), [&](const auto &name) { return Equal(name, entry.szExeFile); }))
            ObserveProcess(entry.th32ProcessID);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
}
bool Worker::Apply(bool wanted) {
    if (wanted == requested_) return true;
    if (check_) { requested_ = wanted; return true; }
    privacy::Write write(L"keep-awake", L"power-state", L"current rule worker execution request",
        wanted ? L"keep-awake-rule-matched" : L"restore-rule-worker-state");
    EXECUTION_STATE state = SetThreadExecutionState(wanted ?
        (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | (rules_.display ? ES_DISPLAY_REQUIRED : 0)) : (ES_CONTINUOUS | previous_));
    if (!write.Complete(state != 0)) return false;
    if (wanted) previous_ = state;
    requested_ = wanted;
    privacy::Policy().Unwatch(L"power-state", reinterpret_cast<uintptr_t>(this));
    if (wanted) privacy::Policy().Watch(L"power-state", reinterpret_cast<uintptr_t>(this), L"keep-awake", L"matched-rule-request");
    return true;
}
void Worker::Loop() {
    std::wstring className = L"Dako.AwakeRules." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(reinterpret_cast<uintptr_t>(this));
    WNDCLASSW type{}; type.lpfnWndProc = Procedure; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = className.c_str();
    ATOM atom = RegisterClassW(&type);
    if (atom) ++resources_;
    HWND window = atom ? CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className.c_str(), L"",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, this) : nullptr;
    if (window) ++resources_;
    window_ = window;
    HPOWERNOTIFY power = nullptr; bool session = false; HWINEVENTHOOK hook = nullptr; bool ok = false;
    try {
    power = window && rules_.ac ? RegisterPowerSettingNotification(window, &AcSource, DEVICE_NOTIFY_WINDOW_HANDLE) : nullptr;
    if (power) ++resources_;
    session = window && WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
    if (session) ++resources_;
    hook = window && !rules_.apps.empty() ? SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW,
        nullptr, Event, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS) : nullptr;
    if (hook) {
        ++resources_;
        privacy::Policy().Watch(L"window-event-hook", reinterpret_cast<uintptr_t>(hook), L"keep-awake", L"selected-desktop-app-starts");
    }
    ok = window && session && (!rules_.ac || power) && (rules_.apps.empty() || hook);
    requested_ = false; previous_ = 0; changed_ = true; failed_ = false;
    ac_ = external_ = checkApp_ = Fact::Unknown;
    suspended_ = !check_ && !Unlocked();
    if (ok && !check_) { ReadPower(); ReadDisplays(); ScanProcesses(); }
    while (ok && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        if (slow_) { ok = false; break; }
        if (changed_) {
            changed_ = false;
            Fact app = check_ ? checkApp_ : std::any_of(processes_.begin(), processes_.end(), [](HANDLE p) { return p != nullptr; }) ? Fact::Yes : Fact::No;
            bool wanted = !suspended_ && Match(rules_, ac_, external_, app);
            ok = Apply(wanted);
            Status(ok ? (wanted ? State::Active : State::Waiting) : State::Failed);
            if (!ok) break;
        }
        std::array<HANDLE, 9> handles{}; handles[0] = stop_; DWORD count = 1;
        for (HANDLE process : processes_) if (process) handles[count++] = process;
        DWORD result = MsgWaitForMultipleObjectsEx(count, handles.data(), INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0) break;
        if (result > WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) { ScanProcesses(); continue; }
        if (result != WAIT_OBJECT_0 + count) { ok = false; break; }
        MSG message{};
        for (unsigned batch = 0; batch < 128 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
            if (message.message == WM_QUIT) { SetEvent(stop_); break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    } catch (...) { ok = false; }
    ok = ok && !failed_ && !slow_;
    try { if (!Apply(false)) ok = false; } catch (...) { ok = false; }
    // Thread exit also releases an execution request if its explicit restoration failed.
    privacy::Policy().Unwatch(L"power-state", reinterpret_cast<uintptr_t>(this));
    if (hook) {
        if (UnhookWinEvent(hook)) --resources_; else ok = false;
        privacy::Policy().Unwatch(L"window-event-hook", reinterpret_cast<uintptr_t>(hook));
    }
    if (session) { if (WTSUnRegisterSessionNotification(window)) --resources_; else ok = false; }
    if (power) { if (UnregisterPowerSettingNotification(power)) --resources_; else ok = false; }
    for (auto &process : processes_) if (process) { CloseHandle(process); process = nullptr; --resources_; }
    window_ = nullptr;
    if (window) { if (DestroyWindow(window)) --resources_; else ok = false; }
    if (atom) { if (UnregisterClassW(className.c_str(), type.hInstance)) --resources_; else ok = false; }
    Status(ok ? State::Stopped : State::Failed);
}
}
