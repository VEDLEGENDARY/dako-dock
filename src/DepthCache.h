#pragma once
#include "AmbientShadow.h"
#include <d2d1.h>
#include <wrl/client.h>
#include <deque>

namespace graphics {
class DepthCache {
    struct Entry {
        int dpi, radius;
        float tile, cut;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };
    std::deque<Entry> entries_;

  public:
    void Reset() { entries_.clear(); }
    void Paint(ID2D1RenderTarget *target, D2D1_RECT_F rect, float dpi, float radius, anchor::Edge edge) {
        int scaleKey = static_cast<int>(std::lround(dpi)), radiusKey = static_cast<int>(std::lround(radius * 2));
        auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
            return entry.dpi == scaleKey && entry.radius == radiusKey;
        });
        if (found == entries_.end()) {
            int tile = 0, cut = 0;
            auto pixels = AmbientShadow::Slice(dpi, radius, 8, 24, .35f, tile, cut);
            Entry entry{scaleKey, radiusKey, tile * 96 / dpi, cut * 96 / dpi, nullptr};
            if (FAILED(target->CreateBitmap(
                    D2D1::SizeU(tile, tile), pixels.data(), tile * 4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                           dpi, dpi),
                    &entry.bitmap)))
                return;
            if (entries_.size() == 8)
                entries_.pop_front();
            entries_.push_back(std::move(entry));
            found = std::prev(entries_.end());
        }
        float src[]{0, found->cut, found->tile - found->cut, found->tile};
        float x[]{rect.left - 24, rect.left + radius, rect.right - radius, rect.right + 24};
        float offset = anchor::Grow(edge) * 8;
        float y[]{rect.top - 24 + offset, rect.top + radius + offset, rect.bottom - radius + offset,
                  rect.bottom + 24 + offset};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                target->DrawBitmap(found->bitmap.Get(), {x[column], y[row], x[column + 1], y[row + 1]}, 1,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                   {src[column], src[row], src[column + 1], src[row + 1]});
    }
};
} // namespace graphics
