#pragma once
#include <windows.h>
#include <thread>
#include <system_error>

// Wait only while motion is active. One UI frame must be presented before the
// next request: neither a busy loop nor an unbounded queue of frame messages.
class FrameClock {
    HANDLE stop_=nullptr,presented_=nullptr;
    std::thread thread_;
    ULONG_PTR generation_=0;
public:
    ~FrameClock(){Stop();}
    bool Active()const{return thread_.joinable();}
    bool Accepts(WPARAM generation)const{return Active()&&generation==generation_;}
    bool Start(HWND window,UINT message,HANDLE frameReady){
        if(Active())return true;
        if(!frameReady)return false;
        stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);presented_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!stop_||!presented_){Stop();return false;}
        ++generation_;auto generation=generation_;HANDLE stop=stop_,presented=presented_;
        try{thread_=std::thread([window,message,frameReady,stop,presented,generation]{
            HANDLE ready[]{stop,frameReady},ack[]{stop,presented};
            while(WaitForMultipleObjects(2,ready,FALSE,INFINITE)==WAIT_OBJECT_0+1){
                if(!PostMessageW(window,message,generation,0))break;
                if(WaitForMultipleObjects(2,ack,FALSE,INFINITE)!=WAIT_OBJECT_0+1)break;
            }
        });}catch(const std::system_error&){Stop();return false;}
        return true;
    }
    void Presented(){if(presented_)SetEvent(presented_);}
    void Stop(){
        if(stop_)SetEvent(stop_);if(thread_.joinable())thread_.join();
        if(stop_)CloseHandle(stop_);if(presented_)CloseHandle(presented_);stop_=presented_=nullptr;++generation_;
    }
};
