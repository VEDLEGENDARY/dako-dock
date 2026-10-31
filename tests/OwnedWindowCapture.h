#pragma once
#include <windows.h>
#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace fixture {
struct CapturePixels {
    HRESULT status = E_PENDING;
    unsigned center = 0, corner = 0;
};
// Test-only: capture an HWND created in this process, never the desktop/another app.
inline CapturePixels CaptureOwnedWindow(HWND window, DWORD timeout = 4000) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (!window || process != GetCurrentProcessId())
        return {E_ACCESSDENIED};
    using namespace winrt::Windows::Graphics;
    using Microsoft::WRL::ComPtr;
    struct State {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::mutex mutex;
        std::atomic<bool> done = false;
        CapturePixels pixels;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ~State() {
            if (event)
                CloseHandle(event);
        }
    };
    auto state = std::make_shared<State>();
    try {
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                               &state->device, nullptr, &state->context));
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(state->device.As(&dxgi));
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        auto device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        auto interop = winrt::get_activation_factory<Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        Capture::GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(
            interop->CreateForWindow(window, winrt::guid_of<Capture::GraphicsCaptureItem>(), winrt::put_abi(item)));
        auto pool = Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, item.Size());
        auto token = pool.FrameArrived([state](const auto &sender, const auto &) {
            std::scoped_lock lock(state->mutex);
            if (state->done)
                return;
            try {
                auto frame = sender.TryGetNextFrame();
                if (!frame)
                    return;
                auto access =
                    frame.Surface()
                        .template as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                ComPtr<ID3D11Texture2D> source, staging;
                winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&source)));
                D3D11_TEXTURE2D_DESC description{};
                source->GetDesc(&description);
                description.Usage = D3D11_USAGE_STAGING;
                description.BindFlags = description.MiscFlags = 0;
                description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                winrt::check_hresult(state->device->CreateTexture2D(&description, nullptr, &staging));
                state->context->CopyResource(staging.Get(), source.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                winrt::check_hresult(state->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                auto sample = [&](UINT x, UINT y) {
                    auto row = static_cast<const BYTE *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
                    return reinterpret_cast<const unsigned *>(row)[x];
                };
                state->pixels.center = sample(description.Width / 2, description.Height / 2);
                state->pixels.corner = sample(20, 20);
                state->context->Unmap(staging.Get(), 0);
                state->pixels.status = S_OK;
            } catch (const winrt::hresult_error &error) {
                state->pixels.status = error.code();
            }
            state->done = true;
            SetEvent(state->event);
        });
        auto session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(false);
        session.StartCapture();
        auto started = GetTickCount64();
        while (!state->done && GetTickCount64() - started < timeout) {
            MsgWaitForMultipleObjectsEx(1, &state->event, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        session.Close();
        pool.FrameArrived(token);
        pool.Close();
        std::scoped_lock lock(state->mutex);
        return state->pixels;
    } catch (const winrt::hresult_error &error) {
        return {error.code()};
    }
}
} // namespace fixture
