#pragma once
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <memory>
#include <mutex>

// Native premultiplied composition surface. The software fixture renderer stays
// independent, and the caller can fall back to the opaque HWND renderer.
class CompositionSurface {
    struct SharedGpu {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
    };
    std::shared_ptr<SharedGpu> sharedGpu_;
    Microsoft::WRL::ComPtr<ID3D11Device> gpu_;
    Microsoft::WRL::ComPtr<ID2D1Device> device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> destination_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    UINT width_ = 0, height_ = 0;
    HANDLE frameReady_ = nullptr;
    UINT swapFlags_ = 0;
    HRESULT Bind(float dpi) {
        Microsoft::WRL::ComPtr<IDXGISurface> surface;
        HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(&surface));
        if (FAILED(hr))
            return hr;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        hr = context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &bitmap);
        if (SUCCEEDED(hr)) {
            context_->SetTarget(bitmap.Get());
            context_->SetDpi(dpi, dpi);
            context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        }
        return hr;
    }

  public:
    ~CompositionSurface() { Reset(); }
    HRESULT Create(HWND window, ID2D1Factory *factory, UINT width, UINT height, float dpi, ID2D1RenderTarget **output) {
        // Visible surfaces share a device; the weak pool releases it at the last collapse/exit.
        static std::mutex poolMutex;
        static std::weak_ptr<SharedGpu> pool;
        HRESULT hr = S_OK;
        {
            std::scoped_lock lock(poolMutex);
            sharedGpu_ = pool.lock();
            if (!sharedGpu_) {
                auto next = std::make_shared<SharedGpu>();
                UINT flags =
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                       &next->device, nullptr, nullptr);
                if (FAILED(hr))
                    return hr;
                sharedGpu_ = std::move(next);
                pool = sharedGpu_;
            }
            gpu_ = sharedGpu_->device;
        }
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> manager;
        Microsoft::WRL::ComPtr<ID2D1Factory1> modern;
        if (FAILED(hr = gpu_.As(&dxgi)) || FAILED(hr = dxgi->GetAdapter(&adapter)) ||
            FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&manager))) ||
            FAILED(hr = factory->QueryInterface(IID_PPV_ARGS(&modern))))
            return hr;
        if (FAILED(hr = modern->CreateDevice(dxgi.Get(), &device_)) ||
            FAILED(hr = device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_)))
            return hr;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = std::max(1U, width);
        desc.Height = std::max(1U, height);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        hr = manager->CreateSwapChainForComposition(gpu_.Get(), &desc, nullptr, &swap_);
        if (FAILED(hr)) {
            desc.Flags = 0;
            hr = manager->CreateSwapChainForComposition(gpu_.Get(), &desc, nullptr, &swap_);
        }
        if (FAILED(hr))
            return hr;
        swapFlags_ = desc.Flags;
        Microsoft::WRL::ComPtr<IDXGISwapChain2> paced;
        if (desc.Flags && SUCCEEDED(swap_.As(&paced)) && SUCCEEDED(paced->SetMaximumFrameLatency(1)))
            frameReady_ = paced->GetFrameLatencyWaitableObject();
        // FrameClock owns readiness waits. Never block input or consume the first
        // readiness token on the window thread during a cold reveal.
        if (FAILED(hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition_))))
            return hr;
        if (FAILED(hr = composition_->CreateTargetForHwnd(window, FALSE, &destination_)) ||
            FAILED(hr = composition_->CreateVisual(&visual_)) || FAILED(hr = visual_->SetContent(swap_.Get())) ||
            FAILED(hr = destination_->SetRoot(visual_.Get())) || FAILED(hr = composition_->Commit()))
            return hr;
        width_ = desc.Width;
        height_ = desc.Height;
        if (FAILED(hr = Bind(dpi)))
            return hr;
        return context_.CopyTo(output);
    }
    HANDLE FrameReady() const { return frameReady_; }
    bool Active() const { return swap_ != nullptr; }
    HRESULT Resize(UINT width, UINT height, float dpi) {
        if (!swap_ || !width || !height || width == width_ && height == height_)
            return S_OK;
        context_->SetTarget(nullptr);
        HRESULT hr = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapFlags_);
        if (FAILED(hr))
            return hr;
        width_ = width;
        height_ = height;
        return Bind(dpi);
    }
    HRESULT Statistics(DXGI_FRAME_STATISTICS *statistics) {
        return swap_ ? swap_->GetFrameStatistics(statistics) : E_PENDING;
    }
    HRESULT ReadPixels(std::vector<unsigned char> &pixels, UINT &width, UINT &height) {
        if (!swap_)
            return E_PENDING;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> source, staging;
        HRESULT hr = swap_->GetBuffer(0, IID_PPV_ARGS(&source));
        if (FAILED(hr))
            return hr;
        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        width = description.Width;
        height = description.Height;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = description.MiscFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(hr = gpu_->CreateTexture2D(&description, nullptr, &staging)))
            return hr;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        gpu_->GetImmediateContext(&immediate);
        immediate->CopyResource(staging.Get(), source.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(hr = immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return hr;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        for (UINT y = 0; y < height; ++y)
            memcpy(pixels.data() + static_cast<size_t>(y) * width * 4,
                   static_cast<const unsigned char *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                   static_cast<size_t>(width) * 4);
        immediate->Unmap(staging.Get(), 0);
        return S_OK;
    }
    HRESULT Present() { return swap_ ? swap_->Present(1, 0) : S_OK; }
    void Reset() {
        if (frameReady_) {
            CloseHandle(frameReady_);
            frameReady_ = nullptr;
        }
        swapFlags_ = 0;
        if (context_)
            context_->SetTarget(nullptr);
        if (visual_)
            visual_->SetContent(nullptr);
        if (composition_)
            composition_->Commit();
        visual_.Reset();
        destination_.Reset();
        composition_.Reset();
        swap_.Reset();
        context_.Reset();
        device_.Reset();
        gpu_.Reset();
        sharedGpu_.reset();
        width_ = height_ = 0;
    }
};
