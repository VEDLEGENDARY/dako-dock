#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace awake {
enum class Fact { Unknown, No, Yes };
struct Rules {
    bool ac = true, external = false, display = false;
    std::vector<std::wstring> apps;
    bool HasCondition() const { return ac || external || !apps.empty(); }
};
inline bool Equal(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
inline std::optional<std::vector<std::wstring>> ParseApps(const std::wstring &input) {
    if (input.size() > 654 || input.find(L'\0') != std::wstring::npos) return {};
    std::vector<std::wstring> apps;
    size_t begin = 0;
    while (begin < input.size()) {
        size_t end = input.find(L';', begin);
        auto name = input.substr(begin, end == std::wstring::npos ? end : end - begin);
        auto first = name.find_first_not_of(L" \t");
        if (first == std::wstring::npos) return {};
        name = name.substr(first, name.find_last_not_of(L" \t") - first + 1);
        if (name.size() <= 4 || name.size() > 80 || name.front() == L'.' ||
            name.find_first_of(L"/\\:*?\"<>|") != std::wstring::npos ||
            std::any_of(name.begin(), name.end(), [](wchar_t ch) { return ch < 32 || ch == 127; }) ||
            !Equal(name.substr(name.size() - 4), L".exe")) return {};
        if (std::none_of(apps.begin(), apps.end(), [&](const auto &other) { return Equal(name, other); }))
            apps.push_back(std::move(name));
        if (apps.size() > 8) return {};
        if (end == std::wstring::npos) break;
        begin = end + 1;
        if (begin == input.size()) return {};
    }
    return apps;
}
inline std::wstring EncodeApps(const std::vector<std::wstring> &apps) {
    std::wstring result;
    for (const auto &app : apps) { if (!result.empty()) result += L"; "; result += app; }
    return result;
}
inline bool Match(const Rules &rules, Fact ac, Fact external, Fact app) {
    return rules.HasCondition() && (!rules.ac || ac == Fact::Yes) &&
        (!rules.external || external == Fact::Yes) && (rules.apps.empty() || app == Fact::Yes);
}
}
