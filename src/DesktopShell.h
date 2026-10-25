#pragma once
#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include "DockMotion.h"
#include <array>
#include <string>
#include <vector>
#include <map>

namespace desktop {
constexpr UINT TrayMessage=WM_APP+20,ShellChanged=WM_APP+21,RailChanged=WM_APP+22,RailCallback=WM_APP+23;
constexpr UINT_PTR ShellTimer=20;
struct Area { RECT monitor{},work{}; };
struct BrowserCutout {HWND window=nullptr;DWORD process=0,bytes=0;bool hadRegion=false;std::array<DWORD,16384> original{};};
struct Recovery {
    DWORD taskbarState=0; LONG areaCount=0;
    std::array<Area,16> areas{};
    BrowserCutout cutout;
};
inline void RestoreCutout(BrowserCutout& saved){
    DWORD process=0;GetWindowThreadProcessId(saved.window,&process);
    if(saved.window&&IsWindow(saved.window)&&process==saved.process){HRGN region=saved.hadRegion?ExtCreateRegion(nullptr,saved.bytes,reinterpret_cast<const RGNDATA*>(saved.original.data())):nullptr;
        if(!saved.hadRegion||region){if(!SetWindowRgn(saved.window,region,TRUE)&&region)DeleteObject(region);}}
    saved.window=nullptr;saved.bytes=0;saved.hadRegion=false;
}
inline bool IsTaskbar(HWND window){wchar_t name[64]{};GetClassNameW(window,name,64);return !wcscmp(name,L"Shell_TrayWnd")||!wcscmp(name,L"Shell_SecondaryTrayWnd");}
inline RECT WorkArea(HMONITOR monitor,HMONITOR dockMonitor){
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);RECT work=info.rcMonitor;
    if(monitor==dockMonitor){DEVICE_SCALE_FACTOR scale=SCALE_100_PERCENT;GetScaleFactorForMonitor(monitor,&scale);work.top+=MulDiv(static_cast<int>(theme::TopRail),static_cast<int>(scale),100);}return work;
}
inline void ReflowMaximized(){
    EnumWindows([](HWND window,LPARAM)->BOOL{
        if(!IsWindowVisible(window)||!IsZoomed(window)||IsTaskbar(window)||(GetWindowLongPtrW(window,GWL_STYLE)&WS_THICKFRAME)==0)return TRUE;
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);RECT outer{},visible{};GetWindowRect(window,&outer);
        int frame=GetSystemMetricsForDpi(SM_CXSIZEFRAME,GetDpiForWindow(window))+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,GetDpiForWindow(window));
        LONG left=frame,top=frame,right=frame,bottom=frame;
        if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible)))&&visible.right>visible.left){
            LONG margins[]{visible.left-outer.left,visible.top-outer.top,outer.right-visible.right,outer.bottom-visible.bottom};
            bool valid=true;for(LONG margin:margins)valid&=margin>=0&&margin<=64;if(valid){left=margins[0];top=margins[1];right=margins[2];bottom=margins[3];}
        }
        RECT desired{info.rcWork.left-left,info.rcWork.top-top,info.rcWork.right+right,info.rcWork.bottom+bottom};
        if(!EqualRect(&desired,&outer))SetWindowPos(window,nullptr,desired.left,desired.top,desired.right-desired.left,desired.bottom-desired.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
        return TRUE;
    },0);
}
inline void Restore(Recovery& saved){
    APPBARDATA bar{sizeof(bar)};bar.lParam=saved.taskbarState;SHAppBarMessage(ABM_SETSTATE,&bar);
    EnumWindows([](HWND window,LPARAM)->BOOL{if(IsTaskbar(window))ShowWindowAsync(window,SW_SHOWNOACTIVATE);return TRUE;},0);
    for(LONG i=0;i<saved.areaCount;++i){MONITORINFO info{sizeof(info)};
        if(GetMonitorInfoW(MonitorFromRect(&saved.areas[i].monitor,MONITOR_DEFAULTTONULL),&info)&&EqualRect(&info.rcMonitor,&saved.areas[i].monitor)){
            RECT work=saved.areas[i].work;SystemParametersInfoW(SPI_SETWORKAREA,0,&work,0);
        }
    }
    ReflowMaximized();SendNotifyMessageW(HWND_BROADCAST,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);RestoreCutout(saved.cutout);
}
// Inherited handles avoid PID reuse and expose only this session's recovery snapshot.
inline int Guard(const wchar_t* arguments){
    unsigned long long parentValue=0,stopValue=0,readyValue=0,mappingValue=0,mutexValue=0;
    if(swscanf_s(arguments,L"%llu %llu %llu %llu %llu",&parentValue,&stopValue,&readyValue,&mappingValue,&mutexValue)!=5)return 4;
    HANDLE parent=reinterpret_cast<HANDLE>(parentValue),stop=reinterpret_cast<HANDLE>(stopValue),ready=reinterpret_cast<HANDLE>(readyValue),mapping=reinterpret_cast<HANDLE>(mappingValue),mutex=reinterpret_cast<HANDLE>(mutexValue);
    auto* saved=static_cast<Recovery*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Recovery)));if(!saved)return 5;
    SetEvent(ready);HANDLE waits[]{parent,stop};WaitForMultipleObjects(2,waits,FALSE,INFINITE);
    DWORD lock=WaitForSingleObject(mutex,5000);
    if(lock==WAIT_OBJECT_0||lock==WAIT_ABANDONED){Restore(*saved);ReleaseMutex(mutex);}
    UnmapViewOfFile(saved);for(HANDLE handle:{parent,stop,ready,mapping,mutex})CloseHandle(handle);return 0;
}
class Shell {
    HWND owner_=nullptr;HANDLE stop_=nullptr,mapping_=nullptr,mutex_=nullptr,guard_=nullptr;
    Recovery* saved_=nullptr;HWINEVENTHOOK hook_=nullptr;HICON icon_=nullptr;
    inline static HWND callbackWindow_=nullptr;
    bool active_=false,tray_=false;
    bool maximizeNewWindows=false;
    HWND rail_=nullptr;bool railRegistered_=false,positioning_=false;RECT railBounds_{};
    RECT clippedBounds_{};
    std::map<HWND,DWORD> seen_;
    static void CALLBACK OnShow(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){
        if(object==OBJID_WINDOW&&callbackWindow_)PostMessageW(callbackWindow_,ShellChanged,event,reinterpret_cast<LPARAM>(window));
    }
    static LRESULT CALLBACK RailProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Shell*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Shell*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==RailCallback&&wparam==ABN_POSCHANGED&&self&&!self->positioning_)PostMessageW(self->owner_,RailChanged,0,0);
        if(self&&self->railRegistered_&&(message==WM_WINDOWPOSCHANGED||message==WM_ACTIVATE)){APPBARDATA bar{sizeof(bar)};bar.hWnd=window;SHAppBarMessage(message==WM_ACTIVATE?ABM_ACTIVATE:ABM_WINDOWPOSCHANGED,&bar);}
        if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        return DefWindowProcW(window,message,wparam,lparam);
    }
    static BOOL CALLBACK CaptureArea(HMONITOR monitor,HDC,LPRECT,LPARAM context){
        auto* saved=reinterpret_cast<Recovery*>(context);MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(monitor,&info))return TRUE;
        for(LONG i=0;i<saved->areaCount;++i)if(EqualRect(&saved->areas[i].monitor,&info.rcMonitor))return TRUE;
        if(saved->areaCount<static_cast<LONG>(saved->areas.size()))saved->areas[saved->areaCount++]={info.rcMonitor,info.rcWork};return TRUE;
    }
    void AddTray(){
        NOTIFYICONDATAW data{sizeof(data)};data.hWnd=owner_;data.uID=1;data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;data.uCallbackMessage=TrayMessage;data.hIcon=icon_;
        wcscpy_s(data.szTip,L"Dako — desktop dock");tray_=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
        data.uVersion=NOTIFYICON_VERSION_4;if(tray_)Shell_NotifyIconW(NIM_SETVERSION,&data);
    }
public:
    UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    bool Active()const{return active_;}
    void SetMaximizeNew(bool value){maximizeNewWindows=value;}
    bool MaximizeNew()const{return maximizeNewWindows;}
    ~Shell(){Stop();}
    void PositionRail(){
        if(!active_)return;bool changed=false;
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM context)->BOOL{MONITORINFO info{sizeof(info)};
            if(GetMonitorInfoW(monitor,&info)&&!EqualRect(&info.rcWork,&info.rcMonitor)){SystemParametersInfoW(SPI_SETWORKAREA,0,&info.rcMonitor,SPIF_SENDCHANGE);*reinterpret_cast<bool*>(context)=true;}return TRUE;},reinterpret_cast<LPARAM>(&changed));
        if(changed)ReflowMaximized();
    }
    void UpdateBrowserCutout(HWND browser){
        if(!active_||!saved_||WaitForSingleObject(mutex_,0)!=WAIT_OBJECT_0)return;
        auto& saved=saved_->cutout;
        bool usable=browser&&IsWindow(browser)&&IsZoomed(browser)&&MonitorFromWindow(browser,MONITOR_DEFAULTTONEAREST)==MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST);
        if(!usable||saved.window!=browser){RestoreCutout(saved);clippedBounds_={};}
        if(!usable){ReleaseMutex(mutex_);return;}
        RECT bounds{};GetWindowRect(browser,&bounds);if(saved.window==browser&&EqualRect(&bounds,&clippedBounds_)){ReleaseMutex(mutex_);return;}
        DWORD_PTR response=0;if(!SendMessageTimeoutW(browser,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,80,&response)){ReleaseMutex(mutex_);return;}
        if(!saved.window){
            HRGN original=CreateRectRgn(0,0,0,0);int kind=GetWindowRgn(browser,original);saved.hadRegion=kind!=ERROR;
            if(saved.hadRegion){DWORD bytes=GetRegionData(original,0,nullptr);if(bytes>sizeof(saved.original)||!GetRegionData(original,bytes,reinterpret_cast<RGNDATA*>(saved.original.data()))){DeleteObject(original);saved.hadRegion=false;ReleaseMutex(mutex_);return;}saved.bytes=bytes;}
            DeleteObject(original);saved.window=browser;GetWindowThreadProcessId(browser,&saved.process);
        }
        HRGN region=saved.hadRegion?ExtCreateRegion(nullptr,saved.bytes,reinterpret_cast<const RGNDATA*>(saved.original.data())):CreateRectRgn(0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
        MONITORINFO monitor{sizeof(monitor)};HMONITOR handle=MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(handle,&monitor);DEVICE_SCALE_FACTOR factor=SCALE_100_PERCENT;GetScaleFactorForMonitor(handle,&factor);float scale=static_cast<float>(factor)/100;
        auto outline=dock::Notch((monitor.rcMonitor.left+monitor.rcMonitor.right-theme::DockWidth*scale)/2-bounds.left,static_cast<float>(monitor.rcMonitor.top-bounds.top),theme::DockWidth*scale,theme::DockHeight*scale);
        std::array<POINT,35> points{};for(size_t i=0;i<points.size();++i)points[i]={static_cast<LONG>(std::lround(outline[i].x)),static_cast<LONG>(std::lround(outline[i].y))};
        HRGN notch=CreatePolygonRgn(points.data(),static_cast<int>(points.size()),WINDING);if(region){CombineRgn(region,region,notch,RGN_DIFF);if(SetWindowRgn(browser,region,TRUE))clippedBounds_=bounds;else{DeleteObject(region);RestoreCutout(saved);}}DeleteObject(notch);ReleaseMutex(mutex_);
    }
    void Initialize(HWND owner){
        owner_=owner;HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP color=CreateCompatibleBitmap(screen,32,32),mask=CreateBitmap(32,32,1,1,nullptr);
        auto old=SelectObject(dc,color);RECT bounds{0,0,32,32};HBRUSH black=CreateSolidBrush(RGB(13,13,15));FillRect(dc,&bounds,black);DeleteObject(black);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(203,176,107));HFONT font=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");
        auto oldFont=SelectObject(dc,font);DrawTextW(dc,L"D",1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);
        old=SelectObject(dc,mask);PatBlt(dc,0,0,32,32,BLACKNESS);SelectObject(dc,old);ICONINFO info{TRUE,0,0,mask,color};icon_=CreateIconIndirect(&info);
        DeleteObject(color);DeleteObject(mask);DeleteDC(dc);ReleaseDC(nullptr,screen);AddTray();
    }
    bool Start(){
        if(active_)return true;
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(Recovery),nullptr);
        mutex_=CreateMutexW(&security,FALSE,nullptr);stop_=CreateEventW(&security,TRUE,FALSE,nullptr);HANDLE ready=CreateEventW(&security,TRUE,FALSE,nullptr);
        HANDLE parent=OpenProcess(SYNCHRONIZE,TRUE,GetCurrentProcessId());
        if(mapping_)saved_=static_cast<Recovery*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Recovery)));
        bool started=false;
        if(saved_&&mutex_&&stop_&&ready&&parent){
            *saved_=Recovery{};APPBARDATA bar{sizeof(bar)};saved_->taskbarState=static_cast<DWORD>(SHAppBarMessage(ABM_GETSTATE,&bar));
            EnumDisplayMonitors(nullptr,nullptr,CaptureArea,reinterpret_cast<LPARAM>(saved_));
            wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            std::wstring command=L"\""+std::wstring(executable)+L"\" --shell-guard ";
            for(HANDLE handle:{parent,stop_,ready,mapping_,mutex_})command+=std::to_wstring(reinterpret_cast<ULONG_PTR>(handle))+L" ";
            SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> buffer(bytes);
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
            if(InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)){
                HANDLE inherited[]{parent,stop_,ready,mapping_,mutex_};
                if(UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)){
                    PROCESS_INFORMATION process{};
                    if(CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,nullptr,&startup.StartupInfo,&process)){
                        guard_=process.hProcess;CloseHandle(process.hThread);started=WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;
                    }
                }
                DeleteProcThreadAttributeList(startup.lpAttributeList);
            }
        }
        if(parent)CloseHandle(parent);if(ready)CloseHandle(ready);
        if(!started){Stop();return false;}
        seen_.clear();EnumWindows([](HWND window,LPARAM context)->BOOL{DWORD process=0;GetWindowThreadProcessId(window,&process);reinterpret_cast<Shell*>(context)->seen_[window]=process;return TRUE;},reinterpret_cast<LPARAM>(this));
        active_=true;callbackWindow_=owner_;hook_=SetWinEventHook(EVENT_OBJECT_DESTROY,EVENT_OBJECT_SHOW,nullptr,OnShow,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        APPBARDATA bar{sizeof(bar)};bar.lParam=ABS_AUTOHIDE;SHAppBarMessage(ABM_SETSTATE,&bar);
        PositionRail();
        SetCoalescableTimer(owner_,ShellTimer,1000,nullptr,200);Reconcile();return true;
    }
    void Reconcile(){
        if(!active_)return;
        if(guard_&&WaitForSingleObject(guard_,0)==WAIT_OBJECT_0){Restore(*saved_);Stop();return;}
        EnumWindows([](HWND window,LPARAM)->BOOL{if(IsTaskbar(window)&&IsWindowVisible(window))ShowWindowAsync(window,SW_HIDE);return TRUE;},0);
        if(WaitForSingleObject(mutex_,0)==WAIT_OBJECT_0){
            EnumDisplayMonitors(nullptr,nullptr,CaptureArea,reinterpret_cast<LPARAM>(saved_));
            ReleaseMutex(mutex_);
        }
        PositionRail();
    }
    void WindowEvent(DWORD event,HWND window){
        if(!active_)return;
        if(event==EVENT_OBJECT_DESTROY){seen_.erase(window);return;}
        if(IsTaskbar(window)){Reconcile();return;}
        if(!maximizeNewWindows)return;
        if(!IsWindow(window)||!IsWindowVisible(window)||GetAncestor(window,GA_ROOT)!=window||GetWindow(window,GW_OWNER))return;
        LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
        if((style&(WS_THICKFRAME|WS_MAXIMIZEBOX))!=(WS_THICKFRAME|WS_MAXIMIZEBOX)||(GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)))return;
        DWORD process=0;GetWindowThreadProcessId(window,&process);if(process==GetCurrentProcessId())return;
        auto found=seen_.find(window);if(found!=seen_.end()&&found->second==process)return;
        seen_[window]=process;
        // Only the first show is managed. Restore, dragging, Snap and later resizing remain native.
        if(!IsZoomed(window)&&!IsIconic(window))ShowWindowAsync(window,SW_MAXIMIZE);
    }
    void ExplorerRestarted(){AddTray();if(active_){APPBARDATA bar{sizeof(bar)};bar.lParam=ABS_AUTOHIDE;SHAppBarMessage(ABM_SETSTATE,&bar);Reconcile();}}
    void Stop(){
        active_=false;seen_.clear();if(owner_)KillTimer(owner_,ShellTimer);if(hook_){UnhookWinEvent(hook_);hook_=nullptr;}callbackWindow_=nullptr;
        if(railRegistered_){APPBARDATA bar{sizeof(bar)};bar.hWnd=rail_;SHAppBarMessage(ABM_REMOVE,&bar);railRegistered_=false;}if(rail_){DestroyWindow(rail_);rail_=nullptr;}railBounds_={};
        if(stop_)SetEvent(stop_);if(guard_){WaitForSingleObject(guard_,6000);CloseHandle(guard_);guard_=nullptr;}
        if(saved_){UnmapViewOfFile(saved_);saved_=nullptr;}for(HANDLE handle:{stop_,mapping_,mutex_})if(handle)CloseHandle(handle);stop_=mapping_=mutex_=nullptr;
    }
    void RemoveTray(){if(tray_){NOTIFYICONDATAW data{sizeof(data)};data.hWnd=owner_;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);tray_=false;}if(icon_){DestroyIcon(icon_);icon_=nullptr;}}
};
}
