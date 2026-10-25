#pragma once
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include "ThemePalette.h"

constexpr UINT CatalogChanged = WM_APP + 4;
constexpr UINT WallpaperApplied=WM_APP+25,ThemeChanged=WM_APP+26;

struct AppEntry {
    std::wstring name;
    std::wstring parsingName;
    std::shared_ptr<std::vector<BYTE>> iconPixels;
    UINT iconWidth=0,iconHeight=0;bool iconAttempted=false;
    AppEntry(std::wstring label={},std::wstring identity={}):name(std::move(label)),parsingName(std::move(identity)){}

};
struct WallpaperEntry {
    std::wstring path;
    std::wstring title;
    UINT width = 0, height = 0;
    std::shared_ptr<std::vector<BYTE>> pixels;
    UINT detailWidth=0,detailHeight=0;std::shared_ptr<std::vector<BYTE>> detailPixels;
};

// Lazy discovery and bounded thumbnail decoding keep file IO and shell COM off the UI thread.
class DesktopCatalog {
    HWND window_;
    std::mutex mutex_, resultMutex_;
    std::condition_variable ready_;
    struct Job {std::function<void()> run;unsigned kind,generation;};
    std::deque<Job> jobs_;
    std::atomic<unsigned> wallpaperGeneration_{0};
    bool iconsPending_=false;
    std::wstring detailPath_;std::atomic<unsigned> detailGeneration_{0};
    bool stopping_ = false;
    std::vector<AppEntry> apps_;
    std::vector<WallpaperEntry> wallpapers_;
    std::wstring appError_,wallpaperError_;
    std::wstring personalWallpaperPath_;
    bool personalWallpaperCaptured_ = false;
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
    void PollTheme(){
        try{auto path=CurrentWallpaperPath();std::error_code error;auto stamp=std::filesystem::last_write_time(path,error);if(path==themePath_&&stamp==themeStamp_)return;themePath_=path;themeStamp_=stamp;
            ComPtr<IWICImagingFactory> images;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images))))return;
            auto image=Decode(images.Get(),path);if(image.pixels&&!image.pixels->empty())PostMessageW(window_,ThemeChanged,palette::Accent(image.pixels->data(),image.pixels->size()),0);
        }catch(const std::exception&){}catch(const winrt::hresult_error&){}
    }
    void Run() {
        HRESULT initialized = CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (FAILED(initialized)) return;
        PollTheme();
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait_for(lock,std::chrono::seconds(5),[this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) break;
                if(jobs_.empty()){lock.unlock();PollTheme();continue;}
                job=std::move(jobs_.front()); jobs_.pop_front();
            }
            try { job.run(); }
            catch (const std::exception&) {
                std::scoped_lock lock(resultMutex_); (job.kind==1?appError_:wallpaperError_)=job.kind==1?L"Installed apps could not be read. Reopen Apps to retry.":L"This folder could not be read. Choose another folder.";
            }
            catch (const winrt::hresult_error&) {
                std::scoped_lock lock(resultMutex_); (job.kind==1?appError_:wallpaperError_)=L"Windows could not load this collection. Try again.";
            }
            if(job.kind&&!(job.kind==2&&job.generation&&job.generation!=wallpaperGeneration_)&&!(job.kind==3&&job.generation&&job.generation!=detailGeneration_))PostMessageW(window_,CatalogChanged,job.kind,0);
            PollTheme();
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
        WritePrivateProfileStringW(L"Original",L"SourcePath",source.c_str(),metadata.c_str());
        if(!backup.empty())return source;
        auto extension=std::filesystem::path(source).extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        if(extension!=L".jpg"&&extension!=L".jpeg"&&extension!=L".png"&&extension!=L".bmp")extension=L".jpg";
        const auto saved=root/(L"original-wallpaper"+extension),temporary=root/L"original-wallpaper.tmp";
        if(CopyFileW(source.c_str(),temporary.c_str(),FALSE))MoveFileExW(temporary.c_str(),saved.c_str(),MOVEFILE_WRITE_THROUGH);
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
                if (!name.empty() && !parsing.empty()) entries.push_back({std::move(name),std::move(parsing)});
                item.Reset();
            }
            std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
                return CompareStringOrdinal(a.name.c_str(),-1,b.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;
            });
            std::scoped_lock lock(resultMutex_); apps_=std::move(entries); appError_.clear();
        });
    }
    void ApplyWallpaperAsync(std::wstring path){Queue([this,path=std::move(path)]{
        HRESULT result=E_FAIL;try{result=ApplyWallpaper(path);}catch(...){result=E_FAIL;}
        PostMessageW(window_,WallpaperApplied,SUCCEEDED(result),0);
    },0);}
    void DiscoverWallpapers(std::wstring folder={}) {
        unsigned generation=++wallpaperGeneration_;
        Queue([this,generation,folder=std::move(folder)] {
            auto cancelled=[this,generation]{return Stopping()||wallpaperGeneration_!=generation;};
            if(cancelled())return;
            if(folder.empty() && !personalWallpaperCaptured_) {
                personalWallpaperPath_=PreservePersonalWallpaper();personalWallpaperCaptured_=true;
            }
            auto entries=folder.empty()?LoadDefaultWallpapers(cancelled,personalWallpaperPath_):LoadWallpapers(folder,cancelled);
            if(cancelled())return;
            std::scoped_lock lock(resultMutex_);
            wallpapers_=std::move(entries);
            wallpaperError_=wallpapers_.empty()?L"No wallpapers found. Choose another folder.":L"";
        },2,generation);
    }
    void RequestWallpaperDetail(std::wstring path){
        {std::scoped_lock lock(resultMutex_);if(path==detailPath_)return;detailPath_=path;}
        unsigned generation=++detailGeneration_;
        Queue([this,generation,path=std::move(path)]{
            if(detailGeneration_!=generation||Stopping())return;
            ComPtr<IWICImagingFactory> images;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&images)));auto image=Decode(images.Get(),path,640,400);
            if(detailGeneration_!=generation||Stopping())return;
            std::scoped_lock lock(resultMutex_);for(auto& entry:wallpapers_){entry.detailPixels.reset();if(entry.path==path){entry.detailPixels=image.pixels;entry.detailWidth=image.width;entry.detailHeight=image.height;}}
        },3,generation);
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

class AppPreferences {
    std::wstring path_;
public:
    std::vector<std::wstring> favorites,recent;
    AppPreferences(){PWSTR local=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){path_=std::wstring(local)+L"\\Dako\\apps.ini";CoTaskMemFree(local);}}
    void Load(){for(auto section:{L"Favorites",L"Recent"}){auto& list=section==std::wstring_view(L"Favorites")?favorites:recent;list.clear();for(int i=0;i<64;++i){wchar_t value[4096]{};auto key=std::to_wstring(i);GetPrivateProfileStringW(section,key.c_str(),L"",value,4096,path_.c_str());if(value[0])list.emplace_back(value);}}}
    bool Save(){if(path_.empty())return false;std::error_code error;std::filesystem::create_directories(std::filesystem::path(path_).parent_path(),error);if(error)return false;
        std::wstring data;for(auto section:{L"Favorites",L"Recent"}){data+=L"["+std::wstring(section)+L"]\r\n";const auto& list=section==std::wstring_view(L"Favorites")?favorites:recent;for(size_t i=0;i<list.size()&&i<64;++i){std::wstring value=list[i];value.erase(std::remove(value.begin(),value.end(),L'\n'),value.end());value.erase(std::remove(value.begin(),value.end(),L'\r'),value.end());data+=std::to_wstring(i)+L"="+value+L"\r\n";}}
        std::wstring temporary=path_+L".tmp";std::ofstream file(temporary,std::ios::binary|std::ios::trunc);const WORD bom=0xFEFF;file.write(reinterpret_cast<const char*>(&bom),sizeof(bom));file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()*sizeof(wchar_t)));file.close();return file&&MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
};

class TaskStore {
    std::wstring path_;
public:
    explicit TaskStore(std::wstring path={}) {
        if(!path.empty()){path_=std::move(path);return;}
        PWSTR local=nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) {
            path_=std::wstring(local)+L"\\Dako\\tasks.ini"; CoTaskMemFree(local);
        }
    }
    const std::wstring& Path() const { return path_; }
    std::vector<std::pair<std::wstring,bool>> Load() const {
        std::vector<std::pair<std::wstring,bool>> tasks;
        if (path_.empty()) return tasks;
        for (int i=0;i<5;++i) {
            auto key=L"task"+std::to_wstring(i);
            wchar_t value[256]{}; GetPrivateProfileStringW(L"Tasks",key.c_str(),L"",value,256,path_.c_str());
            if (value[0] && value[1]==L'|') tasks.emplace_back(value+2,value[0]==L'1');
        }
        return tasks;
    }
    bool Save(const std::vector<std::pair<std::wstring,bool>>& tasks) const {
        if (path_.empty()) return false;
        std::error_code error; std::filesystem::create_directories(std::filesystem::path(path_).parent_path(),error);
        if (error) return false;
        std::wstring data=L"[Tasks]\r\n";
        for (size_t i=0;i<tasks.size() && i<5;++i) data+=L"task"+std::to_wstring(i)+L"="+(tasks[i].second?L"1|":L"0|")+tasks[i].first+L"\r\n";
        // UTF-16 INI, committed by replacing a complete temporary file.
        const std::wstring temporary=path_+L".tmp";
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) return false;
        const WORD bom=0xFEFF; DWORD written=0; DWORD bytes=static_cast<DWORD>(data.size()*sizeof(wchar_t));
        bool success=WriteFile(file,&bom,sizeof(bom),&written,nullptr) && written==sizeof(bom) &&
            WriteFile(file,data.data(),bytes,&written,nullptr) && written==bytes && FlushFileBuffers(file);
        CloseHandle(file);
        return success && MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
};
