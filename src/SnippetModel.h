#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace snippets {
struct Entry { uint64_t id = 0; std::wstring title, text; };
inline bool Valid(const Entry &entry) {
    return entry.id && !entry.title.empty() && entry.title.size() <= 80 && !entry.text.empty() &&
        entry.text.size() <= 2048 && entry.title.find_first_of(L"\r\n\t") == std::wstring::npos &&
        entry.title.find(L'\0') == std::wstring::npos && entry.text.find(L'\0') == std::wstring::npos;
}
inline std::wstring Encode(const std::vector<Entry> &entries) {
    if (entries.size() > 64) return {};
    const wchar_t *digits = L"0123456789abcdef";
    auto hex = [&](const std::wstring &text) {
        std::wstring result;
        for (wchar_t c : text) for (int shift = 12; shift >= 0; shift -= 4) result += digits[(c >> shift) & 15];
        return result;
    };
    std::wstring result = L"1";
    for (const auto &entry : entries) {
        if (!Valid(entry)) return {};
        result += L";" + std::to_wstring(entry.id) + L":" + hex(entry.title) + L":" + hex(entry.text);
    }
    return result;
}
inline std::optional<std::vector<Entry>> Decode(const std::wstring &value) {
    if (value.empty() || value.front() != L'1' || value.size() > 600000) return {};
    auto unhex = [](std::wstring_view text) -> std::optional<std::wstring> {
        if (text.size() % 4 || text.size() > 8192) return {};
        std::wstring result;
        for (size_t at = 0; at < text.size(); at += 4) {
            unsigned code = 0;
            for (size_t i = 0; i < 4; ++i) {
                wchar_t c = text[at + i];
                int digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
                if (digit < 0) return {};
                code = code * 16 + static_cast<unsigned>(digit);
            }
            result += static_cast<wchar_t>(code);
        }
        return result;
    };
    std::vector<Entry> entries;
    size_t at = 1;
    while (at < value.size()) {
        if (value[at++] != L';' || entries.size() >= 64) return {};
        size_t end = value.find(L';', at);
        auto row = std::wstring_view(value).substr(at, end == std::wstring::npos ? value.size() - at : end - at);
        auto first = row.find(L':'), second = first == std::wstring_view::npos ? first : row.find(L':', first + 1);
        if (first == std::wstring_view::npos || second == std::wstring_view::npos || first == 0 || first > 20 ||
            row.substr(0, first).find_first_not_of(L"0123456789") != std::wstring_view::npos) return {};
        uint64_t id = 0;
        try { id = std::stoull(std::wstring(row.substr(0, first))); } catch (...) { return {}; }
        auto title = unhex(row.substr(first + 1, second - first - 1)), text = unhex(row.substr(second + 1));
        if (!title || !text) return {};
        Entry entry{id, std::move(*title), std::move(*text)};
        if (!Valid(entry) || std::any_of(entries.begin(), entries.end(), [&](const Entry &other) { return other.id == id; })) return {};
        entries.push_back(std::move(entry));
        at = end == std::wstring::npos ? value.size() : end;
    }
    return entries;
}
inline std::optional<Entry> Parse(const std::wstring &input, uint64_t id) {
    size_t separator = input.find(L'|');
    if (separator == std::wstring::npos) return {};
    Entry entry{id, input.substr(0, separator), {}};
    while (!entry.title.empty() && entry.title.back() == L' ') entry.title.pop_back();
    while (!entry.title.empty() && entry.title.front() == L' ') entry.title.erase(entry.title.begin());
    size_t at = separator + 1;
    if (at < input.size() && input[at] == L' ') ++at;
    for (; at < input.size(); ++at) {
        wchar_t c = input[at];
        if (c == L'\\' && at + 1 < input.size()) {
            wchar_t next = input[at + 1];
            if (next == L'n' || next == L't' || next == L'\\') {
                entry.text += next == L'n' ? L'\n' : next == L't' ? L'\t' : L'\\'; ++at; continue;
            }
        }
        entry.text += c;
    }
    return Valid(entry) ? std::make_optional(std::move(entry)) : std::nullopt;
}
}
