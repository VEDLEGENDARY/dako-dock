#pragma once
#include "ClipboardTransfer.h"
#include <optional>
#include <cstring>
#include <limits>

namespace systemactions {
struct BinTotals {
    uint64_t items = 0, bytes = 0;
};
struct Drive {
    std::wstring id, label, roots;
    uint64_t arrived = 0;
};
inline bool Text(const std::wstring &text, size_t limit) {
    if (text.empty() || text.size() > limit)
        return false;
    for (auto c : text)
        if (c < 32 || c == 127)
            return false;
    return true;
}
inline bool Roots(const std::wstring &roots) {
    if (roots.empty() || roots.size() > 26 * 4 || roots.size() % 4)
        return false;
    unsigned seen = 0;
    for (size_t at = 0; at < roots.size(); at += 4) {
        wchar_t drive = roots[at];
        if (drive < L'A' || drive > L'Z' || roots.substr(at + 1, 3) != L":\\ ")
            return false;
        unsigned bit = 1U << (drive - L'A');
        if (seen & bit)
            return false;
        seen |= bit;
    }
    return true;
}
inline bool Valid(const Drive &drive) {
    return Text(drive.id, 512) && Text(drive.label, 256) && Roots(drive.roots) && drive.arrived;
}
inline bool SameTarget(const Drive &selected, const Drive &current) {
    return Valid(selected) && Valid(current) && selected.arrived == current.arrived &&
           selected.roots == current.roots &&
           CompareStringOrdinal(selected.id.c_str(), -1, current.id.c_str(), -1, TRUE) == CSTR_EQUAL;
}
inline clipboard::Payload Encode(const BinTotals &totals) {
    clipboard::Payload payload;
    payload.format = CF_PRIVATEFIRST;
    payload.bytes.resize(2 * sizeof(uint64_t));
    std::memcpy(payload.bytes.data(), &totals.items, sizeof(uint64_t));
    std::memcpy(payload.bytes.data() + sizeof(uint64_t), &totals.bytes, sizeof(uint64_t));
    return payload;
}
inline std::optional<BinTotals> DecodeTotals(const clipboard::Payload &payload) {
    if (payload.format != CF_PRIVATEFIRST || payload.bytes.size() != 2 * sizeof(uint64_t))
        return {};
    BinTotals totals;
    std::memcpy(&totals.items, payload.bytes.data(), sizeof(uint64_t));
    std::memcpy(&totals.bytes, payload.bytes.data() + sizeof(uint64_t), sizeof(uint64_t));
    if (totals.items > INT64_MAX || totals.bytes > INT64_MAX)
        return {};
    return totals;
}
inline std::optional<clipboard::Payload> Encode(const std::vector<Drive> &drives) {
    if (drives.size() > 32)
        return {};
    clipboard::Payload payload;
    payload.format = CF_PRIVATEFIRST;
    auto integer = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            payload.bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 255));
    };
    auto text = [&](const std::wstring &value) {
        integer(static_cast<uint32_t>(value.size()));
        const auto *bytes = reinterpret_cast<const std::byte *>(value.data());
        payload.bytes.insert(payload.bytes.end(), bytes, bytes + value.size() * sizeof(wchar_t));
    };
    integer(0x444b4452);
    integer(1);
    integer(static_cast<uint32_t>(drives.size()));
    for (size_t index = 0; index < drives.size(); ++index) {
        const auto &drive = drives[index];
        if (!Valid(drive))
            return {};
        for (size_t previous = 0; previous < index; ++previous)
            if (CompareStringOrdinal(drives[previous].id.c_str(), -1, drive.id.c_str(), -1, TRUE) == CSTR_EQUAL)
                return {};
        text(drive.id);
        text(drive.label);
        text(drive.roots);
        integer(static_cast<uint32_t>(drive.arrived));
        integer(static_cast<uint32_t>(drive.arrived >> 32));
    }
    if (payload.bytes.size() > clipboard::TransferLimit)
        return {};
    return payload;
}
inline std::optional<std::vector<Drive>> DecodeDrives(const clipboard::Payload &payload) {
    if (payload.format != CF_PRIVATEFIRST || payload.bytes.size() > clipboard::TransferLimit)
        return {};
    size_t at = 0;
    bool valid = true;
    auto integer = [&] {
        uint32_t value = 0;
        if (at > payload.bytes.size() || payload.bytes.size() - at < 4) {
            valid = false;
            return value;
        }
        for (unsigned i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(payload.bytes[at++]) << (i * 8);
        return value;
    };
    auto text = [&](size_t limit) {
        uint32_t size = integer();
        if (!valid || size > limit || at > payload.bytes.size() || size * sizeof(wchar_t) > payload.bytes.size() - at) {
            valid = false;
            return std::wstring{};
        }
        std::wstring value(size, 0);
        std::memcpy(value.data(), payload.bytes.data() + at, size * sizeof(wchar_t));
        at += size * sizeof(wchar_t);
        return value;
    };
    if (integer() != 0x444b4452 || integer() != 1)
        return {};
    auto count = integer();
    if (!valid || count > 32)
        return {};
    std::vector<Drive> drives;
    for (uint32_t i = 0; i < count; ++i) {
        Drive drive{text(512), text(256), text(26 * 4)};
        auto low = integer();
        auto high = integer();
        drive.arrived = low | (static_cast<uint64_t>(high) << 32);
        if (!valid || !Valid(drive))
            return {};
        for (const auto &previous : drives)
            if (CompareStringOrdinal(previous.id.c_str(), -1, drive.id.c_str(), -1, TRUE) == CSTR_EQUAL)
                return {};
        drives.push_back(std::move(drive));
    }
    if (at != payload.bytes.size())
        return {};
    return drives;
}
} // namespace systemactions
