#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "LayoutMetrics.h"

namespace layout {
struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    float Width() const { return std::max(0.0f, right - left); }
    float Height() const { return std::max(0.0f, bottom - top); }
    float CenterX() const { return (left + right) / 2; }
    bool Contains(float x, float y) const { return x >= left && x <= right && y >= top && y <= bottom; }
};
inline bool Intersects(Rect a, Rect b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
inline Rect Intersection(Rect a, Rect b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}
constexpr float Padding = Metrics::Padding, Gap = Metrics::Gap, Section = Metrics::Section;
constexpr float PanelRadius = Metrics::PanelRadius, CardRadius = Metrics::CardRadius;
constexpr float ControlRadius = Metrics::ControlRadius;
enum class View { Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power, Wireless, Media };
struct Input {
    float availableWidth = 900, availableHeight = 748;
    View view = View::Controls;
    size_t items = 0;
};
struct Result {
    Rect panel, header, body, status, footer, stage, previous, next;
    float padding = Padding, contentHeight = 0, headerHeight = Metrics::HeaderHeight;
    bool narrow = false, emergency = false, editorial = false;
};
inline Result Solve(Input in) {
    Result r;
    float preferred = in.view == View::Wireless ? Metrics::WirelessWidth
                      : in.view == View::Media  ? Metrics::MediaWidth
                                                : Metrics::MainWidth;
    float w = std::min(preferred, std::max(Metrics::MinWidth, in.availableWidth));
    r.narrow = w < Metrics::NarrowWidth;
    r.emergency = in.availableHeight < Metrics::EmergencyExtent || w < Metrics::EmergencyExtent;
    bool compact = in.availableHeight < Metrics::CompactHeight;
    r.padding = compact ? Metrics::Space12 : Padding;
    float gap = compact ? Metrics::Space4 : Section;
    float status = compact ? Metrics::CompactStatusHeight : Metrics::StatusHeight;
    r.headerHeight = Metrics::HeaderHeight;
    r.editorial = false;
    float content =
        in.view == View::Today      ? (w >= Metrics::ColumnsWidth ? Metrics::BodyHeight : Metrics::TodayStackHeight)
        : in.view == View::Stats    ? (w >= Metrics::ColumnsWidth ? Metrics::BodyHeight : Metrics::StatsStackHeight)
        : in.view == View::Controls ? (r.narrow ? Metrics::ControlsStackHeight : Metrics::BodyHeight)
                                    : Metrics::BodyHeight;
    float body = in.view == View::Wireless ? Metrics::WirelessBodyHeight
                 : in.view == View::Media  ? Metrics::MediaBodyHeight
                                           : Metrics::BodyHeight;
    float foot = compact ? Metrics::CompactFooterHeight : Metrics::FooterHeight;
    float h = std::min(r.headerHeight + gap + body + gap + foot + (foot ? gap : 0.0f) + status + r.padding,
                       std::max(Metrics::MinHeight, in.availableHeight));
    if (in.view == View::Media)
        h = std::min(Metrics::MediaPanelHeight, in.availableHeight);
    if (r.emergency && in.view != View::Media) {
        r.headerHeight = Metrics::CompactHeaderHeight;
        r.padding = Metrics::Space12;
        gap = Metrics::Space4;
        status = foot = 0;
        h = std::min(h, std::max(Metrics::MinHeight, in.availableHeight));
    }
    r.panel = {0, 0, w, h};
    r.header = {r.padding, Metrics::Space8, w - r.padding, r.headerHeight};
    r.status = {r.padding, h - r.padding - status, w - r.padding, h - r.padding};
    r.footer = {r.padding, r.status.top - gap - foot, w - r.padding, r.status.top - gap};
    r.body = {r.padding, r.headerHeight + gap, w - r.padding, (foot ? r.footer.top : r.status.top) - gap};
    if (in.view == View::Media)
        r.body = {Padding, Metrics::Space12, w - Padding, h - Padding};
    r.contentHeight = in.view == View::Apps        ? (r.emergency ? Metrics::EmergencyAppsHeight : r.body.Height())
                      : in.view == View::Clipboard ? r.body.Height()
                      : r.emergency && in.view == View::Wallpaper ? Metrics::EmergencyWallpaperHeight
                                                                  : std::max(content, body);
    r.stage = {r.body.left + Metrics::StageInsetX, r.body.top + Metrics::StageInsetY,
               r.body.right - Metrics::StageInsetX,
               r.emergency ? std::max(r.body.bottom, r.body.top + Metrics::EmergencyWallpaperHeight) : r.body.bottom};
    float cy = (r.stage.top + r.stage.bottom) / 2;
    float half = Metrics::NavigationSize / 2;
    r.previous = {r.body.left, cy - half, r.body.left + Metrics::NavigationSize, cy + half};
    r.next = {r.body.right - Metrics::NavigationSize, cy - half, r.body.right, cy + half};
    if (!r.emergency && in.view != View::Wireless && in.view != View::Media) {
        h = std::min(390.0f, in.availableHeight);
        r.panel.bottom = h;
        r.headerHeight = 48;
        r.header = {r.padding, 4, w - r.padding, 48};
        r.body = {r.padding, 56, w - r.padding, std::max(64.0f, h - 44)};
        r.footer = {r.padding, h - 32, w - r.padding, h - 8};
        r.status = {r.padding, h, w - r.padding, h};
        if (in.view == View::Apps || in.view == View::Clipboard)
            r.contentHeight = r.body.Height();
    }
    return r;
}
inline Rect Column(Rect area, int index, int count, float gap = Gap) {
    float w = (area.Width() - gap * (count - 1)) / count;
    return {area.left + index * (w + gap), area.top, area.left + index * (w + gap) + w, area.bottom};
}
inline bool RoundedContains(Rect r, float radius, float x, float y) {
    if (!r.Contains(x, y))
        return false;
    float cx = std::clamp(x, r.left + radius, r.right - radius), cy = std::clamp(y, r.top + radius, r.bottom - radius);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
}
inline bool SkewContains(Rect r, float skew, float radius, float x, float y) {
    return RoundedContains(r, radius, x - skew * (y - (r.top + r.bottom) / 2), y);
}
struct AppPage {
    size_t begin = 0, end = 0;
};
inline std::vector<AppPage> Paginate(const std::vector<int> &groups, float height,
                                     float rowHeight = Metrics::AppRowHeight,
                                     float headingHeight = Metrics::AppHeadingHeight) {
    std::vector<AppPage> pages;
    size_t first = 0;
    float used = 0;
    int group = -1;
    for (size_t i = 0; i < groups.size(); ++i) {
        float cost = rowHeight + (group == groups[i] ? 0.0f : headingHeight);
        if (i > first && used + cost > height) {
            pages.push_back({first, i});
            first = i;
            used = 0;
            group = -1;
            cost = rowHeight + headingHeight;
        }
        used += cost;
        group = groups[i];
    }
    if (!groups.empty())
        pages.push_back({first, groups.size()});
    return pages;
}
} // namespace layout
