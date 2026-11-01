#include <windows.h>
#include <d2d1.h>
#include "../src/GeometryCache.h"
#include "../src/AmbientShadow.h"
#include "../src/LayoutEngine.h"
#include "../src/WallpaperModel.h"
#include "../src/DockView.h"
#include <cstdio>
#include <cstdlib>

void RequireDesign(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void VerifyDockResources(ID2D1Factory *factory) {
    Microsoft::WRL::ComPtr<IWICImagingFactory> images;
    RequireDesign(
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images))),
        "WIC icon factory");
    Microsoft::WRL::ComPtr<IWICBitmap> surface;
    images->CreateBitmap(128, 128, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &surface);
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
    RequireDesign(
        SUCCEEDED(factory->CreateWicBitmapRenderTarget(
            surface.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &target)),
        "icon cache target");
    dockview::Resources resources;
    resources.images = images;
    desktop::DockGroup group;
    group.key = L"fixture:first";
    group.width = group.height = 32;
    group.pixels = std::make_shared<std::vector<BYTE>>(32 * 32 * 4, static_cast<BYTE>(255));
    auto *base = resources.Icon(target.Get(), group, false, 1);
    auto *large = resources.Icon(target.Get(), group, true, 1);
    RequireDesign(base && large && base != large && resources.icons.size() == 2,
                  "base and magnified icons have separate cached surfaces");
    std::wstring largeKey;
    for (const auto &[key, bitmap] : resources.icons)
        if (bitmap.Get() == large)
            largeKey = key;
    for (int i = 0; i < 125; ++i) {
        group.key = L"fixture:" + std::to_wstring(i);
        resources.Icon(target.Get(), group, false, 1);
    }
    group.key = L"fixture:first";
    RequireDesign(resources.Icon(target.Get(), group, false, 1) == base, "hot icon is reused");
    for (int i = 125; i < 131; ++i) {
        group.key = L"fixture:" + std::to_wstring(i);
        resources.Icon(target.Get(), group, false, 1);
    }
    group.key = L"fixture:first";
    RequireDesign(resources.icons.size() == 128 && !resources.icons.contains(largeKey) &&
                      resources.Icon(target.Get(), group, false, 1) == base,
                  "LRU evicts cold icons individually and keeps the hot surface");
}
int main() {
    RequireDesign(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM initialization");
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    RequireDesign(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
                  "D2D factory");
    graphics::GeometryCache cache;
    auto *surface = cache.Surface(factory.Get(), false, 0, 0, 800, 400, 24, anchor::Edge::Bottom);
    RequireDesign(surface != nullptr, "Bezier surface builds");
    D2D1_RECT_F bounds{};
    surface->GetBounds(nullptr, &bounds);
    RequireDesign(std::abs(bounds.left + shape::Fillet) < .01f && std::abs(bounds.bottom - 400) < .01f,
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
    for (float width : {100.0f, 180.0f, 240.0f, 400.0f, 640.0f, 760.0f, 800.0f})
        for (float height : {80.0f, 160.0f, 240.0f, 400.0f})
            for (auto view : {layout::View::Controls, layout::View::Today, layout::View::Stats, layout::View::Power}) {
                auto layout = layout::Solve({width, height, view});
                RequireDesign(layout::ValidCards(layout, view), "sibling cards are disjoint within the scroll canvas");
                RequireDesign(layout.status.Height() > 0, "status toast has positive height at every viewport");
            }
    auto layout = layout::Solve({800, 760, layout::View::Controls});
    RequireDesign(layout.panel.Width() == surface::Standard && layout.panel.Height() == 368 &&
                      layout.body.Height() == 288 && layout.footer.Height() == 0 &&
                      layout.header.top > layout.body.bottom,
                  "bottom navigation shares one row and leaves the reference content canvas unobstructed");
    for (auto edge : {anchor::Edge::Top, anchor::Edge::Bottom}) {
        desktop::DockOptions options;
        options.edge = edge;
        auto dock = desktop::SolveDock(1200, {}, {}, options);
        RequireDesign(edge == anchor::Edge::Bottom ? dock.stripHeight - dock.pill.bottom == 8 : dock.pill.top == 8,
                      "floating dock keeps its eight-DIP gap at either edge");
        layout::Input input;
        input.bottom = edge == anchor::Edge::Bottom;
        auto panel = layout::Solve(input);
        RequireDesign(input.bottom ? panel.header.top > panel.body.bottom : panel.header.bottom < panel.body.top,
                      "navigation stays nearest its attached dock edge");
    }
    for (auto spec : {dock::motion::Settle, dock::motion::Snappy, dock::motion::Soft, dock::motion::Lively,
                      dock::motion::Press, dock::motion::TabTrail}) {
        dock::Spring spring;
        spring.goal = 800;
        float elapsed = 0;
        while (spring.Moving() && elapsed < .6f) {
            spring.Step(1.0f / 240, spec);
            elapsed += 1.0f / 240;
        }
        RequireDesign(!spring.Moving() && elapsed <= spec.duration + .005f,
                      "named spring settles within its interaction duration and releases motion by 600 ms");
    }
    dock::Spring reveal, panelWidth(216), panelHeight(18);
    reveal.goal = 1;
    panelWidth.goal = 800;
    panelHeight.goal = 400;
    for (int frame = 0; frame < 76; ++frame) {
        reveal.Step(1.0f / 240, dock::motion::Settle);
        panelHeight.Step(1.0f / 240, dock::motion::Height);
        if (frame >= 10)
            panelWidth.Step(1.0f / 240, dock::motion::Width);
    }
    RequireDesign(!reveal.Moving() && !panelWidth.Moving() && !panelHeight.Moving(),
                  "height-led reveal including the width delay settles before 320 ms");
    auto center = wallpaper::Pose(0, 340), left = wallpaper::Pose(-.5f, 340), right = wallpaper::Pose(.5f, 340);
    RequireDesign(center.scale == 1 && center.opacity == 1 && left.x == -right.x && left.skew == right.skew &&
                      left.scale == right.scale && left.scale < 1 && left.opacity < 1,
                  "fractional carousel poses are continuous, share a skew and attenuate symmetrically");
    auto strip = wallpaper::Pose(1, 300), outer = wallpaper::Pose(2, 300);
    RequireDesign(std::abs(strip.widthScale * 300 - 64) < .01f && std::abs(strip.x - (150 + 32 + 12)) < .01f &&
                      std::abs(outer.x - strip.x - 76) < .01f,
                  "settled carousel neighbors are separate 64-DIP strips with 12-DIP gutters");
    std::vector<desktop::DockGroup> groups(4);
    for (size_t i = 0; i < groups.size(); ++i)
        groups[i].key = L"fixture:" + std::to_wstring(i);
    auto normal = desktop::SolveDock(900, groups, {});
    auto first = std::find_if(normal.hits.begin(), normal.hits.end(),
                              [](const auto &hit) { return hit.action == desktop::DockGroupFirst; });
    RequireDesign(first != normal.hits.end(), "dock group is visible");
    auto magnified = desktop::SolveDock(900, groups, {}, {}, 0, first->rect.CenterX(), 1);
    auto peak = std::find_if(magnified.hits.begin(), magnified.hits.end(),
                             [](const auto &hit) { return hit.action == desktop::DockGroupFirst; });
    RequireDesign(peak != magnified.hits.end() && std::abs(peak->rect.Height() - 56) < .01f &&
                      peak->rect.bottom == magnified.baseline && magnified.dotY - peak->rect.bottom == 4,
                  "56 DIP magnification keeps the baseline and running dot outside the icon");
    desktop::DockOptions topOptions;
    topOptions.edge = anchor::Edge::Top;
    auto topDock = desktop::SolveDock(900, groups, {}, topOptions, 0, first->rect.CenterX(), 1);
    auto topPeak = std::find_if(topDock.hits.begin(), topDock.hits.end(),
                                [](const auto &hit) { return hit.action == desktop::DockGroupFirst; });
    RequireDesign(topPeak != topDock.hits.end() && topPeak->rect.top == topDock.baseline &&
                      topPeak->rect.top - topDock.dotY == 4 &&
                      topPeak->rect.top + peak->rect.bottom == topDock.stripHeight,
                  "top-edge icons and fixed running dots mirror bottom-edge geometry");
    dockview::Resources motion;
    motion.Synchronize(normal, groups, -1, -1, true);
    for (int frame = 0; frame < 72; ++frame)
        motion.Step(1.0f / 120);
    RequireDesign(!motion.Moving(), "new dock icons settle and release motion");
    motion.Launch(groups[0].key);
    for (int frame = 0; frame < 72; ++frame)
        motion.Step(1.0f / 120);
    RequireDesign(!motion.Moving(), "two-hop dock launch feedback stops within 600 ms");
    VerifyDockResources(factory.Get());
    HWND owner = CreateWindowExW(0, L"STATIC", L"Inert shadow resource check", WS_POPUP, -10000, -10000, 400, 300,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    RequireDesign(owner != nullptr, "shadow resource fixture owner");
    graphics::AmbientShadow shadow;
    shadow.Position(owner, {-10000, -10000, -9600, -9700}, 96, 24, anchor::Edge::Bottom, 1, true);
    RequireDesign(shadow.Visible(), "the opaque panel's cached shadow is visible");
    shadow.Hide();
    DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    DWORD windows = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (int width = 300; width <= 400; width += 10)
            shadow.Position(owner, {-10000, -10000, -10000 + width, -9700}, 96, 24, anchor::Edge::Bottom, 1, true,
                            {400, 300});
        shadow.Hide();
        RequireDesign(!shadow.Visible() && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi &&
                          GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == windows,
                      "resized shadows release all full-size resources at collapse");
    }
    DestroyWindow(owner);
    std::puts("PASS: Bezier bounds, half-DIP reuse, bounded geometry LRU, continuous ring arcs and DPI sampling.");
    CoUninitialize();
}
