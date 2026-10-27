#pragma once
#include "DockShell.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <map>

namespace dockview {
using Microsoft::WRL::ComPtr;
inline D2D1_RECT_F Rect(layout::Rect rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}
struct Resources {
    ComPtr<IDWriteTextFormat> text, icon;
    ComPtr<IWICImagingFactory> images;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> icons;
    bool Initialize(IDWriteFactory *factory) {
        if (text && icon)
            return true;
        HRESULT result =
            factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, 12, L"", &text);
        if (FAILED(result))
            return false;
        result = factory->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 22, L"", &icon);
        if (FAILED(result))
            return false;
        for (auto *format : {text.Get(), icon.Get()}) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            factory->CreateEllipsisTrimmingSign(format, &ellipsis);
            format->SetTrimming(&trim, ellipsis.Get());
        }
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images));
        return true;
    }
    ID2D1Bitmap *Icon(ID2D1RenderTarget *target, const desktop::DockGroup &group) {
        HICON native = nullptr;
        for (const auto &window : group.windows)
            if (window.icon) {
                native = window.icon->handle;
                break;
            }
        auto key = group.key + L":" + std::to_wstring(reinterpret_cast<UINT_PTR>(native)) + L":" +
                   std::to_wstring(reinterpret_cast<UINT_PTR>(group.pixels.get()));
        auto found = icons.find(key);
        if (found != icons.end())
            return found->second.Get();
        if (icons.size() >= 128)
            icons.clear();
        ComPtr<ID2D1Bitmap> bitmap;
        if (group.pixels && group.width && group.height) {
            target->CreateBitmap(
                D2D1::SizeU(group.width, group.height), group.pixels->data(), group.width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                &bitmap);
        } else if (native && images) {
            ComPtr<IWICBitmap> source;
            if (SUCCEEDED(images->CreateBitmapFromHICON(native, &source)))
                target->CreateBitmapFromWicBitmap(source.Get(), nullptr, &bitmap);
        }
        return icons.emplace(std::move(key), bitmap).first->second.Get();
    }
};
inline HRESULT Paint(ID2D1RenderTarget *target, IDWriteFactory *writer, Resources &resources, float width,
                     const std::vector<desktop::DockGroup> &groups, const desktop::DockStatus &status, int hovered = -1,
                     int focused = -1, float flash = 1, desktop::DockOptions options = {}, size_t first = 0,
                     float pointer = -1000, float strength = 0, bool contrast = false) {
    if (!resources.Initialize(writer))
        return E_FAIL;
    auto geometry = desktop::SolveDock(width, groups, status, options, first, pointer, strength);
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT result = target->CreateSolidColorBrush(D2D1::ColorF(theme::Text), &brush);
    if (FAILED(result))
        return result;
    auto fill = [&](layout::Rect r, unsigned color, float radius, float alpha = 1.0f) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        target->FillRoundedRectangle(D2D1::RoundedRect(Rect(r), radius, radius), brush.Get());
    };
    target->Clear(D2D1::ColorF(0, 0.0f));
    auto pill = geometry.pill;
    if (!contrast) {
        // The soft lower rim stays inside the native capsule input region.
        fill(pill, theme::Ink, 28, .12f);
        fill({pill.left + .8f, pill.top + .8f, pill.right - .8f, pill.bottom - 2}, theme::Surface, 27);
    } else
        fill(pill, theme::Surface, 28);
    brush->SetColor(D2D1::ColorF(contrast ? theme::Text : theme::Ink, contrast ? 1.0f : .25f));
    target->DrawRoundedRectangle(
        D2D1::RoundedRect({pill.left + .6f, pill.top + .6f, pill.right - .6f, pill.bottom - .6f}, 28, 28), brush.Get(),
        contrast ? 2.0f : 1.0f);
    brush->SetColor(D2D1::ColorF(theme::Border));
    for (float divider : geometry.dividers)
        target->DrawLine({divider, 24}, {divider, 48}, brush.Get(), 1);
    for (const auto &hit : geometry.hits) {
        bool isGroup = hit.action >= desktop::DockGroupFirst;
        const desktop::DockGroup *app = isGroup ? &groups[hit.action - desktop::DockGroupFirst] : nullptr;
        if (hit.action == hovered || hit.action == focused || hit.active)
            fill(hit.rect, hit.active ? theme::AccentSoft : theme::CardHover, 12);
        if (app && app->flash) {
            brush->SetColor(D2D1::ColorF(theme::Focus, .5f + .5f * flash));
            target->DrawRoundedRectangle(D2D1::RoundedRect(Rect(hit.rect), 12, 12), brush.Get(), 1.5f);
        }
        auto icon = app ? resources.Icon(target, *app) : nullptr;
        if (icon) {
            auto r = hit.rect;
            target->DrawBitmap(icon, {r.left + 2, r.top + 2, r.right - 2, r.bottom - 2});
        } else if (hit.action == desktop::DockApps) {
            float x = hit.rect.CenterX(), y = (hit.rect.top + hit.rect.bottom) / 2;
            brush->SetColor(D2D1::ColorF(theme::Accent));
            target->FillRoundedRectangle(D2D1::RoundedRect({x - 10, y - 12, x + 8, y + 12}, 9, 9), brush.Get());
            brush->SetColor(D2D1::ColorF(theme::Surface));
            target->FillEllipse(D2D1::Ellipse({x - 1, y}, 4, 5), brush.Get());
            brush->SetColor(D2D1::ColorF(theme::Text));
            target->DrawLine({x + 10, y - 12}, {x + 10, y + 12}, brush.Get(), 2);
        } else {
            brush->SetColor(D2D1::ColorF(hit.active ? theme::SelectedText : theme::Text));
            std::wstring glyph = hit.label;
            if (app) {
                const wchar_t *glyphs[]{L"\xE756", L"\xE774", L"\xE715", L"\xE8B7", L"\xE70F"};
                glyph =
                    app->key.starts_with(L"fixture:") ? glyphs[(hit.action - desktop::DockGroupFirst) % 5] : L"\xE8A5";
            }
            target->DrawTextW(glyph.c_str(), static_cast<UINT32>(glyph.size()),
                              hit.action == desktop::DockClock ? resources.text.Get() : resources.icon.Get(),
                              Rect(hit.rect), brush.Get());
        }
        if (app && !app->windows.empty()) {
            brush->SetColor(D2D1::ColorF(app->active ? theme::Focus : theme::Muted));
            target->FillEllipse(D2D1::Ellipse({hit.rect.CenterX(), 60}, app->active ? 2.3f : 1.7f, 1.7f), brush.Get());
        }
        if (hit.action == focused) {
            brush->SetColor(D2D1::ColorF(theme::Focus));
            target->DrawRoundedRectangle(D2D1::RoundedRect(Rect(hit.rect), 12, 12), brush.Get(), 2);
        }
    }
    return S_OK;
}
inline std::vector<desktop::DockHit> PreviewHits(float width, float height) {
    float footer = height - 38;
    return {{1, 1, {12, footer, 44, height - 6}, L"Previous window", L"‹"},
            {2, 2, {48, footer, 80, height - 6}, L"Next window", L"›"},
            {3, 3, {88, footer, width - 96, height - 6}, L"Activate window", L"Open"},
            {4, 4, {width - 88, footer, width - 12, height - 6}, L"Close window", L"Close"}};
}
inline HRESULT PaintPreview(ID2D1RenderTarget *target, IDWriteFactory *writer, Resources &resources, float width,
                            float height, const std::wstring &title, bool unavailable, int focused,
                            bool fixture = false) {
    if (!resources.Initialize(writer))
        return E_FAIL;
    target->Clear(D2D1::ColorF(theme::Surface));
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT result = target->CreateSolidColorBrush(D2D1::ColorF(theme::Text), &brush);
    if (FAILED(result))
        return result;
    target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), resources.text.Get(), {12, 6, width - 12, 36},
                      brush.Get());
    brush->SetColor(D2D1::ColorF(theme::Inset));
    target->FillRoundedRectangle(D2D1::RoundedRect({12, 42, width - 12, height - 48}, 10, 10), brush.Get());
    if (unavailable || fixture) {
        const std::wstring message = unavailable ? L"Preview unavailable for this window" : L"Inert fixture window";
        brush->SetColor(D2D1::ColorF(theme::Muted));
        target->DrawTextW(message.c_str(), static_cast<UINT32>(message.size()), resources.text.Get(),
                          {16, 42, width - 16, height - 48}, brush.Get());
    }
    for (const auto &hit : PreviewHits(width, height)) {
        brush->SetColor(D2D1::ColorF(hit.action == focused + 1 ? theme::AccentSoft : theme::Inset));
        target->FillRoundedRectangle(D2D1::RoundedRect(Rect(hit.rect), 8, 8), brush.Get());
        brush->SetColor(D2D1::ColorF(hit.action == focused + 1 ? theme::SelectedText : theme::Text));
        target->DrawTextW(hit.label.c_str(), static_cast<UINT32>(hit.label.size()), resources.text.Get(),
                          Rect(hit.rect), brush.Get());
    }
    return S_OK;
}
}
