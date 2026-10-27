#pragma once
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_2.h>
#include <tlhelp32.h>
#include "TelemetryModel.h"

constexpr UINT HardwareChanged = WM_APP + 27;
struct ProcessSample {
    std::wstring name;
    double workingMiB = 0;
    DWORD identity = 0;
    double cpu = 0;
    bool cpuValid = false;
};
struct VolumeSample {
    std::wstring root;
    double totalGiB = 0, usedGiB = 0;
    bool valid = false;
};
struct HardwareSnapshot {
    std::wstring cpu = L"Processor", gpu = L"Graphics";
    double gpuPercent = 0, vramGiB = 0, totalGiB = 0;
    DWORD processes = 0;
    bool gpuValid = false, batteryPresent = false;
    int batterySeconds = -1;
    ULONGLONG uptimeSeconds = 0;
    std::vector<ProcessSample> top, memoryTop;
    std::vector<VolumeSample> volumes;
    telemetry::History history;
};
class HardwareStats {
    HWND window_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stop_ = false, requested_ = false;
    HardwareSnapshot state_;
    std::thread worker_;
    telemetry::ProcessDeltas processDeltas_;
    ULONGLONG idle_ = 0, kernel_ = 0, user_ = 0, received_ = 0, sent_ = 0, sampled_ = 0;
    static ULONGLONG Ticks(FILETIME time) {
        return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }
    void Collect(HardwareSnapshot &next) {
        auto now = GetTickCount64();
        bool continuous = sampled_ && now > sampled_ && now - sampled_ <= 2500;
        telemetry::Point point;
        FILETIME idle{}, kernel{}, user{};
        if (GetSystemTimes(&idle, &kernel, &user)) {
            auto i = Ticks(idle), k = Ticks(kernel), u = Ticks(user);
            auto total = (k - kernel_) + (u - user_);
            point.cpuValid = continuous && kernel_ && total && i >= idle_;
            if (point.cpuValid)
                point.cpu = std::clamp(100.0 * (1.0 - static_cast<double>(i - idle_) / total), 0.0, 100.0);
            idle_ = i;
            kernel_ = k;
            user_ = u;
        }
        MEMORYSTATUSEX memory{sizeof(memory)};
        point.ramValid = GlobalMemoryStatusEx(&memory) != FALSE;
        if (point.ramValid) {
            point.ram = memory.dwMemoryLoad;
            next.totalGiB = memory.ullTotalPhys / 1073741824.0;
        }
        PMIB_IF_TABLE2 interfaces = nullptr;
        if (GetIfTable2(&interfaces) == NO_ERROR) {
            ULONGLONG receive = 0, send = 0;
            for (ULONG i = 0; i < interfaces->NumEntries; ++i) {
                const auto &row = interfaces->Table[i];
                if (row.OperStatus == IfOperStatusUp && row.InterfaceAndOperStatusFlags.HardwareInterface) {
                    receive += row.InOctets;
                    send += row.OutOctets;
                }
            }
            point.networkValid = continuous && receive >= received_ && send >= sent_;
            if (point.networkValid) {
                point.receive = (receive - received_) * 1000.0 / (now - sampled_);
                point.send = (send - sent_) * 1000.0 / (now - sampled_);
            }
            received_ = receive;
            sent_ = send;
            FreeMibTable(interfaces);
        }
        sampled_ = now;
        next.history.Push(now, point);
        next.uptimeSeconds = now / 1000;
        SYSTEM_POWER_STATUS power{};
        if (GetSystemPowerStatus(&power)) {
            next.batteryPresent = power.BatteryFlag != 128 && power.BatteryFlag != 255;
            next.batterySeconds = power.BatteryLifeTime == MAXDWORD ? -1 : static_cast<int>(power.BatteryLifeTime);
        }
        next.volumes.clear();
        wchar_t volumeName[1024]{};
        HANDLE volumes = FindFirstVolumeW(volumeName, 1024);
        if (volumes != INVALID_HANDLE_VALUE) {
            do {
                if (next.volumes.size() >= 128)
                    break;
                if (GetDriveTypeW(volumeName) != DRIVE_FIXED)
                    continue;
                DWORD required = 0;
                GetVolumePathNamesForVolumeNameW(volumeName, nullptr, 0, &required);
                std::vector<wchar_t> paths(std::min(std::max(required, 1UL), 32768UL));
                bool mounted = GetVolumePathNamesForVolumeNameW(volumeName, paths.data(),
                                                                static_cast<DWORD>(paths.size()), &required) &&
                               paths.front();
                VolumeSample volume;
                if (mounted)
                    volume.root = paths.data();
                else {
                    wchar_t label[256]{};
                    GetVolumeInformationW(volumeName, label, 256, nullptr, nullptr, nullptr, nullptr, 0);
                    volume.root = label[0] ? std::wstring(label) : L"Volume " + std::wstring(volumeName).substr(11, 8);
                }
                ULARGE_INTEGER available{}, total{}, free{};
                volume.valid = GetDiskFreeSpaceExW(volumeName, &available, &total, &free) && total.QuadPart;
                if (volume.valid) {
                    volume.totalGiB = total.QuadPart / 1073741824.0;
                    volume.usedGiB = (total.QuadPart - std::min(total.QuadPart, free.QuadPart)) / 1073741824.0;
                }
                next.volumes.push_back(std::move(volume));
            } while (FindNextVolumeW(volumes, volumeName, 1024));
            FindVolumeClose(volumes);
        }
        std::vector<ProcessSample> processes;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        next.processes = 0;
        processDeltas_.Begin();
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{sizeof(entry)};
            if (Process32FirstW(snapshot, &entry))
                do {
                    if (++next.processes > 4096)
                        break;
                    HANDLE process =
                        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
                    if (!process)
                        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                    if (!process)
                        continue;
                    ProcessSample item{entry.szExeFile, 0, entry.th32ProcessID};
                    if (item.name.ends_with(L".exe"))
                        item.name.resize(item.name.size() - 4);
                    PROCESS_MEMORY_COUNTERS_EX memoryCounters{};
                    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memoryCounters),
                                             sizeof(memoryCounters)))
                        item.workingMiB = memoryCounters.WorkingSetSize / 1048576.0;
                    FILETIME created{}, exited{}, processKernel{}, processUser{};
                    if (GetProcessTimes(process, &created, &exited, &processKernel, &processUser)) {
                        auto value = processDeltas_.Sample(
                            item.identity, {Ticks(created), Ticks(processKernel) + Ticks(processUser), now},
                            info.dwNumberOfProcessors);
                        item.cpuValid = value.has_value();
                        item.cpu = value.value_or(0);
                    }
                    CloseHandle(process);
                    processes.push_back(std::move(item));
                } while (Process32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }
        processDeltas_.End();
        std::sort(processes.begin(), processes.end(), [](const auto &a, const auto &b) {
            return a.workingMiB != b.workingMiB ? a.workingMiB > b.workingMiB : a.identity < b.identity;
        });
        next.memoryTop.clear();
        for (const auto &process : processes)
            if (process.workingMiB > 0 && next.memoryTop.size() < 4)
                next.memoryTop.push_back(process);
        std::sort(processes.begin(), processes.end(), [](const auto &a, const auto &b) {
            if (a.cpuValid != b.cpuValid)
                return a.cpuValid;
            return a.cpu != b.cpu ? a.cpu > b.cpu : a.identity < b.identity;
        });
        next.top.assign(processes.begin(), processes.begin() + std::min(size_t{4}, processes.size()));
    }
    void Run() {
        HardwareSnapshot next;
        wchar_t name[256]{};
        DWORD bytes = sizeof(name);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, name, &bytes);
        if (name[0])
            next.cpu = name;
        std::wstring luid;
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            for (UINT index = 0;; ++index) {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
                    break;
                if (!adapter)
                    continue;
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    (next.gpu == L"Graphics" || desc.DedicatedVideoMemory / 1073741824.0 > next.vramGiB)) {
                    next.gpu = desc.Description;
                    next.vramGiB = desc.DedicatedVideoMemory / 1073741824.0;
                    wchar_t key[80]{};
                    swprintf_s(key, L"luid_0x%08x_0x%08x", static_cast<unsigned>(desc.AdapterLuid.HighPart),
                               static_cast<unsigned>(desc.AdapterLuid.LowPart));
                    luid = key;
                }
            }
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER counter = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
            if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) !=
                ERROR_SUCCESS) {
                PdhCloseQuery(query);
                query = nullptr;
            }
        }
        for (;;) {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stop_ || requested_; });
            if (stop_)
                break;
            requested_ = false;
            lock.unlock();
            next.gpuValid = false;
            next.gpuPercent = 0;

            if (query && PdhCollectQueryData(query) == ERROR_SUCCESS) {
                DWORD size = 0, count = 0;
                if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) == PDH_MORE_DATA &&
                    size < 4000000) {
                    std::vector<BYTE> data(size);
                    auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(data.data());
                    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                        std::map<std::wstring, double> engines;
                        for (DWORD i = 0; i < count; ++i) {
                            std::wstring instance = items[i].szName;
                            auto position = instance.find(L"luid_");
                            if (position == std::wstring::npos ||
                                (!luid.empty() && instance.find(luid) == std::wstring::npos))
                                continue;
                            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                                items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
                                engines[instance.substr(position)] += std::max(0.0, items[i].FmtValue.doubleValue);
                        }
                        next.gpuValid = !engines.empty();
                        next.gpuPercent = 0;
                        for (const auto &engine : engines)
                            next.gpuPercent = std::max(next.gpuPercent, std::min(100.0, engine.second));
                    }
                }
            }
            Collect(next);
            {
                std::scoped_lock result(mutex_);
                state_ = next;
            }
            PostMessageW(window_, HardwareChanged, 0, 0);
        }
        if (query)
            PdhCloseQuery(query);
    }

  public:
    explicit HardwareStats(HWND window) : window_(window), worker_([this] { Run(); }) {}
    ~HardwareStats() {
        {
            std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }
    void Request() {
        {
            std::scoped_lock lock(mutex_);
            requested_ = true;
        }
        ready_.notify_one();
    }
    HardwareSnapshot Snapshot() {
        std::scoped_lock lock(mutex_);
        return state_;
    }
};
