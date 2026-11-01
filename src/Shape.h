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

struct Point {
    float x = 0, y = 0;
};
struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    float Width() const { return std::max(0.0f, right - left); }
    float Height() const { return std::max(0.0f, bottom - top); }
};
inline float Extent(Rect rect, float radius, float smoothing = Smoothing) {
    return std::min(
        {std::max(0.0f, radius) * (1 + std::clamp(smoothing, 0.0f, 1.0f)), rect.Width() / 2, rect.Height() / 2});
}
enum class Edge { Top, Bottom };
enum class Command { Line, Cubic, Arc };
struct Segment {
    Command command = Command::Line;
    Point end, first, second;
    float radius = 0;
    bool clockwise = true;
};
// Fixed storage keeps shape construction allocation-free and SDK independent.
struct Path {
    Point start;
    Segment segments[32]{};
    unsigned count = 0;
    bool reflected = false;
    float axis = 0;
    void Line(Point end) { segments[count++] = {Command::Line, end}; }
    void Cubic(Point first, Point second, Point end) { segments[count++] = {Command::Cubic, end, first, second}; }
    void Arc(Point end, float radius, bool clockwise) {
        segments[count++] = {Command::Arc, end, {}, {}, radius, clockwise};
    }
};
inline Point Add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point Scale(Point point, float scale) { return {point.x * scale, point.y * scale}; }
inline Point Difference(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline float Length(Point point) { return std::hypot(point.x, point.y); }
inline float Mix(float a, float b, float amount) { return a + (b - a) * amount; }

inline Path RoundedPolygon(const Point *points, unsigned count, float radius, float smoothing,
                           const bool *circular = nullptr) {
    Path path;
    Point starts[8]{}, ends[8]{}, incoming[8]{}, outgoing[8]{};
    float extents[8]{}, radii[8]{}, smooth[8]{};
    bool convex[8]{};
    if (count < 3 || count > 8)
        return path;
    for (unsigned i = 0; i < count; ++i) {
        auto before = Difference(points[i], points[(i + count - 1) % count]);
        auto after = Difference(points[(i + 1) % count], points[i]);
        float a = Length(before), b = Length(after);
        if (a < .001f || b < .001f)
            return {};
        incoming[i] = Scale(before, 1 / a);
        outgoing[i] = Scale(after, 1 / b);
        convex[i] = before.x * after.y - before.y * after.x > 0;
        smooth[i] = convex[i] && !(circular && circular[i]) ? std::clamp(smoothing, 0.0f, 1.0f) : 0;
        extents[i] = std::min({std::max(0.0f, radius) * (1 + smooth[i]), a / 2, b / 2});
        radii[i] = extents[i] / (1 + smooth[i]);
        starts[i] = Add(points[i], Scale(incoming[i], -extents[i]));
        ends[i] = Add(points[i], Scale(outgoing[i], extents[i]));
    }
    path.start = starts[0];
    for (unsigned i = 0; i < count; ++i) {
        if (i)
            path.Line(starts[i]);
        if (extents[i] <= .001f) {
            path.Line(ends[i]);
            continue;
        }
        if (!convex[i]) {
            path.Arc(ends[i], radii[i], false);
            continue;
        }
        auto local = [&](float x, float y) {
            return Add(points[i], Add(Scale(incoming[i], -x), Scale(outgoing[i], y)));
        };
        if (smooth[i] <= .001f) {
            float control = extents[i] * (1 - .552284749831f);
            path.Cubic(local(control, 0), local(0, control), ends[i]);
        } else {
            float r = radii[i], extent = extents[i], diagonal = r * (1 - std::sqrt(.5f));
            float halfControl = .26521648984f * r, amount = std::min(1.0f, smooth[i] / Smoothing);
            float a = Mix(extent - halfControl, (extent + 2 * diagonal) / 2, amount);
            float b = Mix(diagonal + halfControl * std::sqrt(.5f), 2 * diagonal, amount);
            float c = Mix(diagonal - halfControl * std::sqrt(.5f), 0, amount);
            path.Cubic(local(a, 0), local(b, c), local(diagonal, diagonal));
            path.Cubic(local(c, b), local(0, a), ends[i]);
        }
    }
    return path;
}
inline Path RoundedPath(Rect rect, float radius = Surface, float smoothing = Smoothing) {
    if (rect.Width() <= 0 || rect.Height() <= 0)
        return {};
    Point corners[]{{rect.left, rect.top}, {rect.right, rect.top}, {rect.right, rect.bottom}, {rect.left, rect.bottom}};
    return RoundedPolygon(corners, 4, radius, smoothing);
}
inline Rect JoinedExtension(Rect pill, Rect extension, Edge edge) {
    float width = extension.Width();
    if (std::abs(width - pill.Width()) < 2 * Fillet) {
        extension.left = pill.left;
        extension.right = pill.right;
    } else {
        float low = width < pill.Width() ? pill.left + 2 * Fillet : pill.right + 2 * Fillet - width;
        float high = width < pill.Width() ? pill.right - 2 * Fillet - width : pill.left - 2 * Fillet;
        extension.left = std::clamp(extension.left, low, std::max(low, high));
        extension.right = extension.left + width;
    }
    float height = extension.Height();
    extension.top = edge == Edge::Bottom ? pill.top - height : pill.bottom;
    extension.bottom = extension.top + height;
    return extension;
}
inline Path Union(Rect pill, Rect extension, Edge edge = Edge::Bottom) {
    extension = JoinedExtension(pill, extension, edge);
    float axis = (pill.top + pill.bottom) / 2;
    if (edge == Edge::Top)
        extension = {extension.left, 2 * axis - extension.bottom, extension.right, 2 * axis - extension.top};
    if (std::abs(extension.left - pill.left) < .001f && std::abs(extension.right - pill.right) < .001f) {
        Point corners[]{{pill.left, extension.top},
                        {pill.right, extension.top},
                        {pill.right, pill.bottom},
                        {pill.left, pill.bottom}};
        bool circular[]{false, false, true, true};
        auto result = RoundedPolygon(corners, 4, Surface, Smoothing, circular);
        result.reflected = edge == Edge::Top;
        result.axis = axis;
        return result;
    }
    Point outline[]{{extension.left, extension.top}, {extension.right, extension.top}, {extension.right, pill.top},
                    {pill.right, pill.top},          {pill.right, pill.bottom},        {pill.left, pill.bottom},
                    {pill.left, pill.top},           {extension.left, pill.top}};
    bool circular[]{false, false, extension.right<pill.right, true, true, true, true, extension.left> pill.left};
    auto result = RoundedPolygon(outline, 8, Surface, Smoothing, circular);
    result.reflected = edge == Edge::Top;
    result.axis = axis;
    return result;
}
} // namespace shape
