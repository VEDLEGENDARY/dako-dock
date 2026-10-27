#include "ModuleRegistry.h"
#include "ModuleStrings.h"
#include "ClipboardModule.h"
#include "ShelfModule.h"
#include <tlhelp32.h>
#include <atomic>
#include <fstream>
#include <ole2.h>

namespace modules {
namespace {
struct Apartment {
    HRESULT status = OleInitialize(nullptr);
    ~Apartment() { if (SUCCEEDED(status)) OleUninitialize(); }
};
// A real hook, timer and message-only window, scoped to this diagnostic's worker.
class Probe final : public Module {
    HANDLE thread_ = nullptr, ready_ = nullptr, ping_ = nullptr;
    DWORD threadId_ = 0;
    bool started_ = false;
    Ledger *ledger_ = nullptr;
    uint64_t lease_ = 0;
    inline static std::atomic<unsigned> windows_ = 0, hooks_ = 0, timers_ = 0, classes_ = 0;
    inline static std::atomic<ULONGLONG> callbackTicks_ = 0, callbackCount_ = 0;
    inline static LONGLONG frequency_ = 1;
    static LRESULT CALLBACK Hook(int code, WPARAM wparam, LPARAM lparam) {
        LARGE_INTEGER start{}, finish{};
        QueryPerformanceCounter(&start);
        callbackCount_.fetch_add(1, std::memory_order_relaxed);
        QueryPerformanceCounter(&finish);
        auto elapsed = static_cast<ULONGLONG>(finish.QuadPart - start.QuadPart);
        auto previous = callbackTicks_.load(std::memory_order_relaxed);
        if (elapsed > previous) callbackTicks_.store(elapsed, std::memory_order_relaxed);
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    static DWORD WINAPI Run(void *context) {
        SetThreadDescription(GetCurrentThread(), L"Dako.Module.LifecycleProbe");
        auto &self = *static_cast<Probe *>(context);
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"Dako.Module.Check";
        ATOM registered = RegisterClassW(&type);
        if (registered) ++classes_;
        HWND window = registered ? CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, type.hInstance, nullptr) : nullptr;
        HHOOK hook = window ? SetWindowsHookExW(WH_GETMESSAGE, Hook, nullptr, GetCurrentThreadId()) : nullptr;
        UINT_PTR timer = hook ? SetTimer(window, 1, 60000, nullptr) : 0;
        if (window) ++windows_;
        if (hook) ++hooks_;
        if (timer) ++timers_;
        self.started_ = window && hook && timer;
        SetEvent(self.ready_);
        if (self.started_) {
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (message.message == WM_APP + 100) SetEvent(self.ping_);
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (timer) { KillTimer(window, timer); --timers_; }
        if (hook) { UnhookWindowsHookEx(hook); --hooks_; }
        if (window) { DestroyWindow(window); --windows_; }
        if (registered && UnregisterClassW(type.lpszClassName, type.hInstance)) --classes_;
        return 0;
    }
public:
    ~Probe() override { Stop(); }
    bool Start(Ledger &ledger, const Descriptor &descriptor, const SettingsStore::Section &) override {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_) return false;
        ping_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ping_) return false;
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        frequency_ = frequency.QuadPart;
        thread_ = CreateThread(nullptr, 0, Run, this, 0, &threadId_);
        if (!thread_) return false;
        // Worker has no external IO; its startup always signals, including failures.
        WaitForSingleObject(ready_, INFINITE);
        if (!started_) return false;
        ledger_ = &ledger;
        lease_ = ledger.Acquire(descriptor, L"test:message-hook");
        return true;
    }
    void Stop() noexcept override {
        if (thread_) {
            PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (ready_) { CloseHandle(ready_); ready_ = nullptr; }
        if (ping_) { CloseHandle(ping_); ping_ = nullptr; }
        if (ledger_) ledger_->Release(lease_);
        ledger_ = nullptr;
        lease_ = 0;
        started_ = false;
    }
    bool Invoke(const std::wstring &, HWND) override {
        if (!started_) return false;
        ResetEvent(ping_);
        return PostThreadMessageW(threadId_, WM_APP + 100, 0, 0) &&
               WaitForSingleObject(ping_, 2000) == WAIT_OBJECT_0;
    }
    double HookMicroseconds() const noexcept override { return MaximumCallback(); }
    static double MaximumCallback() { return callbackTicks_.load() * 1000000.0 / frequency_; }
    static ULONGLONG CallbackCount() { return callbackCount_; }
    size_t OwnedBytes() const noexcept override { return sizeof(*this); }
    static bool Empty() { return windows_ == 0 && hooks_ == 0 && timers_ == 0 && classes_ == 0; }
};
struct Resources {
    DWORD handles = 0, gdi = 0, user = 0, threads = 0, ownedThreads = 0;
    static Resources Read() {
        Resources result;
        GetProcessHandleCount(GetCurrentProcess(), &result.handles);
        result.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        result.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 item{};
            item.dwSize = sizeof(item);
            if (Thread32First(snapshot, &item)) do {
                if (item.th32OwnerProcessID == GetCurrentProcessId()) {
                    ++result.threads;
                    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, item.th32ThreadID);
                    PWSTR description = nullptr;
                    if (thread && SUCCEEDED(GetThreadDescription(thread, &description))) {
                        if (description && std::wstring_view(description).starts_with(L"Dako.Module.")) ++result.ownedThreads;
                        LocalFree(description);
                    }
                    if (thread) CloseHandle(thread);
                }
            } while (Thread32Next(snapshot, &item));
            CloseHandle(snapshot);
        }
        return result;
    }
    bool operator==(const Resources &other) const {
        return handles == other.handles && gdi == other.gdi && user == other.user && ownedThreads == other.ownedThreads;
    }
};
}
bool Check(const std::wstring &reportPath) {
    Apartment apartment;
    if (FAILED(apartment.status)) return false;
    privacy::Write audit(L"diagnostics", L"file-write", reportPath, L"module-lifecycle-report");
    std::ofstream report(reportPath, std::ios::trunc);
    if (!report) return false;
    bool ok = true;
    auto require = [&](bool value, const char *label) {
        report << (value ? "PASS " : "FAIL ") << label << '\n';
        ok = value && ok;
    };
    Registry registry;
    registry.Diagnostic(true);
    RegisterBuiltins(registry);
    Descriptor probe;
    probe.id = L"test";
    probe.name = IDS_FEATURES;
    probe.needs = {L"test:message-hook"};
    probe.actions = {{L"test.ping", IDS_FEATURES}};
    require(registry.Add(probe, [] { return std::make_unique<Probe>(); }), "diagnostic registered");
    registry.Grant(L"test");
    require(!registry.Add(probe, [] { return std::make_unique<Probe>(); }), "duplicate module rejected");
    bool rejected = false;
    try {
        Ledger ledger;
        ledger.Acquire(probe, L"keyboard-hook");
    } catch (...) { rejected = true; }
    require(rejected, "undeclared capability rejected");
    // Warm up Windows' per-process GUI infrastructure before exact resource comparisons.
    bool warmed = true;
    for (int pass = 0; pass < 50; ++pass) for (const auto *descriptor : registry.Descriptors()) {
        registry.Grant(descriptor->id);
        warmed = registry.Enable(descriptor->id) && warmed;
        warmed = registry.Disable(descriptor->id) && warmed;
    }
    require(warmed, "all modules warm up and unload");
    // WTS brings up shared RPC infrastructure asynchronously; service the STA while it starts.
    auto settle = [](DWORD milliseconds) {
        ULONGLONG until = GetTickCount64() + milliseconds;
        while (GetTickCount64() < until) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    };
    settle(2000);
    const auto baseline = Resources::Read();
    for (const auto *descriptor : registry.Descriptors()) {
        bool cycles = true;
        for (int i = 0; i < 50; ++i) {
            registry.Grant(descriptor->id);
            cycles = registry.Enable(descriptor->id) && cycles;
            cycles = registry.Enabled(descriptor->id) && cycles;
            cycles = registry.Sample(descriptor->id).ownedBytes <= descriptor->budget.ramMiB * 1024 * 1024 && cycles;
            if (descriptor->id == L"test") {
                cycles = registry.Capabilities().Count(L"test") == 1 && cycles;
                cycles = registry.Invoke(L"test.ping", nullptr) && cycles;
            }
            cycles = registry.Disable(descriptor->id) && cycles;
            cycles = !registry.Enabled(descriptor->id) && registry.Actions().empty() &&
                registry.Sample(descriptor->id).ownedBytes == 0 && registry.Capabilities().Active().empty() &&
                Probe::Empty() && ClipboardModule::Empty() && ShelfModule::Empty() && cycles;
            cycles = !registry.Invoke(L"test.ping", nullptr) && cycles;
        }
        require(cycles, descriptor->id == L"test" ? "diagnostic 50 cycles; live hook in ledger; zero resources after stop" :
                                                  "builtin 50 cycles; actions removed on stop; RAM budget");
        auto observed = Resources::Read();
        std::string name;
        for (wchar_t letter : descriptor->id) name += static_cast<char>(letter);
        report << "after_" << name << "_handles=" << observed.handles << " threads=" << observed.threads << '\n';
    }
    const auto after = Resources::Read();
    require(after == baseline && after.ownedThreads == 0,
            "process handles, GDI, USER objects return to baseline; no module workers remain");
    report << "before_handles=" << baseline.handles << " after_handles=" << after.handles
           << " before_gdi=" << baseline.gdi << " after_gdi=" << after.gdi
           << " before_user=" << baseline.user << " after_user=" << after.user
           << " before_threads=" << baseline.threads << " after_threads=" << after.threads
           << " before_module_threads=" << baseline.ownedThreads << " after_module_threads=" << after.ownedThreads << '\n';
    report << "Windows RPC/thread-pool workers may resize independently; all owned module workers and native subscriptions must close.\n";
    require(Probe::CallbackCount() >= 50 && Probe::MaximumCallback() <= probe.budget.callbackMicroseconds,
            "hook callback work stays within its declared budget");
    report << "hook_callbacks=" << Probe::CallbackCount() << " hook_max_us=" << Probe::MaximumCallback() << '\n';
    require(registry.ApplyPreset(Preset::Taskbar) && registry.FindAction(L"windows.display") &&
        registry.FindAction(L"windows.signin") && registry.FindAction(L"windows.page"), "preset exposes builtin Settings actions");
    require(registry.ApplyPreset(Preset::Minimal) && registry.Actions().empty(), "Minimal unloads optional module");
    report << "No global input hooks, network requests or Windows settings changes were performed.\n";
    report.flush();
    return audit.Complete(ok && static_cast<bool>(report));
}
bool Measure(const std::wstring &reportPath, unsigned seconds) {
    if (seconds < 2 || seconds > 60) return false;
    Apartment apartment;
    if (FAILED(apartment.status)) return false;
    Registry registry;
    registry.Diagnostic(true);
    RegisterBuiltins(registry);
    for (const auto *descriptor : registry.Descriptors())
        if (!registry.Grant(descriptor->id) || !registry.Enable(descriptor->id)) return false;
    registry.ResetSamples();
    ULONGLONG end = GetTickCount64() + seconds * 1000ULL;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    privacy::Write audit(L"diagnostics", L"file-write", reportPath, L"module-budget-report");
    std::ofstream report(reportPath, std::ios::trunc);
    report << "module,owned_bytes,ram_budget_bytes,cpu_percent,idle_budget_percent,hook_us,hook_budget_us,pass\n";
    bool ok = true;
    for (const auto *descriptor : registry.Descriptors()) {
        auto cost = registry.Sample(descriptor->id);
        size_t limit = descriptor->budget.ramMiB * 1024 * 1024;
        bool within = cost.ownedBytes <= limit && cost.cpuPercent >= 0 &&
                      cost.cpuPercent <= descriptor->budget.idleCpuPercent &&
                      cost.hookMicroseconds <= descriptor->budget.callbackMicroseconds;
        std::string id;
        for (wchar_t item : descriptor->id) id += static_cast<char>(item);
        report << id << ',' << cost.ownedBytes << ',' << limit << ',' << cost.cpuPercent << ','
               << descriptor->budget.idleCpuPercent << ',' << cost.hookMicroseconds << ','
               << descriptor->budget.callbackMicroseconds << ',' << within << '\n';
        ok = within && ok;
    }
    report.flush();
    return audit.Complete(ok && static_cast<bool>(report));
}
}
