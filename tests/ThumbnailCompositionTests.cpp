#include <windows.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <cstdio>
#include "OwnedWindowCapture.h"
#include "../src/CompositionSurface.h"
Microsoft::WRL::ComPtr<ID2D1RenderTarget> canvas;
CompositionSurface composition;
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (canvas) {
            canvas->BeginDraw();
            canvas->Clear(D2D1::ColorF(0x102030));
            canvas->EndDraw();
            composition.Present();
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_TIMER || message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW sourceClass{};
    sourceClass.hInstance = GetModuleHandleW(nullptr);
    sourceClass.lpfnWndProc = DefWindowProcW;
    sourceClass.lpszClassName = L"Dako.InertThumbnailSource";
    sourceClass.hbrBackground = CreateSolidBrush(RGB(240, 60, 180));
    RegisterClassW(&sourceClass);
    HWND source = CreateWindowExW(WS_EX_TOOLWINDOW, sourceClass.lpszClassName, L"Inert thumbnail source", WS_POPUP, 20,
                                  20, 320, 200, nullptr, nullptr, sourceClass.hInstance, nullptr);
    ShowWindow(source, SW_SHOWNOACTIVATE);
    UpdateWindow(source);
    WNDCLASSW rootClass{};
    rootClass.hInstance = sourceClass.hInstance;
    rootClass.lpfnWndProc = Procedure;
    rootClass.lpszClassName = L"Dako.ThumbnailCompatibilityProbe";
    RegisterClassW(&rootClass);
    HWND root = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, rootClass.lpszClassName,
                                L"Dako thumbnail compatibility probe", WS_POPUP, 400, 250, 480, 320, nullptr, nullptr,
                                sourceClass.hInstance, nullptr);
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
    HRESULT created = composition.Create(root, factory.Get(), 480, 320, 96, &canvas);
    printf("composition=%08lx\n", created);
    if (FAILED(created))
        return 2;
    ShowWindow(root, SW_SHOWNOACTIVATE);
    UpdateWindow(root);
    HTHUMBNAIL thumbnail = nullptr;
    HRESULT registered = DwmRegisterThumbnail(root, source, &thumbnail);
    printf("thumbnail=%08lx\n", registered);
    if (FAILED(registered))
        return 3;
    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
    props.rcDestination = {60, 70, 420, 270};
    props.fVisible = TRUE;
    props.opacity = 255;
    HRESULT updated = DwmUpdateThumbnailProperties(thumbnail, &props);
    DwmFlush();
    printf("update=%08lx; expect a magenta rectangle above the opaque navy swapchain\n", updated);
    fflush(stdout);
    auto pixels = fixture::CaptureOwnedWindow(root);
    printf("capture=%08lx center=%08x corner=%08x\n", pixels.status, pixels.center, pixels.corner);
    bool composited =
        SUCCEEDED(pixels.status) && (pixels.center & 0xFFFFFF) == 0xF03CB4 && (pixels.corner & 0xFFFFFF) == 0x102030;
    DestroyWindow(root);
    DwmUnregisterThumbnail(thumbnail);
    DestroyWindow(source);
    canvas.Reset();
    composition.Reset();
    DeleteObject(sourceClass.hbrBackground);
    CoUninitialize();
    if (!composited) {
        std::fprintf(stderr, "FAIL: DWM thumbnail is not visible above the swapchain\n");
        return 5;
    }
    std::puts("PASS: owned-window pixel capture confirms a DWM thumbnail above a NOREDIRECTIONBITMAP swapchain.");
    return 0;
}
