#pragma once
#include "BundledFont.h"
#include <array>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace type {
enum Role {
    Caption, Secondary, Body, Section, Metric, Display, Icon, Control, Editorial, Mono,
    LargeIcon, TileIcon, InlineIcon, Count
};
inline constexpr Role Title = Section, Label = Control, Overline = Mono;
inline constexpr std::array<float, Count> Sizes{12, 12, 14, 16, 28, 40, 20, 13, 40, 11, 32, 24, 16};
inline constexpr float Leading = 1.25f, Tracking = .04f;
struct Library {
    std::unique_ptr<BundledFont> font;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, Count> formats;
    Microsoft::WRL::ComPtr<IDWriteTypography> numbers;
    explicit Library(IDWriteFactory *factory) : font(std::make_unique<BundledFont>(factory)) {
        Microsoft::WRL::ComPtr<IDWriteFontCollection> system;
        UINT32 index = 0;
        BOOL fluent = FALSE;
        if (SUCCEEDED(factory->GetSystemFontCollection(&system)))
            system->FindFamilyName(L"Segoe Fluent Icons", &index, &fluent);
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        factory->CreateTypography(&numbers);
        if (numbers)
            numbers->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
        for (size_t i = 0; i < formats.size(); ++i) {
            bool glyph = i == Icon || i == LargeIcon || i == TileIcon || i == InlineIcon;
            const wchar_t *family = glyph ? (fluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets") : L"Rubik";
            auto weight = i == Section ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                          : i == Display || i == Metric || i == Control || i == Mono ? DWRITE_FONT_WEIGHT_MEDIUM
                                                                                   : DWRITE_FONT_WEIGHT_NORMAL;
            HRESULT result = factory->CreateTextFormat(
                family, glyph ? nullptr : font->Collection(), weight, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, Sizes[i], locale, &formats[i]);
            if (FAILED(result))
                throw std::runtime_error("Could not create shared UI typography");
            auto &format = formats[i];
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, Sizes[i] * Leading, Sizes[i]);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            factory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
            format->SetTrimming(&trim, ellipsis.Get());
        }
    }
};
inline std::shared_ptr<Library> Acquire(IDWriteFactory *factory) {
    static std::mutex mutex;
    static std::weak_ptr<Library> shared;
    std::scoped_lock lock(mutex);
    auto result = shared.lock();
    if (!result) {
        result = std::make_shared<Library>(factory);
        shared = result;
    }
    return result;
}
} // namespace type
