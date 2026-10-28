#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <mutex>
#include <string>
#include <optional>
#include <atomic>
#include "PrivacyRuntime.h"
#include "SettingsIdentity.h"
#include "SettingsSchema.h"

// One atomic UTF-16 store. Section updates preserve every other service's settings.
class SettingsStore {
  public:
    using Section = std::map<std::wstring, std::wstring>;
    using Document = std::map<std::wstring, Section>;
    static constexpr unsigned Version = 4;

  private:
    std::wstring path_;
    inline static std::recursive_mutex mutex_;
    static bool ReadFile(const std::wstring &path, Document &data, bool strict = false) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        auto length = file.tellg();
        if (length < 0 || length > 2 * 1024 * 1024)
            return false;
        std::string bytes(static_cast<size_t>(length), '\0');
        file.seekg(0);
        if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
            return false;
        std::wstring text;
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 255 &&
            static_cast<unsigned char>(bytes[1]) == 254) {
            if (bytes.size() % 2)
                return false;
            for (size_t i = 2; i + 1 < bytes.size(); i += 2)
                text += static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) |
                                             (static_cast<unsigned char>(bytes[i + 1]) << 8));
        } else {
            int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                                            nullptr, 0);
            if (!bytes.empty() && !count)
                return false;
            text.resize(count);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                                text.data(), count);
        }
        if (text.find(L'\0') != std::wstring::npos)
            return false;
        std::wstring section;
        for (size_t start = 0; start < text.size();) {
            size_t end = text.find(L'\n', start);
            auto line = text.substr(start, end == std::wstring::npos ? end : end - start);
            if (!line.empty() && line.back() == L'\r')
                line.pop_back();
            if (line.size() > 2 && line.front() == L'[' && line.back() == L']') {
                section = line.substr(1, line.size() - 2);
                if (strict && (data.contains(section) || section.find_first_of(L"[]\r\n") != std::wstring::npos))
                    return false;
                data.try_emplace(section);
            } else if (auto equal = line.find(L'='); equal != std::wstring::npos && !section.empty()) {
                auto key = line.substr(0, equal);
                if (strict && (key.empty() || data[section].contains(key)))
                    return false;
                data[section][key] = line.substr(equal + 1);
            } else if (strict && !line.empty())
                return false;
            if (end == std::wstring::npos)
                break;
            start = end + 1;
        }
        return true;
    }
    std::optional<Document> Read() const {
        Document data;
        if (path_.empty())
            return data;
        if (std::filesystem::exists(path_)) {
            if (!ReadFile(path_, data, true))
                return std::nullopt;
        } else {
            auto root = std::filesystem::path(path_).parent_path();
            for (auto name : {L"appearance.ini", L"tasks.ini", L"apps.ini"}) {
                auto legacy = root / name;
                if (std::filesystem::exists(legacy) && !ReadFile(legacy.wstring(), data))
                    return std::nullopt;
            }
        }
        return data;
    }
    bool Commit(Document data) const {
        if (path_.empty())
            return false;
        auto schema = data[L"Schema"][L"Version"];
        if (!schema.empty() && schema != L"3" && schema != std::to_wstring(Version))
            return false;
        data[L"Schema"][L"Version"] = std::to_wstring(Version);
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), error);
        if (error)
            return false;
        std::wstring text;
        for (const auto &[name, section] : data) {
            if (name.empty() || name.find_first_of(L"[]\r\n") != std::wstring::npos ||
                name.find(L'\0') != std::wstring::npos)
                return false;
            text += L"[" + name + L"]\r\n";
            for (const auto &[key, value] : section) {
                if (key.empty() || key.find_first_of(L"=\r\n") != std::wstring::npos ||
                    key.find(L'\0') != std::wstring::npos || value.find(L'\0') != std::wstring::npos)
                    return false;
                auto safe = value;
                safe.erase(std::remove(safe.begin(), safe.end(), L'\r'), safe.end());
                safe.erase(std::remove(safe.begin(), safe.end(), L'\n'), safe.end());
                text += key + L"=" + safe + L"\r\n";
            }
        }
        if (text.size() > 1024 * 1024 - 1)
            return false;
        privacy::Write destination(L"settings", L"file-write", path_, L"settings-commit");
        static std::atomic<unsigned> sequence = 0;
        auto temporary = path_ + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        privacy::Write staging(L"settings", L"file-write", temporary, L"atomic-staging");
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        WORD bom = 0xFEFF;
        DWORD written = 0, length = static_cast<DWORD>(text.size() * sizeof(wchar_t));
        bool ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom) &&
                  WriteFile(file, text.data(), length, &written, nullptr) && written == length &&
                  FlushFileBuffers(file);
        CloseHandle(file);
        staging.Complete(ok);
        if (ok) {
            for (int attempt = 0; attempt < 4; ++attempt) {
                ok = MoveFileExW(temporary.c_str(), path_.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
                DWORD failure = ok ? ERROR_SUCCESS : GetLastError();
                if (ok ||
                    (failure != ERROR_SHARING_VIOLATION && failure != ERROR_LOCK_VIOLATION &&
                     failure != ERROR_ACCESS_DENIED) ||
                    attempt == 3)
                    break;
                Sleep(static_cast<DWORD>(15 * (attempt + 1)));
            }
        }
        if (!ok) {
            privacy::Write cleanup(L"settings", L"file-delete", temporary, L"failed-staging-cleanup");
            cleanup.Complete(DeleteFileW(temporary.c_str()) != FALSE);
        }
        return destination.Complete(ok);
    }

  public:
    explicit SettingsStore(std::wstring path = {}) : path_(std::move(path)) {
        if (!path_.empty())
            return;
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            path_ = std::wstring(local) + L"\\Dako\\settings.ini";
            CoTaskMemFree(local);
        }
    }
    const std::wstring &Path() const { return path_; }
    Section Get(const std::wstring &section) const {
        std::scoped_lock lock(mutex_);
        auto data = Read();
        return data ? (*data)[section] : Section{};
    }
    bool Update(const std::wstring &section, Section values) const {
        std::scoped_lock lock(mutex_);
        v4::StoreLock processLock;
        if (!processLock)
            return false;
        auto data = Read();
        if (!data)
            return false;
        (*data)[section] = std::move(values);
        return Commit(std::move(*data));
    }
    bool UpdateSections(Document sections) const {
        std::scoped_lock lock(mutex_);
        v4::StoreLock processLock;
        if (!processLock)
            return false;
        auto data = Read();
        if (!data)
            return false;
        for (auto &[name, values] : sections)
            (*data)[name] = std::move(values);
        return Commit(std::move(*data));
    }
    bool Migrate() const {
        std::scoped_lock lock(mutex_);
        v4::StoreLock processLock;
        if (!processLock)
            return false;
        auto data = Read();
        if (!data) {
            if (!std::filesystem::exists(path_))
                return false;
            std::error_code error;
            std::filesystem::copy_file(path_, path_ + L".corrupt-" + std::to_wstring(GetTickCount64()), error);
            return !error && Commit({});
        }
        auto schema = (*data)[L"Schema"][L"Version"];
        if (schema.empty() || schema == L"3") {
            (*data)[L"Privacy"][L"Offline"] = L"1";
            (*data)[L"V4"][L"Offline"] = L"1";
            if ((*data)[L"Dako"][L"layoutMode"] == L"bar")
                (*data)[L"Dako"][L"layoutMode"] = L"dock";
            return Commit(std::move(*data));
        }
        return schema == std::to_wstring(Version);
    }
    bool Export(const std::wstring &path) const {
        std::scoped_lock lock(mutex_);
        v4::StoreLock processLock;
        if (!processLock)
            return false;
        if (std::filesystem::path(path).lexically_normal() == std::filesystem::path(path_).lexically_normal())
            return false;
        auto data = Read();
        if (!data)
            return false;
        (*data)[L"DakoExport"][L"Format"] = L"1";
        return SettingsStore(path).Commit(std::move(*data));
    }
    bool Import(const std::wstring &path) const {
        std::scoped_lock lock(mutex_);
        v4::StoreLock processLock;
        if (!processLock)
            return false;
        Document incoming;
        if (!ReadFile(path, incoming, true) || incoming[L"DakoExport"][L"Format"] != L"1" ||
            (incoming[L"Schema"][L"Version"] != L"3" && incoming[L"Schema"][L"Version"] != std::to_wstring(Version)))
            return false;
        for (const auto &[key, value] : incoming[L"V4"])
            if (auto setting = v4::Find(key); setting && !v4::Valid(*setting, value))
                return false;
        auto current = Read();
        if (!current)
            return false;
        auto version = (*current)[L"Schema"][L"Version"];
        if (!version.empty() && version != L"3" && version != std::to_wstring(Version))
            return false;
        for (auto key : {L"DesktopMode", L"DesktopPromptDone"}) {
            auto &consent = (*current)[L"Dako"];
            if (consent.contains(key))
                incoming[L"Dako"][key] = consent[key];
            else
                incoming[L"Dako"].erase(key);
        }
        if (privacy::Policy().Offline() || (*current)[L"Privacy"][L"Offline"] == L"1")
            incoming[L"Privacy"][L"Offline"] = L"1";
        incoming[L"V4"][L"ReplaceTaskbar"] = (*current)[L"V4"][L"ReplaceTaskbar"];
        if ((*current)[L"Privacy"][L"Offline"] == L"1" || privacy::Policy().Offline())
            incoming[L"V4"][L"Offline"] = L"1";
        incoming.erase(L"DakoExport");
        incoming.erase(L"ModuleConsent");
        static std::atomic<unsigned> imports = 0;
        auto backup = path_ + L".backup-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++imports);
        if (!SettingsStore(backup).Commit(*current))
            return false;
        return Commit(std::move(incoming));
    }
    static std::wstring Scope(const std::wstring &section, const std::wstring &monitor, const std::wstring &app) {
        if (section.empty() || section.find_first_of(L"@[]\r\n") != std::wstring::npos || monitor.size() > 256 ||
            app.size() > 256)
            return {};
        auto encode = [](const std::wstring &value) {
            constexpr wchar_t digits[] = L"0123456789abcdef";
            std::wstring result;
            for (wchar_t item : value) {
                for (int shift = 12; shift >= 0; shift -= 4)
                    result += digits[(item >> shift) & 15];
            }
            return result;
        };
        return section + L"@monitor=" + encode(monitor) + L"@app=" + encode(app);
    }
    Section Effective(const std::wstring &section, const std::wstring &monitor = {},
                      const std::wstring &app = {}) const {
        std::scoped_lock lock(mutex_);
        auto data = Read();
        if (!data)
            return {};
        auto result = (*data)[section];
        auto merge = [&](const std::wstring &name) {
            for (const auto &[key, value] : (*data)[name])
                result[key] = value;
        };
        if (!monitor.empty())
            merge(Scope(section, monitor, {}));
        if (!app.empty())
            merge(Scope(section, {}, app));
        if (!monitor.empty() && !app.empty())
            merge(Scope(section, monitor, app));
        return result;
    }
    bool Override(const std::wstring &section, const std::wstring &monitor, const std::wstring &app,
                  Section values) const {
        auto scoped = Scope(section, monitor, app);
        return !scoped.empty() && (!monitor.empty() || !app.empty()) && Update(scoped, std::move(values));
    }
};

class AppPreferences {
  public:
    std::vector<std::wstring> favorites, recent;
    void Load() {
        SettingsStore store;
        for (auto name : {L"Favorites", L"Recent"}) {
            auto values = store.Get(name);
            auto &list = std::wstring_view(name) == L"Favorites" ? favorites : recent;
            list.clear();
            for (int i = 0; i < 64; ++i) {
                auto value = values[std::to_wstring(i)];
                if (!value.empty())
                    list.push_back(value);
            }
        }
    }
    bool Save() {
        SettingsStore store;
        bool ok = true;
        for (auto name : {L"Favorites", L"Recent"}) {
            SettingsStore::Section values;
            const auto &list = std::wstring_view(name) == L"Favorites" ? favorites : recent;
            for (size_t i = 0; i < list.size() && i < 64; ++i)
                values[std::to_wstring(i)] = list[i];
            ok = store.Update(name, std::move(values)) && ok;
        }
        return ok;
    }
};

class TaskStore {
    SettingsStore store_;

  public:
    static constexpr size_t Capacity = 20;
    explicit TaskStore(std::wstring path = {}) : store_(std::move(path)) {}
    const std::wstring &Path() const { return store_.Path(); }
    std::vector<std::pair<std::wstring, bool>> Load() const {
        std::vector<std::pair<std::wstring, bool>> tasks;
        auto section = store_.Get(L"Tasks");
        for (size_t i = 0; i < Capacity; ++i) {
            auto value = section[L"task" + std::to_wstring(i)];
            if (value.size() > 2 && value[1] == L'|')
                tasks.emplace_back(value.substr(2), value[0] == L'1');
        }
        return tasks;
    }
    bool Save(const std::vector<std::pair<std::wstring, bool>> &tasks) const {
        SettingsStore::Section section;
        for (size_t i = 0; i < tasks.size() && i < Capacity; ++i)
            section[L"task" + std::to_wstring(i)] = (tasks[i].second ? L"1|" : L"0|") + tasks[i].first;
        return store_.Update(L"Tasks", std::move(section));
    }
};
