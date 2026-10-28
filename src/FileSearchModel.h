#pragma once
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <iterator>

namespace files {
struct Result {
    std::wstring name, path;
};
inline constexpr size_t ResultLimit = 50;
inline bool LocalPath(const std::wstring &path) {
    return path.size() >= 3 && path.size() <= 32767 &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/') && path.find(L'\0') == std::wstring::npos &&
           path.find(L':', 2) == std::wstring::npos && path.find_first_of(L"\r\n") == std::wstring::npos;
}
inline std::optional<std::wstring> LocalUrl(const std::wstring &url) {
    if (url.size() > 32768 || url.find(L'\0') != std::wstring::npos || url.size() < 6 ||
        _wcsnicmp(url.c_str(), L"file:", 5))
        return {};
    wchar_t path[32768]{};
    DWORD size = 32768;
    if (FAILED(PathCreateFromUrlW(url.c_str(), path, &size, 0)) || !LocalPath(path))
        return {};
    return std::wstring(path);
}
inline bool ConfirmOpen(const std::wstring &path) {
    auto extension = std::filesystem::path(path).extension().wstring();
    for (auto &c : extension)
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c + L'a' - L'A');
    const wchar_t *types[]{L".exe", L".com",       L".bat", L".cmd", L".ps1", L".vbs", L".vbe", L".js",  L".jse",
                           L".wsf", L".wsh",       L".msi", L".msp", L".reg", L".scr", L".hta", L".lnk", L".url",
                           L".scf", L".appref-ms", L".py",  L".pyw", L".pl",  L".rb",  L".sh"};
    return std::any_of(std::begin(types), std::end(types), [&](const wchar_t *type) { return extension == type; });
}
inline std::optional<std::wstring> Sql(const std::wstring &query) {
    if (query.size() < 2 || query.size() > 256 || query.find(L'\0') != std::wstring::npos ||
        query.find_first_of(L"\r\n\t") != std::wstring::npos)
        return {};
    std::wstring literal;
    for (wchar_t c : query) {
        if (c == L'%')
            literal += L"[%]";
        else if (c == L'_')
            literal += L"[_]";
        else if (c == L'[')
            literal += L"[[]";
        else {
            literal += c;
            if (c == L'\'')
                literal += L'\'';
        }
    }
    // Only a quoted filename pattern enters the fixed read-only local catalog query.
    return L"SELECT TOP 50 System.ItemNameDisplay, System.ItemUrl FROM SystemIndex WHERE SCOPE='file:' "
           L"AND System.ItemNameDisplay LIKE '" +
           literal + L"%'";
}
inline std::optional<std::vector<std::byte>> Encode(const std::vector<Result> &results) {
    if (results.size() > ResultLimit)
        return {};
    std::vector<std::byte> bytes;
    auto integer = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 255));
    };
    auto string = [&](const std::wstring &text) {
        integer(static_cast<uint32_t>(text.size()));
        const auto *data = reinterpret_cast<const std::byte *>(text.data());
        bytes.insert(bytes.end(), data, data + text.size() * sizeof(wchar_t));
    };
    integer(0x444b4653);
    integer(1);
    integer(static_cast<uint32_t>(results.size()));
    for (const auto &result : results) {
        if (result.name.empty() || result.name.size() > 512 || result.name.find(L'\0') != std::wstring::npos ||
            !LocalPath(result.path))
            return {};
        string(result.name);
        string(result.path);
    }
    if (bytes.size() > 2 * 1024 * 1024)
        return {};
    return bytes;
}
inline std::optional<std::vector<Result>> Decode(const std::vector<std::byte> &bytes) {
    if (bytes.size() > 2 * 1024 * 1024)
        return {};
    size_t at = 0;
    bool valid = true;
    auto integer = [&] {
        uint32_t value = 0;
        if (at > bytes.size() || bytes.size() - at < 4) {
            valid = false;
            return value;
        }
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(bytes[at++]) << (i * 8);
        return value;
    };
    auto string = [&](size_t limit) {
        auto size = integer();
        if (!valid || size > limit || at > bytes.size() ||
            static_cast<size_t>(size) * sizeof(wchar_t) > bytes.size() - at) {
            valid = false;
            return std::wstring{};
        }
        std::wstring value(size, 0);
        std::memcpy(value.data(), bytes.data() + at, size * sizeof(wchar_t));
        at += size * sizeof(wchar_t);
        return value;
    };
    if (integer() != 0x444b4653 || integer() != 1)
        return {};
    auto count = integer();
    if (!valid || count > ResultLimit)
        return {};
    std::vector<Result> results;
    for (uint32_t i = 0; i < count; ++i) {
        Result result{string(512), string(32767)};
        if (!valid || result.name.empty() || result.name.find(L'\0') != std::wstring::npos || !LocalPath(result.path))
            return {};
        results.push_back(std::move(result));
    }
    if (at != bytes.size())
        return {};
    return results;
}
} // namespace files
