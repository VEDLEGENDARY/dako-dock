#include "../src/GeometryCache.h"
#include <cstdio>
#include <cstdlib>

void RequireShape(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
float Cross(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
void Simple(std::vector<D2D1_POINT_2F> vertices) {
    if (vertices.size() > 1 &&
        std::hypot(vertices.front().x - vertices.back().x, vertices.front().y - vertices.back().y) < .001f)
        vertices.pop_back();
    RequireShape(vertices.size() >= 8, "a flattened surface has a closed outline");
    for (size_t i = 0; i < vertices.size(); ++i)
        for (size_t j = i + 2; j < vertices.size(); ++j) {
            if (i == 0 && j == vertices.size() - 1)
                continue;
            auto a = vertices[i], b = vertices[(i + 1) % vertices.size()];
            auto c = vertices[j], d = vertices[(j + 1) % vertices.size()];
            RequireShape(!(Cross(a, b, c) * Cross(a, b, d) < -.001f && Cross(c, d, a) * Cross(c, d, b) < -.001f),
                         "the union polygon has no crossing segments");
        }
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    RequireShape(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
                 "D2D factory");
    graphics::GeometryCache cache;
    for (float radius : {shape::Surface, shape::Card, shape::Control, shape::Chip}) {
        auto *path = cache.Rounded(factory.Get(), 200, 100, radius);
        RequireShape(path && shape::Contains(path, {100, 50}) && !shape::Contains(path, {0, 0}),
                     "continuous rounded depths retain their center and exclude corner pixels");
        RequireShape(cache.Rounded(factory.Get(), 200.1f, 100, radius) == path,
                     "size/radius/smoothing keys reuse sub-quantum rounded geometry");
        Simple(shape::Polygon(path, D2D1::Matrix3x2F::Identity()));
    }
    auto pill = cache.Rounded(factory.Get(), 400, 56, shape::Pill(56), shape::Flat());
    RequireShape(pill && shape::Contains(pill, {28, 1}) && !shape::Contains(pill, {1, 1}),
                 "pills retain circular caps");
    unsigned cases = 0;
    for (float pillWidth : {220.0f, 480.0f, 800.0f, 1100.0f})
        for (float width : {360.0f, 480.0f, 800.0f})
            for (float anchor : {100.0f, 600.0f, 1100.0f}) {
                shape::Rect p{600 - pillWidth / 2, 400, 600 + pillWidth / 2, 456};
                shape::Rect e{anchor - width / 2, 120, anchor + width / 2, 400};
                auto joined = shape::JoinedExtension(p, e, shape::Edge::Bottom);
                auto bottom = shape::Union(factory.Get(), p, e, shape::Edge::Bottom);
                auto top = shape::Union(factory.Get(), p, e, shape::Edge::Top);
                RequireShape(bottom && top && shape::Contains(bottom.Get(), {600, 428}) &&
                                 shape::Contains(bottom.Get(), {(joined.left + joined.right) / 2, 260}),
                             "all width classes and anchors contain the pill and extension centers");
                RequireShape(!shape::Contains(bottom.Get(), {std::min(p.left, joined.left) - 1, 400}),
                             "union excludes points beyond the bounding union");
                auto commands = shape::Union(p, e);
                for (unsigned i = 0; i < commands.count; ++i)
                    if (commands.segments[i].command == shape::Command::Arc)
                        RequireShape(std::abs(commands.segments[i].radius - shape::Fillet) < .001f,
                                     "concave joins use the shared fillet token");
                for (float y : {130.0f, 220.0f, 390.0f, 405.0f, 428.0f, 450.0f})
                    for (float x = 0; x <= 1200; x += 31)
                        RequireShape(shape::Contains(bottom.Get(), {x, y}) == shape::Contains(top.Get(), {x, 856 - y}),
                                     "top surfaces are exact reflections of bottom surfaces");
                for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
                    auto transform = D2D1::Matrix3x2F::Scale(scale, scale);
                    Simple(shape::Polygon(bottom.Get(), transform));
                    HRGN region = shape::Region(bottom.Get(), transform);
                    RequireShape(region &&
                                     PtInRegion(region, static_cast<int>(600 * scale), static_cast<int>(428 * scale)),
                                 "window regions flatten the same filled geometry at device-pixel tolerance");
                    DeleteObject(region);
                }
                RequireShape(cache.Union(factory.Get(), p, e, shape::Edge::Bottom) != nullptr && cache.Size() <= 8,
                             "union geometry uses the bounded shared LRU");
                ++cases;
            }
    std::printf("PASS: continuous depths, circular pills, %u union/anchor cases, four DPI tiers and reflection.\n",
                cases);
    CoUninitialize();
}
