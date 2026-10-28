#include <windows.h>
#include <d2d1.h>
#include "../src/GeometryCache.h"
#include "../src/AmbientShadow.h"
#include <cstdio>
#include <cstdlib>

void RequireDesign(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
int main() {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    RequireDesign(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
                  "D2D factory");
    graphics::GeometryCache cache;
    auto *surface = cache.Surface(factory.Get(), false, 0, 0, 800, 400, 24, anchor::Edge::Bottom);
    RequireDesign(surface != nullptr, "Bezier surface builds");
    D2D1_RECT_F bounds{};
    surface->GetBounds(nullptr, &bounds);
    RequireDesign(std::abs(bounds.left + 24) < .01f && std::abs(bounds.bottom - 400) < .01f,
                  "Bezier bounds include attached shoulders");
    for (int i = 0; i < 1000; ++i)
        RequireDesign(cache.Surface(factory.Get(), false, 0, 0, 800.1f, 400, 24, anchor::Edge::Bottom) == surface,
                      "settled and sub-quantum frames reuse their geometry");
    auto *arc = cache.Arc(factory.Get(), 32, .25f);
    float length = 0;
    RequireDesign(arc && SUCCEEDED(arc->ComputeLength(nullptr, &length)) && std::abs(length - 50.265f) < .1f,
                  "ring is a quarter-circle arc");
    arc = cache.Arc(factory.Get(), 32, .75f);
    RequireDesign(arc && SUCCEEDED(arc->ComputeLength(nullptr, .01f, &length)) && std::abs(length - 150.796f) < .2f,
                  "large ring uses two continuous arcs");
    for (int i = 0; i < 20; ++i)
        cache.Surface(factory.Get(), false, 0, 0, 200.0f + i, 200, 24, anchor::Edge::Top);
    RequireDesign(cache.Size() == 8, "geometry LRU is bounded to eight resources");
    RequireDesign(dock::Attached<24>(0, 0, 800, 400).size() == 101, "high DPI region samples 24 segments per curve");
    std::puts("PASS: Bezier bounds, half-DIP reuse, bounded geometry LRU, continuous ring arcs and DPI sampling.");
}
