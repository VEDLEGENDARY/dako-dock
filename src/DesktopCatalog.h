#pragma once
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include "ThemePalette.h"
#include "SettingsStore.h"
#include "WallpaperModel.h"

constexpr UINT CatalogChanged = WM_APP + 4;
constexpr UINT WallpaperApplied=WM_APP+25,ThemeChanged=WM_APP+26;

struct AppEntry {
    std::wstring name;
    std::wstring parsingName,appId,executable;
    std::shared_ptr<std::vector<BYTE>> iconPixels;
    UINT iconWidth=0,iconHeight=0;bool iconAttempted=false;
    AppEntry(std::wstring label={},std::wstring identity={}):name(std::move(label)),parsingName(std::move(identity)){}

};
struct WallpaperEntry {
    std::wstring path;
    std::wstring title;
    UINT width = 0, height = 0;
    std::shared_ptr<std::vector<BYTE>> pixels;
};

struct WallpaperDisplay { std::wstring id, label, path; RECT bounds{}; };

// Lazy discovery and bounded thumbnail decoding keep file IO and shell COM off the UI thread.
class DesktopCatalog {
    HWND window_;
    std::mutex mutex_, resultMutex_;
    std::condition_variable ready_;
    struct Job {std::function<void()> run;unsigned kind,generation;};
    std::deque<Job> jobs_;
    std::atomic<unsigned> wallpaperGeneration_{0};
    bool iconsPending_=false;
    bool stopping_ = false;
    std::vector<AppEntry> apps_;
    std::vector<WallpaperEntry> wallpapers_;
    std::vector<WallpaperDisplay> displays_;
    DESKTOP_WALLPAPER_POSITION position_=DWPOS_FILL;
    std::wstring appError_,wallpaperError_;
    std::wstring themePath_;
    std::filesystem::file_time_type themeStamp_{};
    std::thread worker_;

    static std::wstring DisplayName(IShellItem* item,SIGDN kind) {
        PWSTR text = nullptr;
        if (FAILED(item->GetDisplayName(kind,&text))) return {};
        std::wstring value(text); CoTaskMemFree(text); return value;
    }
    void Queue(std::function<void()> job,unsigned kind=1,unsigned generation=0) {
        { std::scoped_lock lock(mutex_); if (stopping_) return; jobs_.push_back({std::move(job),kind,generation}); }
        ready_.notify_one();
    }
    bool Stopping() { std::scoped_lock lock(mutex_); return stopping_; }
    void RefreshTheme(){
        try{auto path=CurrentWallpaperPath();std::error_code error;auto stamp=std::filesystem::last_write_time(path,error);if(path==themePath_&&stamp==themeStamp_)return;themePath_=path;themeStamp_=stamp;
            ComPtr<IWICImagingFactory> images;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images))))return;
            auto image=Decode(images.Get(),path);if(image.pixels&&!image.pixels->empty())PostMessageW(window_,ThemeChanged,palette::Accent(image.pixels->data(),image.pixels->size()),0);
        }catch(const std::exception&){}catch(const winrt::hresult_error&){}
    }
    void Run() {
        HRESULT initialized = CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (FAILED(initialized)) return;
        RefreshTheme();
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock,[this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) break;

                job=std::move(jobs_.front()); jobs_.pop_front();
            }
            try { job.run(); }
            catch (const std::exception&) {
                std::scoped_lock lock(resultMutex_); (job.kind==1?appError_:wallpaperError_)=job.kind==1?L"Installed apps could not be read. Reopen Apps to retry.":L"This folder could not be read. Choose another folder.";
            }
            catch (const winrt::hresult_error&) {
                std::scoped_lock lock(resultMutex_); (job.kind==1?appError_:wallpaperError_)=L"Windows could not load this collection. Try again.";
            }
            if(job.kind&&!(job.kind==2&&job.generation&&job.generation!=wallpaperGeneration_))PostMessageW(window_,CatalogChanged,job.kind,0);
        }
        CoUninitialize();
    }
    static WallpaperEntry Decode(IWICImagingFactory* images,const std::wstring& path,double maxWidth=320,double maxHeight=200) {
        WallpaperEntry result; result.path=path;result.title=std::filesystem::path(path).stem().wstring();
        ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICBitmapScaler> scaler; ComPtr<IWICFormatConverter> converter;
        if (FAILED(images->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)) ||
            FAILED(decoder->GetFrame(0,&frame))) return result;
        UINT width=0,height=0; if (FAILED(frame->GetSize(&width,&height)) || !width || !height) return result;
        // Cap decoded surfaces at 320x200. Never retain a full-resolution wallpaper bitmap.
        double ratio=std::min({1.0,maxWidth/width,maxHeight/height});
        result.width=std::max(1U,static_cast<UINT>(width*ratio));
        result.height=std::max(1U,static_cast<UINT>(height*ratio));
        if (FAILED(images->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(),result.width,result.height,WICBitmapInterpolationModeFant)) ||
            FAILED(images->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
        result.pixels=std::make_shared<std::vector<BYTE>>(static_cast<size_t>(result.width)*result.height*4);
        if (FAILED(converter->CopyPixels(nullptr,result.width*4,static_cast<UINT>(result.pixels->size()),result.pixels->data()))) return {};
        return result;
    }
public:
    void SettingsChanged(){Queue([this]{RefreshTheme();},0);}
    static constexpr size_t MaxWallpapers = 64;
    static std::wstring CurrentWallpaperPath() {
        ComPtr<IDesktopWallpaper> desktop;
        if(SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&desktop)))) {
            PWSTR path=nullptr;
            HRESULT result=desktop->GetWallpaper(nullptr,&path);
            std::wstring value=path?path:L"";CoTaskMemFree(path);
            if(result==S_OK && !value.empty())return value;
        }
        wchar_t current[32768]{};
        return SystemParametersInfoW(SPI_GETDESKWALLPAPER,32768,current,0)?std::wstring(current):std::wstring{};
    }
    static bool ValidWallpaper(const std::wstring& path) {
        if(path.empty())return false;
        ComPtr<IWICImagingFactory> images;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images))))return false;
        auto decoded=Decode(images.Get(),path);
        return decoded.pixels && !decoded.pixels->empty();
    }
    static HRESULT ApplyWallpaper(const std::wstring& path) {
        // A stale or invalid file must never be passed to Windows as an empty background.
        if(!ValidWallpaper(path))return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        ComPtr<IDesktopWallpaper> desktop;
        HRESULT result=CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&desktop));
        if(FAILED(result))return result;
        return desktop->SetWallpaper(nullptr,path.c_str());
    }
    static bool SameWallpaperFile(const std::filesystem::path& left,const std::filesystem::path& right) {
        std::error_code error;
        const auto leftSize=std::filesystem::file_size(left,error);if(error)return false;
        const auto rightSize=std::filesystem::file_size(right,error);if(error||leftSize!=rightSize)return false;
        std::ifstream a(left,std::ios::binary),b(right,std::ios::binary);
        return a && b && std::equal(std::istreambuf_iterator<char>(a),std::istreambuf_iterator<char>(),std::istreambuf_iterator<char>(b),std::istreambuf_iterator<char>());
    }
    static std::wstring PreservePersonalWallpaper() {
        const auto current=CurrentWallpaperPath();
        PWSTR local=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return current;
        const auto localRoot=std::filesystem::path(local);CoTaskMemFree(local);
        const auto root=localRoot/L"Dako"/L"Wallpapers";
        const auto metadata=root/L"original-wallpaper.ini";
        wchar_t stored[32768]{};
        GetPrivateProfileStringW(L"Original",L"SourcePath",L"",stored,32768,metadata.c_str());
        if(ValidWallpaper(stored))return stored;
        std::error_code error;
        std::wstring backup;
        for(const auto* extension:{L".jpg",L".jpeg",L".png",L".bmp"}) {
            const auto saved=root/(std::wstring(L"original-wallpaper")+extension);
            if(ValidWallpaper(saved.wstring())){backup=saved.wstring();break;}
        }
        // A temporarily missing source must not erase the saved original path.
        if(stored[0] && !backup.empty())return backup;
        // Migrate earlier copies by recovering the actual path retained by the Windows theme.
        const auto customTheme=localRoot/L"Microsoft"/L"Windows"/L"Themes"/L"Custom.theme";
        wchar_t themed[32768]{},expanded[32768]{};
        GetPrivateProfileStringW(L"Control Panel\\Desktop",L"Wallpaper",L"",themed,32768,customTheme.c_str());
        const DWORD expandedLength=ExpandEnvironmentStringsW(themed,expanded,32768);
        std::wstring source;
        if(expandedLength && expandedLength<=32768 && ValidWallpaper(expanded) &&
            (backup.empty() || SameWallpaperFile(expanded,backup)))source=expanded;
        else if(!backup.empty())source=backup;
        else if(ValidWallpaper(current))source=current;
        if(source.empty())return {};
        std::filesystem::create_directories(root,error);
        if(error)return source;
        // Save the original source path before creating any fallback image copy.
        {
            privacy::Write audit(L"wallpaper", L"file-write", metadata.wstring(), L"original-wallpaper-metadata");
            audit.Complete(WritePrivateProfileStringW(L"Original",L"SourcePath",source.c_str(),metadata.c_str()) != FALSE);
        }
        if(!backup.empty())return source;
        auto extension=std::filesystem::path(source).extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        if(extension!=L".jpg"&&extension!=L".jpeg"&&extension!=L".png"&&extension!=L".bmp")extension=L".jpg";
        const auto saved=root/(L"original-wallpaper"+extension),temporary=root/L"original-wallpaper.tmp";
        privacy::Write copy(L"wallpaper", L"file-write", temporary.wstring(), L"original-wallpaper-copy");
        privacy::Write commit(L"wallpaper", L"file-write", saved.wstring(), L"original-wallpaper-backup");
        bool copied = CopyFileW(source.c_str(),temporary.c_str(),FALSE) != FALSE;
        copy.Complete(copied);
        commit.Complete(copied && MoveFileExW(temporary.c_str(),saved.c_str(),MOVEFILE_WRITE_THROUGH));
        DeleteFileW(temporary.c_str());
        return source;
    }
    static std::filesystem::path BundledWallpaperRoot() {
        wchar_t executable[32768]{};
        DWORD length=GetModuleFileNameW(nullptr,executable,32768);
        if (!length || length>=32768) winrt::throw_hresult(E_FAIL);
        return std::filesystem::path(executable).parent_path()/L"assets"/L"wallpapers";
    }
    // Also used by native render validation, with COM initialized by the caller.
    static std::vector<WallpaperEntry> LoadWallpapers(const std::filesystem::path& root,const std::function<bool()>& cancelled={}) {
        std::vector<std::wstring> paths;
        std::error_code error;
        auto options=std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator file(root,options,error),end;
        size_t visited=0;
        for (;file!=end && !error && paths.size()<MaxWallpapers && visited++<2048 && (!cancelled || !cancelled());file.increment(error)) {
            if (file.depth()>=2) file.disable_recursion_pending();
            if (file->is_symlink(error)) { file.disable_recursion_pending(); continue; }
            if (!file->is_regular_file(error)) continue;
            auto extension=file->path().extension().wstring();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (extension==L".jpg" || extension==L".jpeg" || extension==L".png" || extension==L".bmp") paths.push_back(file->path().wstring());
        }
        std::sort(paths.begin(),paths.end());
        ComPtr<IWICImagingFactory> images;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
        std::vector<WallpaperEntry> entries;
        for (const auto& path:paths) {
            if(cancelled && cancelled())break;
            auto decoded=Decode(images.Get(),path);
            if (decoded.pixels && !decoded.pixels->empty()) entries.push_back(std::move(decoded));
        }
        return entries;
    }
    static std::vector<WallpaperEntry> LoadDefaultWallpapers(const std::function<bool()>& cancelled={},const std::wstring& personalPath=CurrentWallpaperPath()) {
        auto entries=LoadWallpapers(BundledWallpaperRoot(),cancelled);
        if(cancelled && cancelled())return entries;
        if(!personalPath.empty()) {
            const bool duplicate=std::any_of(entries.begin(),entries.end(),[&](const auto& entry){
                return CompareStringOrdinal(entry.path.c_str(),-1,personalPath.c_str(),-1,TRUE)==CSTR_EQUAL;
            });
            if(!duplicate && entries.size()<MaxWallpapers) {
                ComPtr<IWICImagingFactory> images;
                Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
                auto personal=Decode(images.Get(),personalPath);
                personal.title=L"Your wallpaper";
                if(personal.pixels && !personal.pixels->empty())entries.push_back(std::move(personal));
            }
        }
        return entries;
    }
    explicit DesktopCatalog(HWND window) : window_(window),worker_([this] { Run(); }) {}
    ~DesktopCatalog() {
        { std::scoped_lock lock(mutex_); stopping_=true; jobs_.clear(); }
        ready_.notify_one(); worker_.join();
    }
    void DiscoverApps() {
        Queue([this] {
            std::vector<AppEntry> entries;
            ComPtr<IShellItem> folder;
            Check(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&folder)));
            ComPtr<IEnumShellItems> items;
            Check(folder->BindToHandler(nullptr,BHID_EnumItems,IID_PPV_ARGS(&items)));
            ComPtr<IShellItem> item;
            while (!Stopping() && entries.size()<2048 && items->Next(1,&item,nullptr)==S_OK) {
                auto name=DisplayName(item.Get(),SIGDN_NORMALDISPLAY);
                auto parsing=DisplayName(item.Get(),SIGDN_DESKTOPABSOLUTEPARSING);
                if (!name.empty() && !parsing.empty()) {
                    AppEntry entry{std::move(name),std::move(parsing)};
                    ComPtr<IShellItem2> properties;
                    if(SUCCEEDED(item.As(&properties))){
                        PWSTR value=nullptr;
                        if(SUCCEEDED(properties->GetString(PKEY_AppUserModel_ID,&value))){
                            entry.appId=value;CoTaskMemFree(value);value=nullptr;
                        }
                        if(SUCCEEDED(properties->GetString(PKEY_Link_TargetParsingPath,&value))){
                            entry.executable=value;CoTaskMemFree(value);value=nullptr;
                        }
                        if(entry.executable.empty()&&SUCCEEDED(properties->GetString(
                            PKEY_AppUserModel_RelaunchCommand,&value))){
                            int count=0;auto arguments=CommandLineToArgvW(value,&count);
                            if(arguments&&count)entry.executable=arguments[0];
                            if(arguments)LocalFree(arguments);CoTaskMemFree(value);
                        }
                    }
                    entries.push_back(std::move(entry));
                }
                item.Reset();
            }
            std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
                return CompareStringOrdinal(a.name.c_str(),-1,b.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;
            });
            std::scoped_lock lock(resultMutex_); apps_=std::move(entries); appError_.clear();
        });
    }
    static std::vector<WallpaperDisplay> ReadDisplays(IDesktopWallpaper* desktop) {
        std::vector<WallpaperDisplay> displays; UINT count=0;
        if (FAILED(desktop->GetMonitorDevicePathCount(&count))) return displays;
        for (UINT i=0; i<std::min(count,16U); ++i) {
            PWSTR id=nullptr; if (FAILED(desktop->GetMonitorDevicePathAt(i,&id))) continue;
            WallpaperDisplay display; display.id=id?id:L""; CoTaskMemFree(id);
            if (display.id.empty() || FAILED(desktop->GetMonitorRECT(display.id.c_str(),&display.bounds))) continue;
            display.label=L"Display "+std::to_wstring(i+1);
            PWSTR path=nullptr; desktop->GetWallpaper(display.id.c_str(),&path);
            display.path=path?path:L""; CoTaskMemFree(path); displays.push_back(std::move(display));
        }
        return displays;
    }
    void DiscoverDisplays() {
        Queue([this] {
            ComPtr<IDesktopWallpaper> desktop;
            if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&desktop)))) return;
            auto displays=ReadDisplays(desktop.Get()); DESKTOP_WALLPAPER_POSITION fit=DWPOS_FILL;
            desktop->GetPosition(&fit);
            std::scoped_lock lock(resultMutex_); displays_=std::move(displays); position_=fit;
        },4);
    }
    void DisplaySnapshot(std::vector<WallpaperDisplay>& displays,DESKTOP_WALLPAPER_POSITION& fit) {
        std::scoped_lock lock(resultMutex_); displays=displays_; fit=position_;
    }
    void DiscoverCurrentWallpapers() {
        unsigned generation=++wallpaperGeneration_;
        Queue([this,generation] {
            ComPtr<IDesktopWallpaper> desktop;
            if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&desktop)))) return;
            ComPtr<IWICImagingFactory> images;
            Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
            std::vector<WallpaperEntry> entries;
            for (const auto& display:ReadDisplays(desktop.Get())) {
                if (Stopping() || generation!=wallpaperGeneration_) return;
                if (display.path.empty() || std::any_of(entries.begin(),entries.end(),[&](const auto& item) {
                    return CompareStringOrdinal(item.path.c_str(),-1,display.path.c_str(),-1,TRUE)==CSTR_EQUAL;
                })) continue;
                auto entry=Decode(images.Get(),display.path);
                if (entry.pixels) { entry.title=display.label+L" · "+entry.title; entries.push_back(std::move(entry)); }
            }
            std::scoped_lock lock(resultMutex_); wallpapers_=std::move(entries);
            wallpaperError_=wallpapers_.empty()?L"No readable current wallpaper images.":L"";
        },2,generation);
    }
    void ApplyWallpaperAsync(std::wstring path,std::wstring monitor={},DESKTOP_WALLPAPER_POSITION fit=DWPOS_FILL) {
        Queue([this,path=std::move(path),monitor=std::move(monitor),fit] {
            bool ok=false;auto applied=wallpaper::Result::InvalidTarget;
            try {
                ComPtr<IDesktopWallpaper> desktop;
                if (ValidWallpaper(path) && SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,
                    IID_PPV_ARGS(&desktop)))) {
                    effects::Transaction transaction;
                    RECT connected{};
                    if((!monitor.empty()&&FAILED(desktop->GetMonitorRECT(monitor.c_str(),&connected)))||
                        !effects::RememberWallpaper(desktop.Get())){
                        PostMessageW(window_,WallpaperApplied,FALSE,static_cast<LPARAM>(wallpaper::Result::InvalidTarget));return;
                    }
                    PreservePersonalWallpaper();
                    wallpaper::Operations operations;
                    operations.validate=[&] {
                        RECT bounds{};
                        return monitor.empty() || SUCCEEDED(desktop->GetMonitorRECT(monitor.c_str(),&bounds));
                    };
                    operations.readFit=[&] {
                        DESKTOP_WALLPAPER_POSITION prior;
                        return SUCCEEDED(desktop->GetPosition(&prior))?static_cast<int>(prior):-1;
                    };
                    operations.setFit=[&](int value) {
                        return SUCCEEDED(desktop->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(value)));
                    };
                    operations.setImage=[&] {
                        return SUCCEEDED(desktop->SetWallpaper(monitor.empty()?nullptr:monitor.c_str(),path.c_str()));
                    };
                    applied=wallpaper::Apply(static_cast<int>(fit),operations);
                    ok=applied==wallpaper::Result::Applied;
                }
            } catch (...) {}
            PostMessageW(window_,WallpaperApplied,ok,static_cast<LPARAM>(applied));
        },0);
    }
    void DiscoverWallpapers(std::wstring folder={}) {
        unsigned generation=++wallpaperGeneration_;
        Queue([this,generation,folder=std::move(folder)] {
            auto cancelled=[this,generation]{return Stopping()||wallpaperGeneration_!=generation;};
            if(cancelled())return;
            auto entries=LoadWallpapers(folder.empty()?BundledWallpaperRoot():std::filesystem::path(folder),cancelled);
            if(cancelled())return;
            std::scoped_lock lock(resultMutex_);
            wallpapers_=std::move(entries);
            wallpaperError_=wallpapers_.empty()?L"No wallpapers found. Choose another folder.":L"";
        },2,generation);
    }
    void RequestIcons(std::vector<std::wstring> keys){
        {std::scoped_lock lock(resultMutex_);if(iconsPending_)return;bool needed=false;for(const auto& key:keys)for(const auto& app:apps_)if(app.parsingName==key&&!app.iconAttempted)needed=true;if(!needed)return;iconsPending_=true;}
        Queue([this,keys=std::move(keys)]{
            try{
                ComPtr<IWICImagingFactory> images;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));
                for(const auto& key:keys){if(Stopping())break;{std::scoped_lock lock(resultMutex_);for(auto& app:apps_)if(app.parsingName==key)app.iconAttempted=true;}ComPtr<IShellItemImageFactory> item;if(FAILED(SHCreateItemFromParsingName(key.c_str(),nullptr,IID_PPV_ARGS(&item))))continue;
                    HBITMAP bitmap=nullptr;if(FAILED(item->GetImage({128,128},SIIGBF_ICONONLY,&bitmap)))continue;
                    ComPtr<IWICBitmap> source;HRESULT result=images->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapUseAlpha,&source);DeleteObject(bitmap);if(FAILED(result))continue;
                    ComPtr<IWICFormatConverter> converter;Check(images->CreateFormatConverter(&converter));Check(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
                    UINT w=0,h=0;converter->GetSize(&w,&h);if(w>128||h>128)continue;auto pixels=std::make_shared<std::vector<BYTE>>(static_cast<size_t>(w)*h*4);if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels->size()),pixels->data())))continue;
                    std::scoped_lock lock(resultMutex_);for(auto& app:apps_)if(app.parsingName==key){app.iconPixels=pixels;app.iconWidth=w;app.iconHeight=h;}
                }
                std::scoped_lock lock(resultMutex_);size_t retained=0;for(auto& app:apps_)if(app.iconPixels){bool visible=std::find(keys.begin(),keys.end(),app.parsingName)!=keys.end();if(!visible&&++retained>32){app.iconPixels.reset();app.iconAttempted=false;}}iconsPending_=false;
            }catch(...){std::scoped_lock lock(resultMutex_);iconsPending_=false;}
        },1);
    }
    void Snapshot(std::vector<AppEntry>& apps,std::vector<WallpaperEntry>& wallpapers,std::wstring& error,unsigned kind=0) {
        std::scoped_lock lock(resultMutex_); apps=apps_; wallpapers=wallpapers_; error=kind==1?appError_:wallpaperError_;
    }
};
