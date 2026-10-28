#pragma once

namespace layout {
struct Metrics {
    static constexpr float Space4 = 4, Space8 = 8, Space12 = 12, Space16 = 16, Space20 = 20, Space24 = 24;
    static constexpr float Padding = Space20, Gap = Space12, Section = Space16;
    static constexpr float PanelRadius = 20, CardRadius = 14, ControlRadius = 10;
    static constexpr float SurfaceRadius = 18, SmallRadius = 8;
    static constexpr float MainWidth = 800, WirelessWidth = 460, MediaWidth = 344;
    static constexpr float MinWidth = 100, MinHeight = 80, NarrowWidth = 640, ColumnsWidth = 720;
    static constexpr float EmergencyExtent = 240, CompactHeight = 360;
    static constexpr float HeaderHeight = 64, CompactHeaderHeight = 32;
    static constexpr float BodyHeight = 360, WirelessBodyHeight = 372, MediaBodyHeight = 116;
    static constexpr float MediaPanelHeight = 164;
    static constexpr float TodayStackHeight = 1104, StatsStackHeight = 580, ControlsStackHeight = 476;
    static constexpr float StatusHeight = 28, CompactStatusHeight = 32;
    static constexpr float FooterHeight = 44, CompactFooterHeight = 32;
    static constexpr float EmergencyAppsHeight = 100, EmergencyWallpaperHeight = 110;
    static constexpr float StageInsetX = 56, StageInsetY = 42, NavigationSize = 40;
    static constexpr float AppRowHeight = 44, AppHeadingHeight = 24;
};
} // namespace layout
