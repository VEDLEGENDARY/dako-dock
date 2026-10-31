#pragma once
#include <algorithm>
#include <cmath>

namespace shape {
inline constexpr float Surface = 28;
inline constexpr float Card = 16;
inline constexpr float Control = 12;
inline constexpr float Chip = 8;
inline constexpr float Fillet = Surface;
inline constexpr float Smoothing = .6f;
inline constexpr float Pill(float height) { return height * .5f; }
inline constexpr float Inner(float outer, float inset) { return std::max(Chip, outer - inset); }
inline constexpr float Flat() { return 0; }
} // namespace shape
