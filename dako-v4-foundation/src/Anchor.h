#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include "DockMotion.h"

// Edge-relative geometry: the only place that knows whether the island hangs from the top or bottom.
namespace anchor {
enum class Edge { Top, Bottom };

// +1 grows downward (top edge), -1 grows upward (bottom edge).
constexpr float Grow(Edge edge) { return edge == Edge::Top ? 1.0f : -1.0f; }

// Reflect a y coordinate about the horizontal line that passes through `axis`.
constexpr float MirrorY(float y, float axis) { return 2 * axis - y; }

// Top-left y of a box of `height` that touches the screen edge.
constexpr float EdgeTop(Edge edge, float screenTop, float screenBottom, float height) {
    return edge == Edge::Top ? screenTop : screenBottom - height;
}

// Mirror a polygon built for the top edge so it hangs from the bottom edge. Top stays unchanged.
template <size_t N>
std::array<dock::Point, N> ForEdge(Edge edge, std::array<dock::Point, N> points, float top, float height) {
    if (edge == Edge::Top) return points;
    for (auto &p : points) p.y = MirrorY(p.y, top + height / 2);
    return points;
}

// The wheel direction that moves toward the screen centre: up at the bottom edge, down at the top edge.
// Windows reports wheel-up as a positive delta.
constexpr bool WheelExpands(Edge edge, int wheelDelta) {
    return edge == Edge::Bottom ? wheelDelta > 0 : wheelDelta < 0;
}

// Flyouts open toward the screen centre; flip only when they would leave the work area.
inline bool FlyoutOpensUp(Edge edge, float anchorY, float flyoutHeight, float screenTop, float screenBottom) {
    bool preferUp = edge == Edge::Bottom;
    bool fitsUp = anchorY - flyoutHeight >= screenTop, fitsDown = anchorY + flyoutHeight <= screenBottom;
    if (preferUp) return fitsUp || !fitsDown;
    return !fitsDown && fitsUp;
}
}
