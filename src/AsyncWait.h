#pragma once
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <memory>
#include <string>
#include <algorithm>

// One completion handler owns its event independently of the waiting service.
// Cancellation and late completion never capture the service or owner HWND.
template<class Async,class Stop>
auto AwaitNative(Async operation,Stop stopping,DWORD timeout=15000){
    struct Completion {winrt::handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};};
    auto completion=std::make_shared<Completion>();if(!completion->event)winrt::throw_last_error();
    operation.Completed([completion](auto&&,auto&&){SetEvent(completion->event.get());});
    ULONGLONG deadline=GetTickCount64()+timeout;
    for(;;){
        if(stopping()){operation.Cancel();throw winrt::hresult_canceled();}
        if(WaitForSingleObject(completion->event.get(),25)==WAIT_OBJECT_0)return operation.GetResults();
        if(GetTickCount64()>=deadline){operation.Cancel();winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));}
    }
}

// Shared ownership also wipes queued secrets when a worker discards its queue.
struct NativeSecret {
    std::wstring value;
    explicit NativeSecret(std::wstring&& input):value(input){if(!input.empty())SecureZeroMemory(input.data(),input.size()*sizeof(wchar_t));}
    ~NativeSecret(){if(!value.empty())SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));}
    NativeSecret(const NativeSecret&)=delete;
    NativeSecret& operator=(const NativeSecret&)=delete;
};

inline bool IsHexNetworkKey(const std::wstring& value){return value.size()==64&&std::all_of(value.begin(),value.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');});}
inline bool ValidNetworkKey(const std::wstring& value){return (value.size()>=8&&value.size()<=63)||IsHexNetworkKey(value);}
