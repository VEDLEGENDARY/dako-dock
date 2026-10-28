#pragma once
#include "DockShell.h"
#include "UiTypography.h"
#include "GeometryCache.h"
#include "MotionController.h"
#include "DepthCache.h"
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <map>
#include <deque>

namespace dockview {
using Microsoft::WRL::ComPtr;
inline D2D1_RECT_F Rect(layout::Rect rect) { return {rect.left, rect.top, rect.right, rect.bottom}; }
struct Resources {
    std::shared_ptr<type::Library> typography;
    graphics::GeometryCache geometryCache;
    graphics::DepthCache depth;
    ComPtr<IDWriteTextFormat> text, icon;
    ComPtr<IWICImagingFactory> images;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> icons;
    std::deque<std::wstring> iconLru;
    struct Visual {
        dock::Spring appearance{0}, running{0}, x{0}, hover{0}, drag{0};
        dock::Feedback bounce;
        float bounceClock = 1;
        bool secondHop = false;
        bool positioned = false;
    };
    std::map<std::wstring, Visual> visuals;
    std::map<int, dock::Spring> actionHover;
    dock::Spring focusOpacity{1};
    int previousFocus = -1;
    std::wstring pulseKey, clockText;
    ComPtr<IDWriteTextLayout> clockLayout;
    float clockWidth = 0, clockHeight = 0;
    bool Initialize(IDWriteFactory *factory) {
        if (text && icon)
            return true;
        typography = type::Acquire(factory);
        text = typography->formats[type::Label];
        icon = typography->formats[type::Icon];
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images));
        return true;
    }
    void ResetBitmaps() {
        icons.clear();
        iconLru.clear();
        depth.Reset();
    }
    void Synchronize(const desktop::DockLayout &layout, const std::vector<desktop::DockGroup> &groups, int hovered,
                     int dragged, bool animated) {
        for (const auto &hit : layout.hits) {
            if (hit.action < desktop::DockGroupFirst) {
                auto &spring = actionHover[hit.action];
                spring.goal = hit.action == hovered ? 1.0f : 0.0f;
                if (!animated)
                    spring.Snap(spring.goal);
                continue;
            }
            const auto &group = groups[hit.action - desktop::DockGroupFirst];
            auto [it, inserted] = visuals.try_emplace(group.key);
            auto &v = it->second;
            if (inserted || !v.positioned) {
                v.x.Snap(hit.rect.left);
                v.positioned = true;
            }
            v.appearance.goal = 1;
            v.running.goal = group.windows.empty() ? 0.0f : 1.0f;
            v.x.goal = hit.rect.left;
            v.hover.goal = hit.action == hovered ? 1.0f : 0.0f;
            v.drag.goal = hit.action - desktop::DockGroupFirst == dragged ? 1.0f : 0.0f;
            if (!animated) {
                for (auto *spring : {&v.appearance, &v.running, &v.x, &v.hover, &v.drag})
                    spring->Snap(spring->goal);
                v.bounce.Snap(0);
                v.bounceClock = 1;
            }
        }
        for (auto it = visuals.begin(); it != visuals.end();)
            if (std::none_of(groups.begin(), groups.end(), [&](const auto &group) { return group.key == it->first; }))
                it = visuals.erase(it);
            else
                ++it;
    }
    bool Moving() const {
        if (focusOpacity.Moving())
            return true;
        for (const auto &[id, spring] : actionHover) {
            (void)id;
            if (spring.Moving())
                return true;
        }
        for (const auto &[key, v] : visuals) {
            (void)key;
            if (v.appearance.Moving() || v.running.Moving() || v.x.Moving() || v.hover.Moving() || v.drag.Moving() ||
                v.bounce.Moving() || v.bounceClock < .55f)
                return true;
        }
        return false;
    }
    void Step(float seconds, float speed = 1) {
        focusOpacity.Step(seconds, dock::motion::Focus, speed);
        for (auto &[id, spring] : actionHover) {
            (void)id;
            spring.Step(seconds, dock::motion::Snappy, speed);
        }
        for (auto &[key, v] : visuals) {
            (void)key;
            v.appearance.Step(seconds, dock::motion::Settle, speed);
            v.running.Step(seconds, dock::motion::Snappy, speed);
            v.x.Step(seconds, dock::motion::Settle, speed);
            v.hover.Step(seconds, dock::motion::Snappy, speed);
            v.drag.Step(seconds, dock::motion::Lively, speed);
            if (v.bounceClock < .55f) {
                v.bounceClock += seconds * speed;
                if (!v.secondHop && v.bounceClock >= .18f) {
                    v.secondHop = true;
                    v.bounce.Snap(0);
                    v.bounce.velocity = 160;
                }
                if (v.bounceClock >= .55f)
                    v.bounce.Snap(0);
                else
                    v.bounce.Step(seconds, speed);
            }
        }
    }
    void Launch(const std::wstring &key) {
        auto &v = visuals[key];
        if (v.bounceClock < .55f)
            return;
        v.bounceClock = 0;
        v.secondHop = false;
        v.bounce.Snap(0);
        v.bounce.velocity = 240;
    }
    void DrawClock(ID2D1RenderTarget *target, IDWriteFactory *writer, ID2D1Brush *brush, const std::wstring &value,
                   D2D1_RECT_F rect) {
        float width = rect.right - rect.left, height = rect.bottom - rect.top;
        if (!clockLayout || clockText != value || clockWidth != width || clockHeight != height) {
            clockText = value;
            clockWidth = width;
            clockHeight = height;
            clockLayout.Reset();
            if (FAILED(writer->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), text.Get(), width,
                                                height, &clockLayout)))
                return;
            clockLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            if (typography->numbers)
                clockLayout->SetTypography(typography->numbers.Get(), {0, static_cast<UINT32>(value.size())});
        }
        target->DrawTextLayout({rect.left, rect.top}, clockLayout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    ID2D1Bitmap *Icon(ID2D1RenderTarget *target, const desktop::DockGroup &group, bool large, float scale) {
        HICON native = nullptr;
        for (const auto &window : group.windows)
            if (window.icon) {
                native = window.icon->handle;
                break;
            }
        UINT extent = static_cast<UINT>(std::clamp(std::ceil((large ? 56.0f : 40.0f) * scale * 1.5f), 32.0f, 512.0f));
        auto key = group.key + L":" + std::to_wstring(reinterpret_cast<UINT_PTR>(native)) + L":" +
                   std::to_wstring(reinterpret_cast<UINT_PTR>(group.pixels.get())) + L":" + std::to_wstring(extent);
        auto found = icons.find(key);
        if (found != icons.end()) {
            std::erase(iconLru, key);
            iconLru.push_back(key);
            return found->second.Get();
        }
        ComPtr<IWICBitmap> source;
        if (images && group.pixels && group.width && group.height) {
            images->CreateBitmapFromMemory(group.width, group.height, GUID_WICPixelFormat32bppPBGRA, group.width * 4,
                                           static_cast<UINT>(group.pixels->size()), group.pixels->data(), &source);
        } else if (images && native)
            images->CreateBitmapFromHICON(native, &source);
        ComPtr<ID2D1Bitmap> bitmap;
        if (source) {
            UINT width = 0, height = 0;
            source->GetSize(&width, &height);
            float ratio = std::min({1.0f, extent / static_cast<float>(std::max(1U, width)),
                                    extent / static_cast<float>(std::max(1U, height))});
            ComPtr<IWICBitmapScaler> scaled;
            if (SUCCEEDED(images->CreateBitmapScaler(&scaled)) &&
                SUCCEEDED(scaled->Initialize(source.Get(), std::max(1U, static_cast<UINT>(width * ratio)),
                                             std::max(1U, static_cast<UINT>(height * ratio)),
                                             WICBitmapInterpolationModeFant)))
                target->CreateBitmapFromWicBitmap(scaled.Get(), nullptr, &bitmap);
        }
        while (icons.size() >= 128 && !iconLru.empty()) {
            icons.erase(iconLru.front());
            iconLru.pop_front();
        }
        iconLru.push_back(key);
        return icons.emplace(std::move(key), bitmap).first->second.Get();
    }
};
inline HRESULT Paint(ID2D1RenderTarget *target, IDWriteFactory *writer, Resources &resources, float width,
                     const std::vector<desktop::DockGroup> &groups, const desktop::DockStatus &status, int hovered = -1,
                     int focused = -1, float flash = 1, desktop::DockOptions options = {}, size_t first = 0,
                     float pointer = -1000, float strength = 0, bool contrast = false, bool animated = false,
                     int dragged = -1) {
    if (!resources.Initialize(writer))
        return E_FAIL;
    auto geometry = desktop::SolveDock(width, groups, status, options, first, pointer, strength);
    resources.Synchronize(geometry, groups, hovered, dragged, animated);
    if (resources.previousFocus != focused) {
        resources.previousFocus = focused;
        resources.focusOpacity.Snap(animated ? 0.0f : 1.0f);
        resources.focusOpacity.goal = 1;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT result = target->CreateSolidColorBrush(D2D1::ColorF(theme::Text), &brush);
    if (FAILED(result))
        return result;
    ComPtr<ID2D1DeviceContext> context;
    target->QueryInterface(IID_PPV_ARGS(&context));
    D2D1::Matrix3x2F transform;
    target->GetTransform(&transform);
    float dpiX = 96, dpiY = 96;
    target->GetDpi(&dpiX, &dpiY);
    float scale = dpiX / 96 * std::max(.01f, std::abs(transform._11)), pixel = 1 / scale;
    auto fill = [&](layout::Rect r, unsigned color, float radius, float alpha = 1.0f) {
        brush->SetColor(D2D1::ColorF(color, alpha));
        target->FillRoundedRectangle(D2D1::RoundedRect(Rect(r), radius, radius), brush.Get());
    };
    target->Clear(D2D1::ColorF(0, 0.0f));
    bool translucent = options.island && !contrast && options.opacity < .999f;
    if (translucent) {
        auto layer = D2D1::LayerParameters();
        layer.opacity = options.opacity;
        target->PushLayer(layer, nullptr);
    }
    auto pill = geometry.pill;
    if (options.island) {
        ComPtr<ID2D1Factory> factory;
        target->GetFactory(&factory);
        auto *path = resources.geometryCache.Surface(factory.Get(), false, 0, 0, pill.Width(), pill.Height(),
                                                     options.radius, options.edge);
        if (path) {
            target->SetTransform(D2D1::Matrix3x2F::Translation(pill.left, pill.top) * transform);
            brush->SetColor(D2D1::ColorF(theme::Surface));
            target->FillGeometry(path, brush.Get());
            target->SetTransform(D2D1::Matrix3x2F::Scale((pill.Width() - pixel) / pill.Width(),
                                                         (pill.Height() - pixel) / pill.Height()) *
                                 D2D1::Matrix3x2F::Translation(pill.left + pixel / 2, pill.top + pixel / 2) *
                                 transform);
            brush->SetColor(
                D2D1::ColorF(contrast ? theme::Border : theme::Edge, contrast ? 1.0f : palette::Tokens::HairlineOuter));
            target->DrawGeometry(path, brush.Get(), contrast ? 2.0f : pixel);
            target->SetTransform(transform);
        }
    } else {
        fill(pill, theme::Surface, pill.Height() / 2);
        brush->SetColor(
            D2D1::ColorF(contrast ? theme::Border : theme::Edge, contrast ? 1.0f : palette::Tokens::HairlineOuter));
        target->DrawRoundedRectangle(D2D1::RoundedRect(Rect(pill), pill.Height() / 2, pill.Height() / 2), brush.Get(),
                                     contrast ? 2.0f : pixel);
    }
    if (!contrast) {
        float y = options.edge == anchor::Edge::Bottom ? pill.top + pixel * 1.5f : pill.bottom - pixel * 1.5f;
        brush->SetColor(D2D1::ColorF(theme::Edge, palette::Tokens::HairlineInner));
        target->DrawLine({pill.left + options.radius, y}, {pill.right - options.radius, y}, brush.Get(), pixel);
    }
    brush->SetColor(D2D1::ColorF(contrast ? theme::Border : theme::Edge, contrast ? 1.0f : palette::Tokens::Divider));
    for (float divider : geometry.dividers) {
        float x = (std::floor(divider / pixel) + .5f) * pixel;
        target->DrawLine({x, geometry.dividerTop}, {x, geometry.dividerBottom}, brush.Get(), pixel);
    }
    for (const auto &hit : geometry.hits) {
        bool isGroup = hit.action >= desktop::DockGroupFirst;
        const desktop::DockGroup *app = isGroup ? &groups[hit.action - desktop::DockGroupFirst] : nullptr;
        auto r = hit.rect;
        float opacity = 1, hover = 0, running = 1;
        if (app) {
            auto &v = resources.visuals[app->key];
            hover = v.hover.value;
            running = std::clamp(v.running.value, 0.0f, 1.0f);
            opacity = std::clamp(v.appearance.value, 0.0f, 1.0f);
            float size = std::min(56.0f, r.Height() * (.6f + .4f * opacity) * (1 + .15f * v.drag.value));
            float cx = v.x.value + r.Width() / 2;
            float bounce = std::clamp(v.bounce.value, 0.0f, 6.0f) * anchor::Grow(options.edge);
            float top = options.edge == anchor::Edge::Bottom ? geometry.baseline - size : geometry.baseline;
            r = {cx - size / 2, top + bounce, cx + size / 2, top + size + bounce};
            auto layer = D2D1::LayerParameters();
            layer.opacity = opacity * (resources.pulseKey == app->key ? flash : 1);
            target->PushLayer(layer, nullptr);
            if (!contrast && v.drag.value > .01f)
                resources.depth.Paint(target, Rect(r), dpiX * std::abs(transform._11), layout::ControlRadius,
                                      options.edge);
        } else
            hover = resources.actionHover[hit.action].value;
        if (hover > 0 || hit.active || hit.action == focused)
            fill(r, hit.active ? theme::AccentSoft : theme::CardHover, layout::ControlRadius,
                 contrast || hit.active ? 1.0f : hover * .6f);
        auto bitmap = app ? resources.Icon(target, *app, r.Height() > 42, scale) : nullptr;
        if (bitmap) {
            float size = std::min(r.Height(), options.iconSize * r.Height() / 40);
            float cx = r.CenterX(), top = options.edge == anchor::Edge::Bottom ? r.bottom - size : r.top;
            D2D1_RECT_F destination{cx - size / 2, top, cx + size / 2, top + size};
            if (context)
                context->DrawBitmap(bitmap, &destination, 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr,
                                    nullptr);
            else
                target->DrawBitmap(bitmap, destination);
        } else if (hit.action == desktop::DockApps) {
            float x = r.CenterX() - 8.5f, y = (r.top + r.bottom) / 2 - 8.5f;
            brush->SetColor(D2D1::ColorF(theme::Text));
            for (int row = 0; row < 2; ++row)
                for (int column = 0; column < 2; ++column) {
                    D2D1_RECT_F square{x + column * 10, y + row * 10, x + column * 10 + 7, y + row * 10 + 7};
                    target->FillRoundedRectangle(D2D1::RoundedRect(square, 7.0f / 4, 7.0f / 4), brush.Get());
                }
        } else {
            brush->SetColor(D2D1::ColorF(app ? theme::Text : dock::Mix(theme::Muted, theme::Text, hover)));
            std::wstring glyph = hit.label;
            if (app) {
                const wchar_t *glyphs[]{L"\xE756", L"\xE774", L"\xE715", L"\xE8B7", L"\xE70F"};
                glyph =
                    app->key.starts_with(L"fixture:") ? glyphs[(hit.action - desktop::DockGroupFirst) % 5] : L"\xE8A5";
            }
            if (hit.action == desktop::DockClock)
                resources.DrawClock(target, writer, brush.Get(), glyph, Rect(r));
            else
                target->DrawTextW(glyph.c_str(), static_cast<UINT32>(glyph.size()),
                                  app ? resources.typography->formats[type::LargeIcon].Get() : resources.icon.Get(),
                                  Rect(r), brush.Get());
        }
        if (app && options.runningDots && running > 0) {
            brush->SetColor(D2D1::ColorF(contrast ? theme::Text : app->active ? theme::Accent : theme::Muted));
            target->FillEllipse(
                D2D1::Ellipse({r.CenterX(), geometry.dotY}, (app->active ? 2.5f : 1.5f) * running, 1.5f * running),
                brush.Get());
        }
        if (hit.action == focused) {
            brush->SetColor(D2D1::ColorF(theme::Focus, contrast ? 1 : resources.focusOpacity.value));
            target->DrawRoundedRectangle(D2D1::RoundedRect(Rect(r), layout::ControlRadius, layout::ControlRadius),
                                         brush.Get(), contrast ? 2.0f : pixel * 1.5f);
        }
        if (app)
            target->PopLayer();
    }
    if (translucent)
        target->PopLayer();
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
} // namespace dockview
