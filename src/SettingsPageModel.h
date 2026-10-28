#pragma once
#include <windows.h>
#include <appmodel.h>
#include <optional>
#include <string>
#include <string_view>
#include <cwchar>

namespace settingspage {
inline bool Prefix(const std::wstring &text) {
    return text.size() >= 12 && !_wcsnicmp(text.c_str(), L"ms-settings:", 12);
}
inline bool Unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}
inline int Hex(wchar_t c) {
    return c >= L'0' && c <= L'9'   ? c - L'0'
           : c >= L'a' && c <= L'f' ? c - L'a' + 10
           : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                    : -1;
}
inline std::optional<std::wstring> DecodeValue(std::wstring_view text) {
    if (text.empty() || text.size() > 1000)
        return {};
    std::string bytes;
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c == L'%') {
            if (i + 2 >= text.size() || Hex(text[i + 1]) < 0 || Hex(text[i + 2]) < 0)
                return {};
            bytes.push_back(static_cast<char>((Hex(text[i + 1]) << 4) | Hex(text[i + 2])));
            i += 2;
        } else if (c < 128 && (Unreserved(static_cast<unsigned char>(c)) || c == L'{' || c == L'}' || c == L'!'))
            bytes.push_back(static_cast<char>(c));
        else
            return {}; // Delimiters, spaces and non-ASCII input must be URI-escaped in a value.
    }
    int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0 || count > 512)
        return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), result.data(),
                            count) != count)
        return {};
    for (auto c : result)
        if (c < 32 || c == 127)
            return {};
    return result;
}
inline std::optional<std::wstring> EncodeValue(const std::wstring &value) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (count <= 0 || count > 2048)
        return {};
    std::string bytes(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), bytes.data(),
                            count, nullptr, nullptr) != count)
        return {};
    std::wstring result;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : bytes) {
        if (Unreserved(c))
            result += static_cast<wchar_t>(c);
        else {
            result += L'%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
inline std::optional<std::wstring> Parameters(const std::wstring &page, std::wstring_view query) {
    auto equals = query.find(L'=');
    std::wstring key;
    std::wstring_view encoded;
    if (page == L"appsfeatures-app") {
        if (equals != std::wstring_view::npos)
            return {};
        encoded = query;
    } else {
        if (equals == std::wstring_view::npos)
            return {};
        key = query.substr(0, equals);
        encoded = query.substr(equals + 1);
        bool allowed = (page == L"defaultapps" &&
                        (key == L"registeredAppUser" || key == L"registeredAppMachine" || key == L"registeredAUMID")) ||
                       (page == L"camera" && key == L"cameraId") ||
                       (page == L"sound-properties" && (key == L"endpointId" || key == L"interfaceId"));
        if (!allowed)
            return {};
    }
    auto value = DecodeValue(encoded);
    if (!value || (page == L"appsfeatures-app" && VerifyPackageFamilyName(value->c_str()) != ERROR_SUCCESS))
        return {};
    auto canonical = EncodeValue(*value);
    if (!canonical)
        return {};
    return key.empty() ? *canonical : key + L"=" + *canonical;
}
inline std::optional<std::wstring> Uri(std::wstring text) {
    if (text.size() > 1024 || text.find(L'\0') != std::wstring::npos ||
        text.find_first_of(L"\r\n\t") != std::wstring::npos)
        return {};
    while (!text.empty() && text.front() == L' ')
        text.erase(text.begin());
    while (!text.empty() && text.back() == L' ')
        text.pop_back();
    if (Prefix(text))
        text.erase(0, 12);
    auto split = text.find(L'?');
    auto page = text.substr(0, split);
    if (page.size() > 96)
        return {};
    for (auto &c : page) {
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c + L'a' - L'A');
        if ((c < L'a' || c > L'z') && (c < L'0' || c > L'9') && c != L'-')
            return {};
    }
    std::wstring result = L"ms-settings:" + page;
    if (split != std::wstring::npos) {
        auto parameters = Parameters(page, std::wstring_view(text).substr(split + 1));
        if (!parameters)
            return {};
        result += L"?" + *parameters;
    }
    return result.size() <= 1024 ? std::optional<std::wstring>(std::move(result)) : std::nullopt;
}
} // namespace settingspage
