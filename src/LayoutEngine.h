#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <initializer_list>
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
enum class View {
    Controls,
    Today,
    Stats,
    Clipboard,
    Apps,
    Wallpaper,
    Power,
    Wireless,
    Media,
    Features,
    Palette,
    Status,
    Module
};
inline float ReferenceBodyHeight(View view) {
    switch (view) {
    case View::Controls:
        return 288;
    case View::Today:
        return 336;
    case View::Stats:
        return 240;
    case View::Clipboard:
        return 344;
    case View::Wallpaper:
        return 224;
    default:
        return Metrics::BodyHeight;
    }
}
struct Input {
    float availableWidth = 900, availableHeight = 748;
    View view = View::Controls;
    float preferredWidth = 0, preferredBodyHeight = 0;
    size_t items = 0;
    size_t active = 0, events = 0;
    int page = 0;
    bool offlineIntro = false, image = false, bottom = true;
};
struct Result {
    Rect panel, header, body, status, footer, stage, previous, next;
    float padding = Padding, contentHeight = 0, headerHeight = Metrics::HeaderHeight;
    bool narrow = false, emergency = false, editorial = false;
};
inline float FocusHeight(float width) {
    int columns = std::max(1, static_cast<int>((width - 24) / Metrics::MinHit));
    float start = width < 104 ? 124.0f : 88.0f;
    return std::max(136.0f, start + static_cast<float>((7 + columns - 1) / columns) * 36);
}
inline float StatsLeftHeight(float width) { return width < 240 ? 840.0f : Metrics::BodyHeight; }
inline Result Solve(Input in) {
    Result r;
    bool flyout = in.view == View::Wireless || in.view >= View::Features;
    float preferred = in.view == View::Media                                      ? Metrics::MediaWidth
                      : in.view == View::Controls                                 ? Metrics::ControlsWidth
                      : in.view == View::Wallpaper                                ? Metrics::WallpaperWidth
                      : flyout || in.view == View::Apps || in.view == View::Power ? Metrics::WirelessWidth
                                                                                  : Metrics::MainWidth;
    if (in.preferredWidth > 0)
        preferred = in.preferredWidth;
    float w = std::min(preferred, std::max(Metrics::MinWidth, in.availableWidth));
    float reference = in.preferredBodyHeight > 0 ? in.preferredBodyHeight : ReferenceBodyHeight(in.view);
    float h = std::min(reference + 80, std::max(Metrics::MinHeight, in.availableHeight));
    r.narrow = w < (in.view == View::Controls ? Metrics::ControlsNarrowWidth : Metrics::NarrowWidth);
    r.emergency = h < Metrics::EmergencyExtent || w < 7 * Metrics::MinHit + 2 * Metrics::PanelInset + 44;
    r.padding = r.emergency ? Metrics::Space12 : Metrics::PanelInset;
    float leadingInset = r.emergency ? Metrics::Space4 : in.bottom ? Metrics::FreeInset : Metrics::AttachedInset;
    float trailingInset = r.emergency ? Metrics::Space8 : in.bottom ? Metrics::AttachedInset : Metrics::FreeInset;
    r.headerHeight = r.emergency ? Metrics::CompactHeaderHeight : Metrics::HeaderHeight;
    float gap = r.emergency ? Metrics::Space4 : Metrics::Gutter;
    float footer = r.emergency || !flyout ? 0 : Metrics::FooterHeight;
    if (in.view == View::Media)
        h = std::min(Metrics::MediaPanelHeight, in.availableHeight);
    r.panel = {0, 0, w, h};
    r.header = {r.padding, leadingInset, w - r.padding, leadingInset + r.headerHeight};
    r.footer = {r.padding, h - trailingInset - footer, w - r.padding, h - trailingInset};
    r.body = {r.padding, r.header.bottom + gap, w - r.padding,
              std::max(r.header.bottom + gap + 4, r.footer.top - (footer ? gap : 0))};
    if (in.bottom) {
        r.header = {r.padding, h - trailingInset - r.headerHeight, w - r.padding, h - trailingInset};
        r.footer = {r.padding, r.header.top - gap - footer, w - r.padding, r.header.top - gap};
        r.body = {r.padding, leadingInset, w - r.padding,
                  std::max(leadingInset + 4, (footer ? r.footer.top : r.header.top) - gap)};
    }
    if (in.view == View::Media)
        r.body = {r.padding, Metrics::Space8, w - r.padding, h - Metrics::Space8};
    float content = reference;
    switch (in.view) {
    case View::Controls:
        content = r.narrow ? Metrics::ControlsStackHeight : ReferenceBodyHeight(in.view);
        break;
    case View::Today:
        content = w < Metrics::ColumnsWidth ? 744 + FocusHeight(r.body.Width()) : ReferenceBodyHeight(in.view);
        break;
    case View::Stats:
        content = r.narrow ? StatsLeftHeight(r.body.Width()) + 284 : ReferenceBodyHeight(in.view);
        break;
    case View::Wireless:
        content = Metrics::WirelessBodyHeight;
        break;
    case View::Features:
        content = in.page == 3 ? 512 + static_cast<float>(in.items) * 256 : 520 + static_cast<float>(in.items) * 160;
        if (in.page == 2)
            content += 520 + static_cast<float>(in.active) * 88 + static_cast<float>(in.events) * 122;
        if (in.offlineIntro)
            content += 136;
        break;
    case View::Status:
        content = 304;
        break;
    case View::Apps:
        content = std::max(112.0f, r.body.Height());
        break;
    case View::Palette:
        content = std::max(144.0f, r.body.Height());
        break;
    case View::Clipboard:
        content = std::max(r.body.Height(), r.body.Width() < 320 ? 600.0f : r.body.Width() < 640 ? 420.0f : 272.0f);
        break;
    case View::Module:
        content = in.image ? std::max(r.body.Height(), std::max(24.0f, std::min(96.0f, r.body.Height() - 56)) + 56)
                           : r.body.Height();
        break;
    case View::Media:
        content = Metrics::MediaBodyHeight;
        break;
    default:
        break;
    }
    if (in.preferredBodyHeight > 0)
        content = in.preferredBodyHeight;
    r.contentHeight = std::max(content, r.body.Height());
    // A transient status overlays the viewport; it never consumes the footer or changes the layout.
    float toast = std::min(32.0f, r.body.Height());
    r.status = {r.body.left, r.body.bottom - toast, r.body.right, r.body.bottom};
    float stageWidth = std::min(340.0f, r.body.Width() * .56f);
    float stageHeight = std::min(stageWidth * 9 / 16, std::max(32.0f, r.contentHeight - 56));
    stageWidth = std::min(stageWidth, stageHeight * 16 / 9);
    float galleryTop = r.body.top + 48 + std::max(0.0f, (r.contentHeight - 48 - stageHeight) / 2);
    r.stage = {r.body.CenterX() - stageWidth / 2, galleryTop, r.body.CenterX() + stageWidth / 2,
               galleryTop + stageHeight};
    float cy = (r.stage.top + r.stage.bottom) / 2, half = Metrics::NavigationSize / 2;
    r.previous = {r.body.left, cy - half, r.body.left + Metrics::NavigationSize, cy + half};
    r.next = {r.body.right - Metrics::NavigationSize, cy - half, r.body.right, cy + half};
    return r;
}

inline std::vector<Rect> Split(Rect area, std::initializer_list<float> weights, float gap, bool vertical) {
    std::vector<Rect> out;
    float total = 0;
    for (float weight : weights)
        total += weight;
    if (weights.size() == 0 || total <= 0)
        return out;
    float extent = (vertical ? area.Height() : area.Width()) - gap * static_cast<float>(weights.size() - 1);
    float cursor = vertical ? area.top : area.left;
    for (float weight : weights) {
        float next = cursor + std::max(0.0f, extent) * weight / total;
        bool last = out.size() + 1 == weights.size();
        if (vertical)
            out.push_back({area.left, cursor, area.right, last ? area.bottom : next});
        else
            out.push_back({cursor, area.top, last ? area.right : next, area.bottom});
        cursor = next + gap;
    }
    return out;
}
inline std::vector<Rect> Columns(Rect area, std::initializer_list<float> weights, float gutter = Metrics::Gutter) {
    return Split(area, weights, gutter, false);
}
inline std::vector<Rect> Rows(Rect area, std::initializer_list<float> weights, float gutter = Metrics::Gutter) {
    return Split(area, weights, gutter, true);
}
inline Rect Canvas(const Result &layout, float height = 0) {
    return {layout.body.left, layout.body.top, layout.body.right,
            layout.body.top + (height > 0 ? height : layout.contentHeight)};
}
inline std::vector<Rect> ControlsColumns(const Result &layout) {
    auto area = Canvas(layout, layout.narrow ? Metrics::ControlsStackHeight : ReferenceBodyHeight(View::Controls));
    float media = layout.narrow ? 164.0f : 112.0f;
    return {{area.left, area.top, area.right, area.bottom - media - Metrics::Gutter},
            {area.left, area.bottom - media, area.right, area.bottom}};
}
inline std::vector<Rect> TodayColumns(const Result &layout) {
    if (layout.panel.Width() >= Metrics::ColumnsWidth)
        return Columns(Canvas(layout), {160, 320, 264});
    auto area = Canvas(layout, layout.contentHeight);
    float centerHeight = 176 + FocusHeight(area.Width());
    return {{area.left, area.top, area.right, area.top + 272},
            {area.left, area.top + 284, area.right, area.top + 284 + centerHeight},
            {area.left, area.top + 296 + centerHeight, area.right, area.top + 568 + centerHeight}};
}
inline std::vector<Rect> StatsColumns(const Result &layout) {
    if (!layout.narrow)
        return Columns(Canvas(layout), {2.2f, 1});
    auto area = Canvas(layout, layout.contentHeight);
    float leftHeight = StatsLeftHeight(area.Width());
    return {{area.left, area.top, area.right, area.top + leftHeight},
            {area.left, area.top + leftHeight + 12, area.right, area.top + leftHeight + 284}};
}
inline std::vector<Rect> StatsRingRects(const Result &layout) {
    auto left = StatsColumns(layout)[0];
    auto rows =
        Rows(left, left.Width() < 240 ? std::initializer_list<float>{552, 276} : std::initializer_list<float>{176, 52});
    return left.Width() < 240 ? Rows(rows[0], {1, 1, 1}) : Columns(rows[0], {1, 1, 1});
}
inline std::vector<Rect> StatsMiniRects(const Result &layout) {
    auto left = StatsColumns(layout)[0];
    auto rows =
        Rows(left, left.Width() < 240 ? std::initializer_list<float>{552, 276} : std::initializer_list<float>{176, 52});
    return left.Width() < 240 ? Rows(rows[1], {1, 1, 1}) : Columns(rows[1], {1, 1, 1});
}
inline std::vector<Rect> Cards(const Result &layout, View view) {
    std::vector<Rect> cards;
    auto append = [&](const std::vector<Rect> &next) { cards.insert(cards.end(), next.begin(), next.end()); };
    if (view == View::Controls) {
        auto columns = ControlsColumns(layout);
        auto grid = columns[0];
        grid.bottom -= 44;
        auto rows = layout.narrow ? Rows(grid, {1, 1, 1}) : Rows(grid, {1, 1});
        for (size_t i = 0; i < rows.size(); ++i)
            append(layout.narrow || i == 1 ? Columns(rows[i], {1, 1}) : Columns(rows[i], {1, 1, 1}));
        cards.push_back({columns[0].left, columns[0].bottom - 32, columns[0].right, columns[0].bottom});
        cards.push_back(columns[1]);
    } else if (view == View::Today) {
        auto columns = TodayColumns(layout);
        append(Rows(columns[0], {188, 120}));
        append(Rows(columns[1], {164, columns[1].Height() - 176}));
        cards.push_back(columns[2]);
    } else if (view == View::Stats) {
        append(StatsRingRects(layout));
        append(StatsMiniRects(layout));
        append(Rows(StatsColumns(layout)[1], {124, 104}));
    } else if (view == View::Power) {
        for (auto row : Rows(Canvas(layout), {1, 1}))
            append(Columns(row, {1, 1}));
    }
    return cards;
}
inline bool ValidCards(const Result &layout, View view) {
    auto cards = Cards(layout, view);
    for (size_t i = 0; i < cards.size(); ++i) {
        auto r = cards[i];
        if (r.Width() <= 0 || r.Height() <= 0 || r.left < layout.body.left - .01f ||
            r.right > layout.body.right + .01f || r.top < layout.body.top - .01f ||
            r.bottom > layout.body.top + layout.contentHeight + .01f)
            return false;
        for (size_t j = i + 1; j < cards.size(); ++j)
            if (Intersects(r, cards[j]))
                return false;
    }
    return true;
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
