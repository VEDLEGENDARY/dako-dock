#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../src/AsyncWait.h"
using winrt::Windows::Foundation::IAsyncOperation;
void Require(bool condition,const char* text){if(!condition){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
IAsyncOperation<int> Immediate(){co_return 7;}
IAsyncOperation<int> Delayed(){co_await winrt::resume_after(std::chrono::milliseconds(150));co_return 42;}
int main(){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::wstring queuedInput=L"test-key";{NativeSecret secret(std::move(queuedInput));Require(secret.value==L"test-key"&&std::all_of(queuedInput.begin(),queuedInput.end(),[](wchar_t c){return c==0;}),"queued secret ownership wipes the source, including short strings");}
    Require(!ValidNetworkKey(L"short")&&ValidNetworkKey(L"password")&&ValidNetworkKey(std::wstring(64,L'A'))&&!ValidNetworkKey(std::wstring(64,L'Z')),"password boundaries and hexadecimal keys are validated before native connection");
    Require(AwaitNative(Immediate(),[]{return false;})==7,"completed result is returned");
    auto start=GetTickCount64();bool timeout=false;
    try{AwaitNative(Delayed(),[]{return false;},30);}catch(const winrt::hresult_error& e){timeout=e.code()==HRESULT_FROM_WIN32(ERROR_TIMEOUT);}
    Require(timeout&&GetTickCount64()-start<250,"deadline is finite and reports timeout");
    std::atomic<bool> stop=false;std::thread stopper([&]{Sleep(30);stop=true;});bool canceled=false;start=GetTickCount64();
    try{AwaitNative(Delayed(),[&]{return stop.load();});}catch(const winrt::hresult_canceled&){canceled=true;}
    stopper.join();Require(canceled&&GetTickCount64()-start<250,"shutdown cancels active wait");
    // Let late completions release shared event state before measuring cycles.
    Sleep(250);DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    for(int i=0;i<50;++i){try{AwaitNative(Delayed(),[]{return true;});}catch(const winrt::hresult_canceled&){}}
    Sleep(400);GetProcessHandleCount(GetCurrentProcess(),&after);
    Require(after<=before+4,"canceled operations and late completions release event ownership");
    winrt::uninit_apartment();std::puts("PASS: async success, deadline, shutdown cancellation, late completion and 50-cycle event cleanup.");
}
