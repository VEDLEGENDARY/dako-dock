#pragma once
#include "Anchor.h"
#include <d2d1.h>
#include <wrl/client.h>
#include <array>
#include <deque>

namespace graphics {
// Factory resources survive target loss. Positions and extents are quantized to half a DIP.
class GeometryCache {
    using Path = Microsoft::WRL::ComPtr<ID2D1PathGeometry>;
    struct Entry {
        std::array<int, 8> key;
        Path path;
    };
    std::deque<Entry> entries_;
    template <typename Build> ID2D1PathGeometry *Get(ID2D1Factory *factory, std::array<int, 8> key, Build build) {
        auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) { return e.key == key; });
        if (found != entries_.end()) {
            auto entry = std::move(*found);
            entries_.erase(found);
            entries_.push_back(std::move(entry));
            return entries_.back().path.Get();
        }
        Path path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
            return nullptr;
        build(sink.Get());
        if (FAILED(sink->Close()))
            return nullptr;
        if (entries_.size() == 8)
            entries_.pop_front();
        entries_.push_back({key, std::move(path)});
        return entries_.back().path.Get();
    }

  public:
    size_t Size() const { return entries_.size(); }
    ID2D1PathGeometry *Surface(ID2D1Factory *factory, bool notch, float x, float y, float w, float h, float radius,
                              anchor::Edge edge) {
        auto q = [](float n) { return static_cast<int>(std::lround(n * 2)); };
        std::array<int, 8> key{notch ? 1 : 0, q(x), q(y), q(w), q(h), q(radius), static_cast<int>(edge), 0};
        x = key[1] / 2.0f;
        y = key[2] / 2.0f;
        w = key[3] / 2.0f;
        h = key[4] / 2.0f;
        radius = key[5] / 2.0f;
        return Get(factory, key, [=](ID2D1GeometrySink *sink) {
            auto p = [=](float px, float py) {
                return D2D1::Point2F(px, edge == anchor::Edge::Top ? py : y + h - (py - y));
            };
            auto curve = [&](D2D1_POINT_2F b, D2D1_POINT_2F c, D2D1_POINT_2F d) {
                sink->AddBezier(D2D1::BezierSegment(b, c, d));
            };
            if (notch) {
                sink->BeginFigure(p(x, y), D2D1_FIGURE_BEGIN_FILLED);
                curve(p(x + w * .11f, y), p(x + w * .11f, y + h), p(x + w * .22f, y + h));
                sink->AddLine(p(x + w * .78f, y + h));
                curve(p(x + w * .89f, y + h), p(x + w * .89f, y), p(x + w, y));
            } else {
                float r = std::min(radius, h / 2), shoulder = 24;
                sink->BeginFigure(p(x - shoulder, y), D2D1_FIGURE_BEGIN_FILLED);
                curve(p(x - 3, y), p(x, y + 3), p(x, y + r));
                sink->AddLine(p(x, y + h - r));
                curve(p(x, y + h), p(x, y + h), p(x + r, y + h));
                sink->AddLine(p(x + w - r, y + h));
                curve(p(x + w, y + h), p(x + w, y + h), p(x + w, y + h - r));
                sink->AddLine(p(x + w, y + r));
                curve(p(x + w, y + 3), p(x + w + 3, y), p(x + w + shoulder, y));
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        });
    }
    ID2D1PathGeometry *Arc(ID2D1Factory *factory, float radius, float fraction) {
        int step = std::clamp(static_cast<int>(std::lround(fraction * 200)), 1, 199);
        std::array<int, 8> key{2, static_cast<int>(std::lround(radius * 2)), step, 0, 0, 0, 0, 0};
        return Get(factory, key, [=](ID2D1GeometrySink *sink) {
            float r = key[1] / 2.0f, angle = step / 200.0f * 6.28318530718f;
            sink->BeginFigure({0, -r}, D2D1_FIGURE_BEGIN_HOLLOW);
            auto arc = [&](float a) {
                sink->AddArc(D2D1::ArcSegment({r * std::sin(a), -r * std::cos(a)}, {r, r}, 0,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            };
            if (angle > 3.14159265359f)
                arc(3.14159265359f);
            arc(angle);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        });
    }
};
} // namespace graphics
