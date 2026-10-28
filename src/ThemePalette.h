#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace palette {
struct ThemeSet {
    unsigned surface, card, hover, hairline, text, muted, accent;
};
inline constexpr ThemeSet Midnight{0x0B0B0C, 0x161617, 0x1C1C1E, 0x232325, 0xF2F2F2, 0x8C8C91, 0xCFAE60};
inline constexpr ThemeSet Paper{0xEEEBE3, 0xF7F5EF, 0xDEDBD4, 0xCFCCC5, 0x0F0F0E, 0x65635E, 0xCFAE60};

struct Hsl {
    float h, s, l;
};
inline Hsl ToHsl(unsigned rgb) {
    float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    float high = std::max({r, g, b}), low = std::min({r, g, b}), d = high - low, l = (high + low) / 2, h = 0, s = 0;
    if (d > 0.0001f) {
        s = d / (1 - std::abs(2 * l - 1));
        h = high == r ? std::fmod((g - b) / d + 6.0f, 6.0f) : high == g ? (b - r) / d + 2 : (r - g) / d + 4;
        h /= 6;
    }
    return {h, s, l};
}
inline unsigned FromHsl(Hsl value) {
    float c = (1 - std::abs(2 * value.l - 1)) * value.s, x = c * (1 - std::abs(std::fmod(value.h * 6, 2.0f) - 1)),
          m = value.l - c / 2;
    float r = 0, g = 0, b = 0;
    int sector = static_cast<int>(value.h * 6) % 6;
    switch (sector) {
    case 0:
        r = c;
        g = x;
        break;
    case 1:
        r = x;
        g = c;
        break;
    case 2:
        g = c;
        b = x;
        break;
    case 3:
        g = x;
        b = c;
        break;
    case 4:
        r = x;
        b = c;
        break;
    default:
        r = c;
        b = x;
        break;
    }
    return (static_cast<unsigned>(std::lround((r + m) * 255)) << 16) |
           (static_cast<unsigned>(std::lround((g + m) * 255)) << 8) | static_cast<unsigned>(std::lround((b + m) * 255));
}
inline unsigned Accent(const unsigned char *bgra, std::size_t bytes) {
    struct Bin {
        double weight = 0, h = 0, s = 0, l = 0;
    };
    std::array<Bin, 24> bins{};
    double neutral = 0;
    std::size_t samples = 0;
    for (std::size_t i = 0; i + 3 < bytes; i += 16) {
        unsigned color =
            (static_cast<unsigned>(bgra[i + 2]) << 16) | (static_cast<unsigned>(bgra[i + 1]) << 8) | bgra[i];
        auto hsl = ToHsl(color);
        neutral += hsl.l;
        ++samples;
        if (hsl.s < 0.12f || hsl.l < 0.08f || hsl.l > 0.94f)
            continue;
        double weight = hsl.s * (1 - std::abs(hsl.l - 0.55f));
        auto &bin = bins[std::min(23, static_cast<int>(hsl.h * 24))];
        bin.weight += weight;
        bin.h += hsl.h * weight;
        bin.s += hsl.s * weight;
        bin.l += hsl.l * weight;
    }
    auto best = std::max_element(bins.begin(), bins.end(), [](auto a, auto b) { return a.weight < b.weight; });
    if (best->weight < 0.001)
        return FromHsl({0, 0, static_cast<float>(samples && neutral / samples > 0.5 ? 0.78 : 0.66)});
    return FromHsl({static_cast<float>(best->h / best->weight),
                    std::clamp(static_cast<float>(best->s / best->weight), 0.28f, 0.62f),
                    std::clamp(static_cast<float>(best->l / best->weight), 0.62f, 0.76f)});
}
inline float RevealRadius(float progress, float width, float height) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    float eased = 1 - std::pow(1.0f - progress, 3.0f);
    return (std::hypot(width / 2, height / 2) + 2) * eased;
}
} // namespace palette
