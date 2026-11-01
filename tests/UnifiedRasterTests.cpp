#include "../src/GeometryCache.h"
#include "../src/SurfaceModel.h"
#include <wincodec.h>
#include <filesystem>
#include <cstdio>
#include <cstdlib>

using Microsoft::WRL::ComPtr;
void RequireRaster(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void SaveRaster(IWICImagingFactory *images, IWICBitmapSource *bitmap, const std::filesystem::path &path, UINT width,
                UINT height) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    RequireRaster(SUCCEEDED(images->CreateStream(&stream)) &&
                      SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
                      SUCCEEDED(images->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                      SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                      SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
                      SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
                      SUCCEEDED(frame->WriteSource(bitmap, nullptr)) && SUCCEEDED(frame->Commit()) &&
                      SUCCEEDED(encoder->Commit()),
                  "write reviewable raster fixture");
}
int wmain(int argc, wchar_t **argv) {
    RequireRaster(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM apartment");
    ComPtr<ID2D1Factory> factory;
    ComPtr<IWICImagingFactory> images;
    RequireRaster(
        SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&images))),
        "software rendering factories");
    std::filesystem::path output = argc > 1 ? argv[1] : L"build/tests/unified-raster";
    std::filesystem::create_directories(output);
    graphics::GeometryCache cache;
    unsigned count = 0;
    for (UINT dpi : {96U, 120U, 144U, 192U})
        for (int edge = 0; edge < 2; ++edge)
            for (int appearance = 0; appearance < 3; ++appearance)
                for (float width : {surface::Compact, surface::Standard, surface::Wide})
                    for (int anchor = 0; anchor < 3; ++anchor) {
                        float scale = dpi / 96.0f;
                        UINT pixelWidth = static_cast<UINT>(1200 * scale);
                        UINT pixelHeight = static_cast<UINT>(640 * scale);
                        ComPtr<IWICBitmap> bitmap;
                        ComPtr<ID2D1RenderTarget> target;
                        RequireRaster(
                            SUCCEEDED(images->CreateBitmap(pixelWidth, pixelHeight, GUID_WICPixelFormat32bppPBGRA,
                                                           WICBitmapCacheOnLoad, &bitmap)) &&
                                SUCCEEDED(factory->CreateWicBitmapRenderTarget(
                                    bitmap.Get(),
                                    D2D1::RenderTargetProperties(
                                        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                        static_cast<float>(dpi), static_cast<float>(dpi)),
                                    &target)),
                            "software union target");
                        shape::Rect pill{280, 500, 920, 556};
                        if (edge)
                            pill = {280, 84, 920, 140};
                        float center = anchor == 0 ? 300.0f : anchor == 1 ? 600.0f : 900.0f;
                        auto direction = edge ? shape::Edge::Top : shape::Edge::Bottom;
                        auto extension =
                            shape::JoinedExtension(pill, {center - width / 2, 0, center + width / 2, 320}, direction);
                        auto *path = cache.Union(factory.Get(), pill, extension, direction, static_cast<float>(dpi));
                        ComPtr<ID2D1SolidColorBrush> brush;
                        unsigned fill = appearance == 0 ? 0x161616 : appearance == 1 ? 0xF4F1EB : 0x000000;
                        target->CreateSolidColorBrush(D2D1::ColorF(fill), &brush);
                        target->BeginDraw();
                        target->Clear(D2D1::ColorF(0, 0.0f));
                        target->FillGeometry(path, brush.Get());
                        brush->SetColor(D2D1::ColorF(appearance == 2 ? 0xFFFFFF : 0x777777));
                        target->DrawGeometry(path, brush.Get(), appearance == 2 ? 2.0f : 1 / scale);
                        RequireRaster(SUCCEEDED(target->EndDraw()), "union draw succeeds");
                        WICRect region{0, 0, static_cast<INT>(pixelWidth), static_cast<INT>(pixelHeight)};
                        ComPtr<IWICBitmapLock> lock;
                        RequireRaster(SUCCEEDED(bitmap->Lock(&region, WICBitmapLockRead, &lock)), "read union pixels");
                        UINT stride = 0, size = 0;
                        BYTE *bytes = nullptr;
                        lock->GetStride(&stride);
                        lock->GetDataPointer(&size, &bytes);
                        auto pixel = [&](float x, float y) {
                            UINT px = static_cast<UINT>(x * scale), py = static_cast<UINT>(y * scale);
                            return *reinterpret_cast<const unsigned *>(bytes + py * stride + px * 4);
                        };
                        float join = edge ? pill.bottom : pill.top;
                        float low = std::max(pill.left, extension.left) + shape::Fillet + 8;
                        float high = std::min(pill.right, extension.right) - shape::Fillet - 8;
                        for (float x = low; x < high; x += 2)
                            for (float y : {join - 2, join - 1, join, join + 1, join + 2})
                                RequireRaster(pixel(x, y) == (0xFF000000 | fill),
                                              "join has no gap, interior hairline or color discontinuity");
                        RequireRaster(pixel((pill.left + pill.right) / 2, (pill.top + pill.bottom) / 2) ==
                                          (0xFF000000 | fill),
                                      "pill center is opaque");
                        RequireRaster(pixel(8, 8) == 0, "outside silhouette remains transparent");
                        lock.Reset();
                        auto name = L"join-" + std::to_wstring(dpi) + L"-" + std::to_wstring(edge) + L"-" +
                                    std::to_wstring(appearance) + L"-" + std::to_wstring(static_cast<int>(width)) +
                                    L"-" + std::to_wstring(anchor) + L".png";
                        SaveRaster(images.Get(), bitmap.Get(), output / name, pixelWidth, pixelHeight);
                        ComPtr<IWICBitmap> crop;
                        ComPtr<IWICBitmapScaler> zoom;
                        float cropX = extension.left + shape::Fillet;
                        UINT cropLeft = static_cast<UINT>(std::max(0.0f, (cropX - 32) * scale));
                        UINT cropTop = static_cast<UINT>((join - 32) * scale);
                        UINT cropSize = static_cast<UINT>(64 * scale);
                        RequireRaster(SUCCEEDED(images->CreateBitmapFromSourceRect(bitmap.Get(), cropLeft, cropTop,
                                                                                   cropSize, cropSize, &crop)) &&
                                          SUCCEEDED(images->CreateBitmapScaler(&zoom)) &&
                                          SUCCEEDED(zoom->Initialize(crop.Get(), 256, 256,
                                                                     WICBitmapInterpolationModeNearestNeighbor)),
                                      "create zoomed join crop");
                        SaveRaster(images.Get(), zoom.Get(), output / (L"crop-" + name), 256, 256);
                        ++count;
                    }
    std::puts("Join raster checks completed.");
    std::fflush(stdout);
    // Probe every corner token against a separately sampled mathematical path.
    for (float radius : {shape::Surface, shape::Card, shape::Control, shape::Chip}) {
        std::printf("Corner token %.0f\n", radius);
        std::fflush(stdout);
        auto *path = cache.Rounded(factory.Get(), 240, 120, radius);
        RequireRaster(!shape::Contains(path, {.1f, .1f}) && shape::Contains(path, {radius, radius}) &&
                          shape::Contains(path, {radius * 1.6f, .25f}),
                      "corner-token distance probes");
    }
    std::printf("PASS: %u join rasters; four DPIs, both edges, three materials, three widths/anchors; corner probes.\n",
                count);
    unsigned corners = 0;
    for (UINT dpi : {96U, 120U, 144U, 192U})
        for (float radius : {shape::Surface, shape::Card, shape::Control, shape::Chip}) {
            float scale = dpi / 96.0f;
            UINT width = static_cast<UINT>(240 * scale), height = static_cast<UINT>(120 * scale);
            ComPtr<IWICBitmap> bitmap;
            ComPtr<ID2D1RenderTarget> target;
            ComPtr<ID2D1SolidColorBrush> brush;
            RequireRaster(SUCCEEDED(images->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                                         WICBitmapCacheOnLoad, &bitmap)) &&
                              SUCCEEDED(factory->CreateWicBitmapRenderTarget(
                                  bitmap.Get(),
                                  D2D1::RenderTargetProperties(
                                      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                      static_cast<float>(dpi), static_cast<float>(dpi)),
                                  &target)) &&
                              SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF), &brush)),
                          "corner audit target");
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0, 0.0f));
            graphics::Rounded(target.Get(), factory.Get(), cache, {0, 0, 240, 120}, radius, brush.Get());
            RequireRaster(SUCCEEDED(target->EndDraw()), "corner audit draw");
            WICRect rect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
            ComPtr<IWICBitmapLock> lock;
            RequireRaster(SUCCEEDED(bitmap->Lock(&rect, WICBitmapLockRead, &lock)), "corner audit pixels");
            UINT stride = 0, length = 0;
            BYTE *pixels = nullptr;
            lock->GetStride(&stride);
            lock->GetDataPointer(&length, &pixels);
            auto probe = [&](float fraction) {
                UINT point = static_cast<UINT>(radius * fraction * scale);
                return *reinterpret_cast<const unsigned *>(pixels + point * stride + point * 4);
            };
            RequireRaster(probe(.15f) == 0 && probe(.4f) == 0xFFFFFFFF,
                          "raster distance probes distinguish every expected corner radius");
            lock.Reset();
            auto name = L"radius-" + std::to_wstring(dpi) + L"-" + std::to_wstring(static_cast<int>(radius)) + L".png";
            SaveRaster(images.Get(), bitmap.Get(), output / name, width, height);
            ++corners;
        }
    std::printf("PASS: %u raster radius audits through the production rounded-fill helper.\n", corners);
    std::fflush(stdout);
    cache = graphics::GeometryCache{};
    images.Reset();
    factory.Reset();
    CoUninitialize();
    return 0;
}
