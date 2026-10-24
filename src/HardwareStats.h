#pragma once
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_2.h>
#include <tlhelp32.h>

constexpr UINT HardwareChanged=WM_APP+27;
struct HardwareSnapshot {
    std::wstring cpu=L"Processor",gpu=L"Graphics";double gpuPercent=0,vramGiB=0,totalGiB=0;DWORD processes=0;bool gpuValid=false;
    std::vector<std::pair<std::wstring,double>> top;
};
class HardwareStats {
    HWND window_;std::mutex mutex_;std::condition_variable ready_;bool stop_=false,requested_=false;
    HardwareSnapshot state_;std::thread worker_;
    void Run(){
        HardwareSnapshot next;wchar_t name[256]{};DWORD bytes=sizeof(name);RegGetValueW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString",RRF_RT_REG_SZ,nullptr,name,&bytes);if(name[0])next.cpu=name;
        std::wstring luid;
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))for(UINT index=0;;++index){Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;if(!adapter)continue;DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
            if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&(next.gpu==L"Graphics"||desc.DedicatedVideoMemory/1073741824.0>next.vramGiB)){next.gpu=desc.Description;next.vramGiB=desc.DedicatedVideoMemory/1073741824.0;wchar_t key[80]{};swprintf_s(key,L"luid_0x%08x_0x%08x",static_cast<unsigned>(desc.AdapterLuid.HighPart),static_cast<unsigned>(desc.AdapterLuid.LowPart));luid=key;}}
        PDH_HQUERY query=nullptr;PDH_HCOUNTER counter=nullptr;
        if(PdhOpenQueryW(nullptr,0,&query)==ERROR_SUCCESS){if(PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&counter)!=ERROR_SUCCESS){PdhCloseQuery(query);query=nullptr;}}
        for(;;){std::unique_lock lock(mutex_);ready_.wait(lock,[this]{return stop_||requested_;});if(stop_)break;requested_=false;lock.unlock();
            MEMORYSTATUSEX memory{sizeof(memory)};GlobalMemoryStatusEx(&memory);next.totalGiB=memory.ullTotalPhys/1073741824.0;
            if(query&&PdhCollectQueryData(query)==ERROR_SUCCESS){DWORD size=0,count=0;if(PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&size,&count,nullptr)==PDH_MORE_DATA&&size<4000000){std::vector<BYTE> data(size);auto* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(data.data());
                if(PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&size,&count,items)==ERROR_SUCCESS){std::map<std::wstring,double> engines;for(DWORD i=0;i<count;++i){std::wstring instance=items[i].szName;auto position=instance.find(L"luid_");if(position==std::wstring::npos||(!luid.empty()&&instance.find(luid)==std::wstring::npos))continue;
                    if(items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA||items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA)engines[instance.substr(position)]+=std::max(0.0,items[i].FmtValue.doubleValue);}
                    next.gpuValid=!engines.empty();next.gpuPercent=0;for(const auto& engine:engines)next.gpuPercent=std::max(next.gpuPercent,std::min(100.0,engine.second));}}}
            std::vector<std::pair<std::wstring,double>> processes;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);next.processes=0;
            if(snapshot!=INVALID_HANDLE_VALUE){PROCESSENTRY32W entry{sizeof(entry)};if(Process32FirstW(snapshot,&entry))do{++next.processes;if(next.processes>4096)break;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,entry.th32ProcessID);if(!process)continue;PROCESS_MEMORY_COUNTERS_EX counters{};
                if(GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))&&counters.WorkingSetSize){std::wstring label=entry.szExeFile;if(label.ends_with(L".exe"))label.resize(label.size()-4);processes.emplace_back(std::move(label),counters.WorkingSetSize/1048576.0);}CloseHandle(process);}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);}
            std::sort(processes.begin(),processes.end(),[](const auto& a,const auto& b){return a.second>b.second;});if(processes.size()>4)processes.resize(4);next.top=std::move(processes);
            {std::scoped_lock result(mutex_);state_=next;}PostMessageW(window_,HardwareChanged,0,0);
        }
        if(query)PdhCloseQuery(query);
    }
public:
    explicit HardwareStats(HWND window):window_(window),worker_([this]{Run();}){}
    ~HardwareStats(){{std::scoped_lock lock(mutex_);stop_=true;}ready_.notify_one();worker_.join();}
    void Request(){{std::scoped_lock lock(mutex_);requested_=true;}ready_.notify_one();}
    HardwareSnapshot Snapshot(){std::scoped_lock lock(mutex_);return state_;}
};
