#pragma once
#include "ClipboardTransfer.h"
#include "ClipboardModel.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <memory>
#include <optional>

namespace clipboard {
struct Image {
    UINT width = 0, height = 0;
    std::vector<std::byte> bgra;
};
inline std::shared_ptr<Image> DecodeImage(const Payload &payload) {
    if ((payload.format != CF_DIB && payload.format != CF_DIBV5) ||
        payload.bytes.size() < sizeof(BITMAPINFOHEADER) || payload.bytes.size() > TransferLimit) return {};
    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.bytes.data(), sizeof(header));
    if ((header.biSize != 40 && header.biSize != 108 && header.biSize != 124) ||
        header.biSize > payload.bytes.size() || header.biPlanes != 1 || header.biWidth <= 0 ||
        header.biWidth > 4096 || header.biHeight == 0 || header.biHeight < -4096 || header.biHeight > 4096 ||
        (header.biBitCount != 24 && header.biBitCount != 32) || header.biClrUsed ||
        (header.biCompression != BI_RGB && header.biCompression != BI_BITFIELDS)) return {};
    size_t offset = header.biSize;
    if (header.biCompression == BI_BITFIELDS) {
        if (header.biBitCount != 32) return {};
        DWORD masks[3]{};
        size_t masksOffset = sizeof(BITMAPINFOHEADER);
        if (payload.bytes.size() < masksOffset + sizeof(masks)) return {};
        std::memcpy(masks, payload.bytes.data() + masksOffset, sizeof(masks));
        if (masks[0] != 0x00ff0000 || masks[1] != 0x0000ff00 || masks[2] != 0x000000ff) return {};
        if (header.biSize == sizeof(BITMAPINFOHEADER)) offset += sizeof(masks);
    }
    UINT width = static_cast<UINT>(header.biWidth), height = static_cast<UINT>(std::abs(header.biHeight));
    size_t stride = ((static_cast<size_t>(width) * header.biBitCount + 31) / 32) * 4;
    if (static_cast<size_t>(width) * height > TransferLimit / 4 || offset > payload.bytes.size() ||
        stride * height > payload.bytes.size() - offset) return {};
    auto image = std::make_shared<Image>();
    image->width = width; image->height = height;
    image->bgra.resize(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        const auto *source = payload.bytes.data() + offset + stride * (header.biHeight < 0 ? y : height - y - 1);
        auto *target = image->bgra.data() + static_cast<size_t>(width) * y * 4;
        for (UINT x = 0; x < width; ++x) {
            std::memcpy(target + static_cast<size_t>(x) * 4, source + static_cast<size_t>(x) * (header.biBitCount / 8), 3);
            target[static_cast<size_t>(x) * 4 + 3] = std::byte{255};
        }
    }
    return image;
}
inline std::optional<std::vector<std::wstring>> FileList(const Payload &payload) {
    if (payload.format != CF_HDROP || payload.bytes.size() < sizeof(DROPFILES) + 4 ||
        payload.bytes.size() > TransferLimit) return {};
    DROPFILES header{};
    std::memcpy(&header, payload.bytes.data(), sizeof(header));
    if (!header.fWide || header.pFiles < sizeof(header) || header.pFiles % sizeof(wchar_t) ||
        header.pFiles >= payload.bytes.size() || (payload.bytes.size() - header.pFiles) % sizeof(wchar_t)) return {};
    std::wstring list((payload.bytes.size() - header.pFiles) / sizeof(wchar_t), 0);
    std::memcpy(list.data(), payload.bytes.data() + header.pFiles, list.size() * sizeof(wchar_t));
    std::vector<std::wstring> paths;
    for (size_t at = 0; at < list.size();) {
        size_t end = list.find(L'\0', at);
        if (end == std::wstring::npos) return {};
        if (end == at) return paths.empty() ? std::nullopt : std::make_optional(std::move(paths));
        if (paths.size() >= 128 || end - at > 32767) return {};
        paths.push_back(list.substr(at, end - at));
        at = end + 1;
    }
    return {};
}
inline std::optional<Item> ToItem(Payload payload) {
    Item item;
    item.format = payload.format;
    if (payload.format == CF_UNICODETEXT) {
        item.text = PayloadText(payload);
        if (item.text.empty()) return {};
    } else if (payload.format == CF_DIB || payload.format == CF_DIBV5) {
        auto image = DecodeImage(payload);
        if (!image) return {};
        item.kind = Kind::Image;
        item.text = std::to_wstring(image->width) + L" × " + std::to_wstring(image->height);
        item.bytes = std::move(payload.bytes);
    } else if (payload.format == CF_HDROP) {
        auto paths = FileList(payload);
        if (!paths) return {};
        item.kind = Kind::Files;
        for (const auto &path : *paths) { if (!item.text.empty()) item.text += L"\n"; item.text += path; }
        item.bytes = std::move(payload.bytes);
    } else return {};
    return item;
}
inline Payload FromItem(const Item &item, bool plain = false) {
    if (item.kind == Kind::Text || plain) return TextPayload(item.text);
    return {item.format, 0, item.bytes};
}
}
