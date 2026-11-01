#pragma once
#include "Shape.h"
#include "SurfaceModel.h"

namespace layout {
struct Metrics {
    static constexpr float Space4 = 4, Space8 = 8, Space12 = 12, Space16 = 16, Space20 = 20, Space24 = 24;
    static constexpr float Padding = Space20, Gap = Space12, Section = Space16;
    static constexpr float PanelRadius = shape::Surface, CardRadius = shape::Card, ControlRadius = shape::Control;
    static constexpr float SurfaceRadius = shape::Surface, SmallRadius = shape::Chip;
    static constexpr float PanelInset = Space16, AttachedInset = Space12, FreeInset = Space16;
    static constexpr float Gutter = Space12, CardPad = Space16, TilePad = Space12, RowGap = Space8, MinHit = 32;
    static constexpr float FloatingGap = Space8;
    static constexpr float MainWidth = surface::Wide, WirelessWidth = surface::Standard, MediaWidth = surface::Compact;
    static constexpr float ControlsWidth = surface::Standard, WallpaperWidth = surface::Wide, ControlsNarrowWidth = 480;
    static constexpr float MinWidth = 100, MinHeight = 80, NarrowWidth = 640, ColumnsWidth = 760;
    static constexpr float EmergencyExtent = 240, CompactHeight = 360;
    static constexpr float HeaderHeight = 40, CompactHeaderHeight = 32;
    static constexpr float BodyHeight = 320, WirelessBodyHeight = 372, MediaBodyHeight = 120;
    static constexpr float MediaPanelHeight = 136;
    static constexpr float TodayStackHeight = 840, StatsStackHeight = 556, ControlsStackHeight = 656;
    static constexpr float StatusHeight = 28, CompactStatusHeight = 32;
    static constexpr float FooterHeight = 36, CompactFooterHeight = 32;
    static constexpr float EmergencyAppsHeight = 100, EmergencyWallpaperHeight = 110;
    static constexpr float StageInsetX = 56, StageInsetY = 42, NavigationSize = 40;
    static constexpr float AppRowHeight = 44, AppHeadingHeight = 24;
};
} // namespace layout
