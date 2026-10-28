#pragma once
#include <dwrite_3.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <wrl/client.h>

class BundledFont {
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory_;
    Microsoft::WRL::ComPtr<IDWriteInMemoryFontFileLoader> loader_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection_;
    std::vector<char> bytes_;

  public:
    explicit BundledFont(IDWriteFactory *factory) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        std::ifstream file(std::filesystem::path(executable).parent_path() / L"assets/fonts/Rubik.ttf",
                           std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > 1024 * 1024)
            return;
        bytes_.resize(static_cast<size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(bytes_.data(), static_cast<std::streamsize>(bytes_.size())) ||
            FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory_))) ||
            FAILED(factory_->CreateInMemoryFontFileLoader(&loader_)) ||
            FAILED(factory_->RegisterFontFileLoader(loader_.Get())))
            return;
        Microsoft::WRL::ComPtr<IDWriteFontFile> font;
        Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder;
        Microsoft::WRL::ComPtr<IDWriteFontSet> set;
        if (FAILED(loader_->CreateInMemoryFontFileReference(factory, bytes_.data(), static_cast<UINT32>(bytes_.size()),
                                                            nullptr, &font)) ||
            FAILED(factory_->CreateFontSetBuilder(&builder)) || FAILED(builder->AddFontFile(font.Get())) ||
            FAILED(builder->CreateFontSet(&set)))
            return;
        factory_->CreateFontCollectionFromFontSet(set.Get(), &collection_);
    }
    ~BundledFont() {
        collection_.Reset();
        if (factory_ && loader_)
            factory_->UnregisterFontFileLoader(loader_.Get());
    }
    IDWriteFontCollection *Collection() const { return collection_.Get(); }
};
