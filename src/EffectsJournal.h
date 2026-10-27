#pragma once
#include "SettingsStore.h"
#include <functional>
#include <mutex>
namespace effects {
using Values = SettingsStore::Section;
inline std::recursive_mutex mutex;
inline std::wstring activePath;
inline std::map<std::wstring, Values> captured;
inline std::wstring (*createHook)() = nullptr;
inline void (*restoreHook)(const std::wstring &) = nullptr;
inline void Activate(const std::wstring &path) {
    std::scoped_lock lock(mutex);
    activePath = path;
    captured.clear();
}
struct Entry {
    std::wstring section;
    Values values;
};
inline std::vector<Entry> Entries(const std::wstring &path) {
    SettingsStore store(path);
    auto meta = store.Get(L"Recovery");
    size_t count = 0;
    try {
        count = std::min(size_t{128}, static_cast<size_t>(std::stoul(meta[L"Count"])));
    } catch (...) {
    }
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i) {
        auto key = L"Effect" + std::to_wstring(i);
        auto values = store.Get(key);
        if (!values.empty())
            entries.push_back({std::move(key), std::move(values)});
    }
    return entries;
}
class Transaction {
    std::unique_lock<std::recursive_mutex> lock_{mutex};

  public:
    bool Remember(const std::wstring &kind, const std::wstring &identity, Values values) {
        if (activePath.empty())
            return false;
        auto cacheKey = kind + L"|" + identity;
        if (captured.contains(cacheKey))
            return true;
        auto entries = Entries(activePath);
        for (const auto &entry : entries) {
            auto old = entry.values;
            if (old[L"Kind"] == kind && old[L"Identity"] == identity) {
                captured[cacheKey] = old;
                return true;
            }
        }
        if (entries.size() >= 128)
            return false;
        values[L"Kind"] = kind;
        values[L"Identity"] = identity;
        SettingsStore store(activePath);
        if (!store.Update(L"Effect" + std::to_wstring(entries.size()), std::move(values)))
            return false;
        bool ok = store.Update(L"Recovery", {{L"Count", std::to_wstring(entries.size() + 1)},
                                             {L"Owner", std::to_wstring(GetCurrentProcessId())}});
        if (ok)
            captured[cacheKey] = {{L"Captured", L"1"}};
        return ok;
    }
};
inline bool RestoreEntries(const std::wstring &path, const std::function<bool(const Values &)> &apply) {
    auto entries = Entries(path);
    bool complete = true;
    SettingsStore store(path);
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->values[L"Restored"] == L"1")
            continue;
        if (apply(it->values)) {
            it->values[L"Restored"] = L"1";
            complete = store.Update(it->section, it->values) && complete;
        } else
            complete = false;
    }
    return complete;
}
bool RememberWallpaper(IUnknown *desktop);
std::wstring Create();
void Restore(const std::wstring &path);
bool Retry(const std::wstring &path);
void Register();
}
