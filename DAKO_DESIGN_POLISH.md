# DAKO_DESIGN_POLISH.md

## Execution evidence (2026-10-28)

- Phase 0: `scripts/verify.ps1 -RenderDirectory docs/verification/design-baseline` passed:
  1,068 PNGs plus native geometry, identity and MSAA assertions. Baseline files are preserved unchanged.
- G5: `ConfigureAppearance` assigns all surface roles and calls `ApplyAccent` for every accent role.
  The render suite explicitly selects both themes; `v4-Controls.png` confirms Midnight is applied.
- L2: confirmed. The non-emergency status rect is zero height and the baseline wallpaper error note is absent.
- M2: confirmed. The baseline active pill is painted from `TabRect`, never `tabLeft` / `tabRight`.
- M3: confirmed. The baseline carousel uses integer selection, ignores both motion channels, and clips photos square.
- G7: polygon/rounded-backdrop mismatch confirmed in source. Live screenshot inspection was attempted,
  but computer-use app approval timed out; visible aliasing and blur bleed remain unverified at this point.


Design, graphics and animation pass for `VEDLEGENDARY/dako-dock` (V4 line, bottom island, C++20 / Win32 / Direct2D / DirectComposition).

Goal: a super clean, Apple-precise look. One calm dark surface, one accent, consistent geometry, soft depth, and motion that feels physical but never costs idle CPU. No behavior changes.

Audience: Codex, working in the repo on Windows with MSVC. Read sections 0 to 2 first, then work phase by phase (section 12).

---

## 0. Ground rules

1. No behavior changes. Hit IDs, settings keys, AppBar and Explorer recovery code, privacy gates and module consent stay untouched. This is a visual and motion pass only.
2. Keep `/W4 /WX /permissive-` clean and the 120 column limit (`scripts/check-style.ps1`).
3. No new runtime dependencies. No web view, no script runtime, no AI features, no mascot.
4. Idle cost stays at zero. Every new animation rides the existing scheduler (`ArmMotion`, `Transition()`, `FrameClock`). No new always-on timers or threads.
5. Everything is edge-relative. The island is mirrored for the bottom edge (`anchor::ForEdge`, `bodyDirection`). Any vertical offset, rise, slide or shadow direction must flip with the edge. Never hard-code "down".
6. Respect `reducedMotion` and `highContrast`. Reduced motion snaps (as today). High contrast removes shadows, glass, gradients and alpha hairlines and uses the existing solid 2 DIP borders.
7. Visual changes will invalidate fixture PNG baselines. Capture "before" images first (section 12, phase 0) and regenerate "after" images deliberately, one phase per commit.
8. Anything marked VERIFY is a hypothesis from reading code. Confirm on a real render before changing it.

---

## 1. Review findings (what the code actually does today)

Reviewed in depth: `IslandPaint.cpp`, `IslandLayout.cpp`, `LayoutEngine.h`, `LayoutMetrics.h`, `DockMotion.h`, `MotionController.h`, `FrameClock.h`, `ThemePalette.h`, `IslandState.cpp` (transition stepping), `ControlsPage.cpp`, `StatsPage.cpp`, `WallpaperPage.cpp`, `PowerPage.cpp`, `TodayPage.cpp` (layout and draw parts), `DockView.h`, `DockShell.h` (`SolveDock`), type setup in `Island.cpp`.
Skimmed only: Apps, Clipboard, Palette, Features, Module and Connection pages, settings app. Section 11 gives a checklist to bring them up to the same standard.

### 1.1 Surface and graphics

| # | Finding | Where |
|---|---|---|
| G1 | `Fill()` forces radius to 10 for any rect at least 48 tall and 60 wide, so the 14 and 16 radii that Stats, Today and Power pass in are silently flattened to 10. The 1 px outline is only drawn when radius is 10 or more and the rect is 32 tall or more, so small controls have no outline and cards do. Result: inconsistent corners and edges. | `IslandPaint.cpp` `Fill` |
| G2 | Radius literals everywhere: 8, 10, 14, 16, 17, 18, 22 and `Metrics` declares 20, 14, 10, 18, 8. There is no single ladder. | all pages, `LayoutMetrics.h` |
| G3 | Panel and notch outlines are polylines (17 points per Bezier, `sink->AddLine`). Curves facet at large radii or high DPI. Use real Bezier segments. | `DockMotion.h`, `IslandPaint.cpp` `IslandSurface`, `DockView.h` |
| G4 | The notch fill is hard-coded `RGB(13,13,15)` in `PaintNotch` while the drawn notch uses `theme::Surface`. Two sources of truth for the same color. | `IslandPaint.cpp` |
| G5 | `theme::` defaults are the Paper/Signal (light, orange) set in `DockMotion.h`, while the target look is `palette::Midnight` with a wallpaper-derived accent. VERIFY that `ConfigureAppearance()` overwrites every theme field (Surface, Card, CardHover, Inset, Border, Text, Muted, Focus, Accent*, Secondary, Tertiary). | `DockMotion.h`, `Island.cpp` |
| G6 | No depth at all. One opaque Border stroke at 0.65 alpha, no inner highlight, no shadow. Surface and Card differ by about 4 percent luminance in Midnight, so cards barely separate. | `IslandPaint.cpp`, `ThemePalette.h` |
| G7 | The window region is a polygon (`CreatePolygonRgn`) and the DWM backdrop window uses a `CreateRoundRectRgn`. Region edges are not antialiased, and the round rect does not match the attached shape with curved shoulders. VERIFY with a zoomed crop that the visible edge is jagged and that blur shows square where the shoulders curve. | `IslandLayout.cpp` `Place`, `IslandPaint.cpp` `PositionBackdrop` |
| G8 | Path geometries (`ID2D1PathGeometry`) are created and released on every paint in `DrawNotch`, `IslandSurface` and `DockView::Paint`. Wasteful during animation. | same |
| G9 | Wallpaper carousel: card is rounded (14) but the photo is drawn with `PushAxisAlignedClip(rect)`, so the photo corners are square and poke past the rounded card. Side cards are 52 DIP flat strips at 0.6 opacity with no shadow or scale falloff. | `WallpaperPage.cpp` |
| G10 | Rings: `DrawRing` draws up to 120 separate `DrawLine` calls with round caps. Overlapping round caps leave visible seams on translucent colors and cost CPU. The track color (`CardHover`) is nearly invisible on `Card`. | `IslandPaint.cpp` `DrawRing` |
| G11 | Stats "Top apps" bars are one full-width 2 px line per row regardless of value. They show no data. `Sparkline()` exists but `Stats()` never calls it (dead code or missing feature). | `StatsPage.cpp` |

### 1.2 Layout and spacing

| # | Finding | Where |
|---|---|---|
| L1 | `Solve()` computes a full layout and then overwrites header, body, footer and status for every non-emergency view with fixed numbers (height 390, header top 4, body top 56, footer bottom margin 8). Side padding is 20, top is 4, bottom is 8. Margins are not equal. | `LayoutEngine.h` end of `Solve` |
| L2 | In that override `r.status = {padding, h, w - padding, h}` has zero height. In `PanelContent` the status note banner (`Fill(status,...)`), the "DAKO / tab" mono caption and the "SCROLL FOR MORE" hint all draw into a zero-height rect, and `Text()` returns early for empty rects. VERIFY by forcing `note` to a value: user-facing messages (save failures, "Preview: wallpaper..." etc.) are probably invisible. If confirmed this is a real bug, not just polish. | `LayoutEngine.h`, `IslandPaint.cpp` `PanelContent` |
| L3 | Magic numbers inside pages. Controls: tile rows at +0 and +62 (54 tall), volume at +132, media card at +174 (116 tall). Gaps are 8, 16, 8. Body is exactly 290, so the media card ends flush with the body and there is no slack. | `ControlsPage.cpp` |
| L4 | Stats: left column ends at +246, right column at +272, body is 290. Three different bottoms. Task card on Today is 292 tall in a 290 body. | `StatsPage.cpp`, `TodayPage.cpp` `TaskCard` |
| L5 | Power: four 88 DIP tiles in a single row inside a 290 DIP body. About 200 DIP of dead space. This is the "too spread out" tab. Controls is the "too compact" tab. | `PowerPage.cpp` |
| L6 | `Layout()` patches `contentHeight` per view after `Solve` (300, 300, 520 + n*160, ...). Layout rules live in three places. | `IslandLayout.cpp` |
| L7 | Text is placed with hand-tuned rects (`top + 7`, `top + 30`, `bottom - 6`) plus center paragraph alignment. Baselines drift between tiles of different height. | `IslandPaint.cpp` `Button` and pages |

### 1.3 Typography and icons

| # | Finding | Where |
|---|---|---|
| T1 | Type sizes are `{12, 12, 14, 16, 28, 38, 18, 13, 32, 11}`. Caption and Secondary are identical. Only Normal and SemiBold weights are used. Footer uses Consolas, a foreign face next to Rubik. | `Island.cpp` |
| T2 | No tabular figures. Clock, percentages and speeds will jitter horizontally as digits change. | all numeric text |
| T3 | Icon glyph soup: Unicode `☀ ☁ ✓ ○ ▶ ▾ ↗ ⇄ •` mixed with Segoe Fluent Icons codepoints. The dock strip uses Segoe UI and Segoe MDL2 Assets while the island uses Rubik and Fluent. Two type systems. | `TodayPage.cpp`, `Island::Controls`, `DockView.h` |

### 1.4 Motion

| # | Finding | Where |
|---|---|---|
| M1 | Reveal uses critically damped springs at 23 Hz for `dockReveal`, `dockWidth` and `dockHeight` together. Width and height travel in lockstep, so the shape grows on a straight diagonal. There is no overshoot, no stagger, and content is clipped by `PushAxisAlignedClip` while the shape grows instead of fading and scaling in. | `IslandState.cpp`, `IslandPaint.cpp` `Draw` |
| M2 | `tabLeft` and `tabRight` springs exist and are stepped, but `Tabs()` draws the active pill with a plain `Fill` at `TabRect`. VERIFY: no sliding indicator today. | `IslandPaint.cpp` `Tabs` |
| M3 | `Wallpaper()` indexes cards from the integer `selectedWallpaper`. `wallpaperFocus` is a spring that is only `Snap`ped in `WallpaperPage.cpp`. VERIFY: selection pops instead of gliding. `wallpaperParallax` is stepped but unused in the draw path read. | `WallpaperPage.cpp` |
| M4 | Pane switch is a 12 DIP translate plus opacity (`paneBlend`). Good bones, but every element moves as one block. No stagger. | `PanelContent` |
| M5 | Hover is a color mix only. Press is a 1.5 DIP inset. The volume bar has no knob and no press response. Scrubber is a 2 px line with no knob. | `Button`, `Controls`, `MediaCard` |
| M6 | Dock strip: magnification is +8 DIP at most (20 percent) and the running dot sits at y = 60 while a fully magnified icon spans y 8 to 64, so the dot overlaps the icon bottom. Launch "flash" is a pulsing 1.5 px ring. | `DockShell.h`, `DockView.h` |

### 1.5 Native chrome that breaks the aesthetic

| # | Finding | Where |
|---|---|---|
| N1 | Power confirmation uses `MessageBoxW`. | `PowerPage.cpp` |
| N2 | Today options use a native `HMENU` popup. Focus, event, weather and calendar inputs use `PromptText` Win32 dialogs. | `TodayPage.cpp` |
| N3 | The `Menu ▾` footer button and several text labels use raw Unicode arrows. | `PanelContent` |

The folder picker (`IFileOpenDialog`) must stay native. It is the right tool.

---

## 2. Design language

One page of rules. Everything later refers back to this.

### 2.1 Principles

1. Quiet by default. Surface and cards are neutral. Accent appears only on: the active tab, an active tile, filled progress, the primary button, the focus ring, and the running dot of the active app.
2. Depth comes from layering, not decoration. Three elevations, hairline edges, one soft shadow under the island. No gradients on controls, no glows except the focus ring.
3. Everything aligns to a 4 DIP grid. Spacing values are 4, 8, 12, 16, 20, 24, 32.
4. Corners follow one ladder and are concentric where nested.
5. Motion explains cause and effect: things grow out of where you touched them, and settle once.

### 2.2 Color tokens (Midnight, default)

Define these in one `Tokens` block next to `palette::Midnight` and have `theme::` read from it. Alpha-based edges let wallpaper accent and backdrop show through without muddy borders.

| Token | Value | Use |
|---|---|---|
| `Surface` | `#0E0E10` | island background (opaque fallback) |
| `Card` (L1) | `#17171A` | cards, tiles |
| `Raised` (L2) | `#1F1F23` | hovered card, inputs, chips, popover |
| `Inset` | `#0A0A0B` | wells: slider track, search field |
| `HairlineOuter` | white at 8 percent | outer edge of the island and floating popovers |
| `HairlineInner` | white at 6 percent | top inner highlight on cards (1 px, top edge only) |
| `Divider` | white at 7 percent | separators |
| `Text` | `#F5F5F7` | primary |
| `Muted` | `#A1A1A6` | secondary (keep 4.5:1 or better on Card) |
| `Faint` | `#6E6E73` | decorative or disabled only |
| `Accent` | wallpaper derived (existing `palette::Accent`) | see 2.1 |
| `AccentInk` | chosen per accent: near black or white, whichever gives at least 4.5:1 | text on accent |
| `AccentSoft` | accent at 16 percent over Card | active tab pill, selected row |
| `Danger` | `#FF6961` | destructive text and icon on hover only |

Paper (light) mirrors the same roles with inverted luminance; keep its existing values but move edges to black-alpha hairlines (8 and 6 percent).

Fix `ApplyAccent` (called from `Transition`) so that `AccentInk` is recomputed from relative luminance every time the accent changes. Do not assume Ink works on every wallpaper accent.

### 2.3 Radius ladder

| Token | DIP | Use |
|---|---|---|
| `RadiusPanel` | 24 | island body (attached shape outer corners) |
| `RadiusCard` | 16 | cards, media card, carousel center card |
| `RadiusControl` | 12 | tiles, buttons, inputs, tab pill container |
| `RadiusChip` | 8 | small chips, thumbnails in lists |
| `RadiusPill` | height / 2 | sliders, tab pill, toggles, transport buttons |

Remove the clamp-to-10 in `Fill()`. Callers pass a token and get exactly that radius. Update `Metrics::PanelRadius`, `CardRadius`, `ControlRadius`, `SurfaceRadius`, `SmallRadius` to match this table and delete duplicates. Replace every literal radius in the pages with a token.

### 2.4 Elevation

| Level | Fill | Edge | Shadow |
|---|---|---|---|
| 0 Island | `Surface` | `HairlineOuter` 1 px | soft ambient shadow, section 4 |
| 1 Card | `Card` | `HairlineInner` top 1 px only, no full outline | none |
| 2 Raised and popovers | `Raised` | `HairlineOuter` 1 px | short shadow, 0 DIP x, 8 DIP y blur 24, black 35 percent (popovers only) |

Edges rule: cards do not get a full outline. Remove the outline branch from `Fill()` and draw the inner top highlight only for Level 1 and above. This is the single biggest "clean" win.

### 2.5 Spacing

| Token | DIP |
|---|---|
| `PanelInset` | 20 left and right, 16 on the free edge, 12 on the attached edge |
| `Gutter` | 12 between cards |
| `CardPad` | 16 inside cards (12 for tiles under 72 DIP tall) |
| `RowGap` | 8 between list rows |
| `MinHit` | 32 x 32 for any pointer target |

Replace `Metrics::Padding/Gap/Section` usage so every page reads from these.

---

## 3. Typography

Keep Rubik (bundled) for UI and Segoe Fluent Icons for glyphs.

| Role | Size / weight | Notes |
|---|---|---|
| `Display` | 40 / Medium | clock hours and minutes only |
| `Metric` | 28 / Medium | big values (percent, temperature) |
| `Title` | 16 / SemiBold | card titles, tile names |
| `Body` | 14 / Regular | rows, paragraph |
| `Label` | 13 / Medium | buttons, tab labels, chips |
| `Caption` | 12 / Regular | secondary lines (Muted color) |
| `Overline` | 11 / Medium, +4 percent tracking, uppercase | DISK, NETWORK, section labels. Replaces Consolas. |

Rules:
1. Remove Consolas. Footer and overlines use `Overline` in Rubik.
2. Tabular figures: on formats that show changing numbers (Display, Metric, Label for percents, Caption for rates), set the DirectWrite `DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES` typography feature if the bundled Rubik build supports it. If it does not, render digits in fixed-width cells (measure `0` once per format, draw each digit centered in a cell). Clock and percent text must never shift horizontally.
3. Line height: `Title`, `Body`, `Caption` use proportional line spacing 1.25 so two-line tiles align. Use `IDWriteTextFormat::SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, ...)` where supported, otherwise uniform spacing.
4. Stop hand-placing text with `top + 7 / top + 30`. Add a small `TwoLine(rect, title, subtitle)` helper that vertically centers the block as a pair. Tiles, list rows and cards all call it so baselines match across heights.
5. The dock strip text (clock) uses the same `Label` format. Delete the Segoe UI / MDL2 `Resources` formats in `DockView.h` and share the island's formats and icon font selection (Fluent if present, MDL2 fallback).

---

## 4. Surface, edge and shadow (graphics)

### 4.1 Real curves

Replace the polyline sinks with Bezier segments.

1. `dock::Attached` and `dock::Notch` keep returning point arrays for window regions and hit testing (those need polygons). Add parallel builders that emit `D2D1_BEZIER_SEGMENT` data for painting: `AttachedPath(left, top, w, h, radius)` building an `ID2D1PathGeometry` with `AddBezier` and `AddLine`.
2. Keep the 16 segment polygon for regions but raise it to 24 segments per curve at DPI above 144, so the clipped input shape stays within one pixel of the painted curve.
3. Cache geometry. Key: (kind, width, height, radius, edge) quantized to 0.5 DIP. Store in a small LRU (8 entries) on `Island` and on the dock strip. Rebuild only when the key changes. During a size spring this still rebuilds, but not twice per frame or for static frames.

### 4.2 Edge and highlight

1. Island outline: `DrawGeometry` with `HairlineOuter` at 1 px, inset by 0.5 px so it stays crisp on pixel boundaries (multiply by DPI scale, round, divide back).
2. Add a 1 px inner highlight: clip to the island path, draw the same path offset 1 px toward the interior with `HairlineInner` only along the free edge (top for a bottom dock, bottom for a top dock). Skip on high contrast.
3. Cards: no outline. Fill, then a 1 px top highlight line inset by `RadiusCard` on each end (`HairlineInner`). Do it in one helper `Card(rect, level)` that every page calls instead of `Fill(rect, theme::Card, N)`.

### 4.3 Shadow

The main window is clipped to the panel polygon with `SetWindowRgn`, so a shadow cannot be drawn outside it from the main window. Do not remove the region (it also defines input shape).

Use the pattern that already exists for `backdropWindow`: a second `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT` layered window, click through, sized to the final panel rect plus 48 DIP margin.

1. Pre-render once per (DPI, final size, radius): a nine-slice shadow bitmap using `CLSID_D2D1Shadow` or Gaussian blur (blur sigma 20 DIP, y offset 10 DIP away from the attached edge, black 38 percent). Store as a premultiplied bitmap; draw by stretching the center slice.
2. Fade the shadow window with `dockReveal.value` (window alpha through a DirectComposition visual or `UpdateLayeredWindow` alpha). Do not resize it per frame. It sits at the final expanded size, hidden when `dockReveal` is 0, so the morph stays cheap.
3. Hide it for high contrast, fullscreen suppression, opaque material mode and when the monitor is too small to show 48 DIP margins.
4. Match order: shadow window below the backdrop window below the island.
5. VERIFY the backdrop mismatch from G7. Preferred fix: make the backdrop window region the same polygon as the island (`AttachedPixels`) instead of `CreateRoundRectRgn`. If edges still alias against the blur, drop the backdrop on non-Win11 22H2 and use an opaque `Surface` with 2 percent noise-free flat fill. Clean opaque beats broken glass.

### 4.4 Rings and gauges

Replace `DrawRing`:
1. Track: full circle at `Inset` color, stroke 8 DIP for 64 DIP rings (scale thickness at 12.5 percent of diameter, clamp 6 to 10).
2. Value: one `ID2D1PathGeometry` arc (two `ArcSegment`s when over 180 degrees), start at 12 o'clock, stroke style with round caps, single `DrawGeometry`. Rebuild only when the displayed value changes by at least 0.5 percent. A 0 value draws nothing, a value of 100 draws a full ellipse.
3. The displayed value is a spring (the existing `rings` springs at 20 Hz), not the raw sample, so the arc sweeps smoothly when stats update once a second.
4. Color: accent for CPU, and two derived hues for GPU and RAM computed from the accent by rotating hue plus 40 and plus 80 degrees, same lightness and saturation. This keeps all three in the wallpaper family. Remove `theme::Secondary` and `theme::Tertiary` constants (`#687F76`, `#677A8A`) as they are muddy against the accent.
5. Center text uses `Metric`, label below uses `Overline`.

### 4.5 Bars and sparklines

1. Top apps rows: track pill 6 DIP tall (`Inset`), fill pill proportional to value (CPU percent, or working set divided by total RAM), `RadiusPill`. Fill color accent at 70 percent. Animate width with the row spring (`rowPositions` pattern, 24 Hz).
2. Wire `Sparkline()` into the CPU and RAM cards or delete it. Recommended: in each ring card, under the label, a 20 DIP tall sparkline in accent at 60 percent with a 12 percent fill under the line. Use one `ID2D1PathGeometry` per card, rebuilt on sample (once per second), not per frame.

### 4.6 Sliders and scrubber

1. Volume: pill track (`Inset`), fill pill (accent), 20 DIP knob (white, soft 0 2 6 shadow at 30 percent) that appears on hover or drag (opacity spring 30 Hz) and grows 1.1x while pressed. Speaker icon left, percent right as `Label` with tabular digits.
2. Scrubber: 4 DIP track, 4 to 6 DIP growth on hover, knob appears on hover, time labels as `Caption` tabular.
3. Both slide the fill with `volumeMotion` (already stepped) so external volume changes ease rather than jump.

---

## 5. Layout system and per-tab specs

### 5.1 Make `Solve()` the only layout authority

1. Delete the trailing override block in `Solve()` (the `if (!r.emergency && ...) { h = min(390...` part) and fold its intent into the main solver using tokens: panel height 400 DIP for tabbed views, `PanelInset` margins, header 40, gutter 12, footer 36, status 0 for tabbed views (see 5.2).
2. Delete the per-view `contentHeight` patches in `Island::Layout()` that restate numbers (Controls 300, Stats 300). Content height is computed by the page from its own grid and returned through `Result`.
3. Every page asks the solver for column and row rects through two helpers: `layout::Columns(area, weights[], gutter)` and `layout::Rows(area, weights[], gutter)`. Pages never write `b.top + 62` again.
4. Add a debug-only assertion in `layoutOnly` passes that every card rect is inside `Body()` and that no two sibling rects intersect. Fixtures will then catch overlap and covered elements automatically.

Proposed numbers (tune against fixtures, keep on the 4 DIP grid):

```
Panel        800 x 400
Padding      20 sides, 16 free edge, 12 attached edge
Header       40 (segmented tab control, 36 tall, centered)
Gutter       12
Body         800 - 40 = 760 wide, 272 tall
Footer       36 (Menu button left, page actions right)
```

Narrow layouts (under 640) stack columns and scroll as today.

### 5.2 Fix the invisible status line first (L2)

Give tabbed views a real status rect, or move transient notes into a toast. Recommended: a toast pill that slides in from the free edge, 32 DIP tall, `Raised` fill, text in `Label`, auto-dismiss after 4 s with a 200 ms fade, one at a time, never shifts layout. It replaces the `Fill(status, AccentSoft)` banner. Remove the "DAKO / tab" and "CTRL + ALT + SPACE" mono captions entirely (noise). Keep the "SCROLL FOR MORE" hint as a 16 DIP bottom fade gradient on the scroll body when content overflows.

### 5.3 Controls (currently too compact)

Two columns: left 2/3 and right 1/3, both 272 tall.

```
Left column (about 499 wide)
  Tile grid 3 x 2, each 88 tall, gutter 12:  Wi-Fi, Bluetooth, Audio / Night Light, Dark mode, Brightness (or Focus when brightness unsupported)
  Volume pill, 72 tall, full width
Right column (about 249 wide)
  Media card, 272 tall:
    artwork square 120 x 120 (RadiusCard), centered, 16 from top
    title (Title), artist (Caption, Muted), single line, ellipsis
    transport: previous, play/pause (48 DIP accent pill), next
    scrubber with times at the bottom
```

Tile anatomy (88 DIP): icon 24 in a 40 DIP circle at top left (Raised circle, accent circle when active), name `Title`, detail `Caption` Muted, chevron only for tiles that open a flyout. Active tile fills with `AccentSoft` (not full accent) and accent icon circle. Full accent fill is too loud and is what makes the page feel heavy.

When no media is available, the media card shows a quiet empty state: large muted note glyph, "Nothing playing", no controls.

### 5.4 Today (equal columns, shared edges)

Three columns, weights 1.1 : 1.2 : 1.0, all cards share top and bottom lines at 272.

```
Left:    clock card 140 + gutter 12 + weather card 120
Center:  week strip card 124 + gutter 12 + focus card 136
Right:   task card 272
```

1. Replace the `• • •` separator with a 1 px `Divider` between hours and minutes, or place `09:41` on one line with a colon that fades 100 to 40 percent once per second (only while the page is visible, driven by the existing clock timer).
2. Week strip: seven equal cells, selected day is an accent pill (`RadiusPill`) with `AccentInk` number. Today gets a 4 DIP accent dot below the number if it is not selected.
3. Focus ring: 64 DIP ring with the new ring renderer, time in `Metric`, presets as chips (`RadiusChip`) in one row.
4. Task rows: 40 DIP tall, circular checkbox drawn as geometry (not the `○ ✓` glyphs), completion animates a stroke-draw of the check over 160 ms and a strikethrough that grows left to right.
5. Task card height derives from the column solver (272), not 292.
6. Weather: use Fluent weather glyphs (`E706` family) or small vector icons, not `☀ ☁`.

### 5.5 Stats (aligned bottoms)

```
Left (2/3):   3 ring cards 176 tall, gutter 12   |  3 mini cards 84 tall   (176 + 12 + 84 = 272)
Right (1/3):  Top apps card 160  |  Internet speed card 100               (160 + 12 + 100 = 272)
```

1. All bottoms land on 272. No ragged edge.
2. Ring card contents: ring 72 DIP with percent inside, `Overline` label (CPU, GPU, RAM) under it, one `Caption` detail line (model or used / total), sparkline under. Drop the second detail line; show it in an accessibility name only.
3. Mini cards: `Overline` header, `Body` value. Disk shows a 4 DIP usage bar under the value.
4. Top apps: four rows of 28 DIP: name left, value right (tabular), proportional bar under. CPU and RAM toggle is a two-segment control, not `RAM ⇄`.
5. Speed card: last result as `Metric`, button as a full-width `RadiusControl` primary. While testing, the button shows a determinate or indeterminate progress sweep (accent fill moving behind the label), no text change jitter.

### 5.6 Wallpaper (carousel)

```
Row 1 (28 tall):  segmented control [Recent | My folder]  count (Caption)  folder icon button   Current (chip) on the right
Stage:            center card 16:10, 340 x 212, RadiusCard, soft Level 2 shadow
                  side cards on a curve (section 8.5)
Below stage:      page dots (6 DIP) centered, 12 DIP above the footer
Footer:           Apply (primary) right, fit and display chips left
```

1. Clip photos with the card shape, not a rectangle. Draw the bitmap through an `ID2D1BitmapBrush` filling a rounded rectangle (extend mode clamp, brush transform to cover-fit). This gives antialiased rounded photos at any transform.
2. Selected card gets a 2 DIP accent ring inset 3 DIP (gap ring, Apple style), visible only on the center card.
3. Thumbnail memory: check `WallpaperEntry.pixels` dimensions. If they are full resolution, downscale to 720 px on the decode worker thread before `CreateBitmap`. Eighteen full-size bitmaps in GPU memory is wasteful for a 340 px card. Keep one mip (720) and let `HIGH_QUALITY_CUBIC` handle the rest.
4. Loading state: shimmer on the card placeholder (a 30 percent white gradient band sweeping across at 1.2 s, only while loading and visible).
5. Empty state: quiet illustration (rounded rect with a mountain glyph) and one-line text.

### 5.7 Power (too spread out)

2 x 2 grid of large tiles (rows 130 each, gutter 12 = 272). Lock, Sleep, Restart, Shut down.

1. Icon 32 DIP centered, label `Title` below, `Caption` hint (for example "Ctrl + Alt + L" is not needed; use "Lock this PC").
2. Destructive tiles (Restart, Shut down) are identical at rest. On hover the icon and label take `Danger`, the background stays `Raised`. No red fills.
3. Confirmation replaces `MessageBoxW`: an in-island sheet (section 9).

### 5.8 Footer

Left: `Menu` button with a Fluent chevron, no `▾` glyph. Right: page actions. Every footer button is 32 DIP tall `RadiusControl`. Footer shares the 36 DIP row so the body is never resized by page changes.

### 5.9 Tab bar

One segmented control: container `RadiusPill`, 36 DIP tall, `Inset` fill, 2 DIP inner padding, a sliding indicator (`Raised` fill with `HairlineInner` top) that moves between labels using the existing `tabLeft` and `tabRight` springs (M2). Active label `Text`, inactive `Muted`. The indicator stretches slightly while moving (leading edge uses 34 Hz, trailing edge 26 Hz) for a liquid feel, which is exactly what the two springs are for. Do not fill the tab pill with accent.

---

## 6. Iconography

1. One icon system: Segoe Fluent Icons with MDL2 fallback, drawn through the shared icon format. Remove `☀ ☁ ✓ ○ ▶ ▾ ↗ ⇄ •` everywhere. Use codepoints (chevron `E70D` down, `E76C` right, check `E73E`, play `E768`, pause `E769`, weather per condition).
2. Where no glyph fits (app drawer mark, task checkbox, week dot) draw small geometry with the same stroke width rules: 1.5 DIP stroke, round caps, 20 DIP optical box.
3. Icon sizes: 16 (inline), 20 (buttons), 24 (tiles), 32 (large tiles). Nothing else.
4. Icons are `Muted` by default, `Text` on hover, `AccentInk` on accent fills, `Accent` when they represent an active state on a neutral background.
5. Dock app icons: see section 7.

---

## 7. Dock strip

Reviewed in `DockView.h` and `SolveDock`.

1. Icon sharpness: `DrawBitmap` uses default linear sampling on a bitmap created from a 32 px HICON. Request icons at `size * dpiScale * 1.5` through `IShellItemImageFactory::GetImage` (or the existing catalog path with a larger size), cache two sizes per app key (base and magnified), and draw with `D2D1_BITMAP_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC` (requires `ID2D1DeviceContext`; the dock paint already has a render target, query for the device context interface). The `icons.clear()` at 128 entries causes a cache thrash; switch to LRU eviction.
2. Magnification: base 40 DIP, peak 56 DIP (40 percent), cosine falloff with radius 72 DIP instead of the current gaussian with `+8`. Neighbor spacing already derives from sizes, keep it. Icons grow away from the attached edge and stay bottom-anchored to the pill baseline, not centered on `center = 36`.
3. Running indicator: move the dot out of the icon box. Dot is 4 DIP, centered, 4 DIP below the base icon bottom, and it does not move with magnification (it sits in the pill's bottom padding). Active app: 5 DIP wide, accent. Inactive running: 3 DIP, `Muted`. Dot scales in from 0 with a 34 Hz spring when a window appears.
4. Launch feedback: replace the pulsing ring with a bounce. Icon translates 6 DIP away from the edge twice using the underdamped `Feedback` spring (z 0.65), 2 hops, no ring. Respect reduced motion (no bounce, a single 120 ms opacity pulse).
5. Hover and active backgrounds: `Raised` rounded square (`RadiusControl`) at 60 percent for hover, `AccentSoft` for active. Remove the hard 2 px focus rectangle on hover. Keyboard focus keeps the ring.
6. Pill: apply the same material as the island (section 4): hairline, inner highlight, shadow window. Height 56 stays, but move all magic y values (24, 48, 60, 72, 64) into `SolveDock` output (`geometry.baseline`, `geometry.dividerTop`, `geometry.dotY`) so paint code reads layout, not literals.
7. Dividers: 1 px `Divider`, 20 DIP tall, centered.
8. The app drawer mark drawn by hand (`FillRoundedRectangle` plus ellipse plus line) becomes a 2 x 2 rounded square glyph (four 7 DIP squares, 3 DIP gap) in `Text`.
9. Reorder and add feedback: dragged icon lifts to 1.15x with a Level 2 shadow, neighbors slide with `rowPositions` style springs at 24 Hz. New icon fades and scales from 0.6 to 1.

---

## 8. Motion system

### 8.1 Tokens

Replace the scattered frequency numbers (20, 23, 24, 26, 30, 34) with named springs. Extend `dock::Spring::Step(seconds, frequency, ratio = 1.0f)` to support ratio under 1 by reusing the closed form already in `dock::Feedback::Step`, then define:

| Name | Frequency (rad/s) | Damping ratio | Use |
|---|---|---|---|
| `Settle` | 24 | 1.0 | size and position of the island, pane blend |
| `Snappy` | 34 | 0.92 | hover, press, tab indicator leading edge, toggles |
| `Soft` | 18 | 1.0 | rings, bars, numbers, carousel glide |
| `Lively` | 26 | 0.65 | bounce, knob grow, check pop (underdamped) |

All still multiply by `options.Int(L"Animation") / 100.0f`, as the reveal does today. Convert frequency from a response time with `omega = 2 * pi / response` if you prefer to think in seconds (Settle is about 260 ms to rest).

Duration guardrails (verify in `test-motion.ps1`): reveal settles within 320 ms, tab change within 240 ms, hover within 140 ms, press within 100 ms.

### 8.2 Reveal choreography (M1)

Currently width, height and reveal all share one spring. Split them:

1. Height leads, width follows with a 40 ms phase offset: give `dockWidth` the `Settle` spring at 0.92x frequency and `dockHeight` at 1.0x. The shape rises first, then widens, so it reads as a notch swelling, not a box scaling.
2. Shape only until 60 percent: content is not drawn (not just clipped) until `dockReveal.value > 0.6`. Then it fades and rises 8 DIP (away from the attached edge) over the remaining travel, using `contentBlend` (which exists). This removes the "text sliced by the growing clip" look.
3. Collapse mirrors: content fades out first (120 ms), then the shape shrinks.
4. Overshoot: give `dockHeight` ratio 0.9 so it overshoots by about 1 percent. It should read as alive, not bouncy. Clamp to the monitor envelope so it never exceeds the window.
5. Shadow window opacity follows `dockReveal`.

### 8.3 Pane and tab switch

1. Sliding tab indicator (5.9).
2. Body crossfade stays (`paneBlend`, 12 DIP direction aware translate), but add a per-card stagger: card `i` starts `i * 28 ms` after the switch, rises 8 DIP away from the free edge, fades from 0 to 1 over 220 ms. Implementation: a `paneClock` that counts seconds since the switch (only advances while `paneBlend < 1`), and `Reveal(i)` returns a clamped 0 to 1. Hit rects always use the final geometry. Only paint is offset. Cap total stagger at 150 ms so large pages do not feel slow.
3. Direction: moving to a tab on the right slides content in from the right (the existing `bodyDirection`), left from the left.

### 8.4 Micro-interactions

| Element | Behavior |
|---|---|
| Tile, button hover | background `Card` to `Raised` (`Snappy`), icon brightens `Muted` to `Text` |
| Press | scale to 0.97 about the center with `Snappy`; on release spring back with `Lively` once (replaces the 1.5 DIP inset) |
| Toggle (Dark mode, Night Light) | knob slides on `Snappy`, track color mixes, 1 percent overshoot |
| Slider knob | fades in on hover (120 ms), grows to 1.1x while dragging |
| Numbers (clock excluded) | displayed number springs to the sampled value (`Soft`); show rounded integer, tabular figures |
| Ring and bars | sweep from the previous value, first appearance sweeps from 0 over 500 ms (once per page show, not every sample) |
| Focus ring | fades in over 100 ms, never pops |
| Toast | slides 8 DIP from the free edge and fades, 200 ms in, 200 ms out |
| Task complete | check strokes in (160 ms), text strikes through left to right, row fades to 60 percent |
| Loading | shimmer band on placeholders, 1.2 s loop, only while visible and loading |

### 8.5 Wallpaper carousel physics (M3)

1. Render from the fractional `wallpaperFocus.value`. Selection change sets only the goal (`Settle`, ratio 0.9). Cards derive pose from `delta = index - focus`.
2. Pose function (smooth, no branching on integers):

```cpp
struct CardPose { float x, scale, opacity, skew, z; };
inline CardPose Pose(float delta, float centerWidth) {
    float a = std::min(std::abs(delta), 2.5f), s = delta < 0 ? -1.0f : 1.0f;
    float ease = 1 - std::exp(-1.8f * a);             // fast near center, flattens far out
    return {s * (centerWidth * 0.62f * ease + 34.0f * std::max(0.0f, a - 1)),
            1.0f - 0.16f * std::min(a, 2.0f),
            std::clamp(1.0f - 0.34f * a, 0.0f, 1.0f),
            -0.158f * std::clamp(delta, -1.0f, 1.0f), // existing skew angle
            -a};
}
```

3. Draw from outermost to center (descending `a`). Hit rects use the pose rect.
4. Side cards are full height scaled, not 52 DIP strips, and overlap behind the center card. Opacity falloff, no hard 0.6.
5. Parallax: shift the photo inside the card by up to 4 DIP opposite the pointer using `wallpaperParallax` (already stepped). Reduced motion disables it.
6. Wheel or arrow input moves `goal` by one. Quick repeats retarget the spring, velocity preserved (springs already do this).

### 8.6 Reduced motion and high contrast

Reduced motion: snap everything (as now). Optional, if tests allow: replace movement with a 100 ms opacity-only crossfade for pane changes and toast. High contrast: no stagger, no shadows, no shimmer, no highlight, solid borders as today.

### 8.7 Cost control

1. `ArmMotion()` only when something is moving. Stagger and shimmer must register as motion only while active.
2. Cache every path geometry (section 4.1). No per-frame `CreatePathGeometry` while idle or settled.
3. Shimmer and clock colon pulse run only when the page is visible and the island is expanded.
4. Keep `FrameClock` handshake (one frame requested after the previous is presented). No frame queue growth.
5. Per-frame budget target: paint under 2 ms at 60 Hz on integrated GPU for the busiest page (Stats with sparklines). `diagnosticsPath` already records paint times, use it.

---

## 9. Replace native chrome with in-island UI

1. Power confirm: a sheet that slides over the body from the free edge (height 160 DIP, `Raised`, `RadiusCard` top corners, Level 2 shadow) with a scrim (black 40 percent over the body only). Title `Title`, line of `Caption`, two buttons (Cancel secondary, confirm primary with `Danger` label only for Shut down and Restart). Default focus on Cancel, Escape cancels, Enter confirms the focused button. This replaces `MessageBoxW` and keeps `MB_DEFBUTTON2` behavior.
2. Today options: replace `HMENU` with a popover list anchored to the options button (`Raised`, `RadiusControl`, 36 DIP rows, hover `AccentSoft`, Level 2 shadow, opens away from the edge). Same commands, same IDs.
3. `PromptText` dialogs (focus duration, weather city, calendar URL, local event): inline editors. A `Raised` input card that expands within the page with the existing native EDIT child positioned through `PositionInputs`, plus Save and Cancel. Keep the privacy copy from the current prompts (Open-Meteo disclosure, HTTPS-only ICS note) as `Caption` text inside the card.
4. Keep the folder picker native.
5. Accessibility: sheets and popovers expose their controls through the existing accessibility tree (`IslandAccessibility.cpp`), take focus on open, and restore focus on close. Do not break the `LIVEREGIONCHANGED` notification path in `RebuildLayout`.

---

## 10. Performance and quality guardrails

1. Geometry cache (4.1) and one-time shadow bitmap (4.3) are required, not optional.
2. Thumbnails downscaled (5.6). Dock icons cached at two sizes with LRU (section 7).
3. No `std::wstring` allocation churn in animated number paths. Format into a stack buffer (`swprintf_s`) only when the rounded value changes.
4. `scripts/test-motion.ps1` gets new assertions: reveal settles under 320 ms, no spring still moving after 600 ms, `ArmMotion` is idle at rest (existing idle checks must keep passing), reduced motion produces zero intermediate frames.
5. 60-second warm rest CPU and memory in `docs/VERIFICATION.md` must not regress (currently about 0.017 percent normalized CPU, 28 MiB private).
6. Pixel snapping: all 1 px strokes are positioned on `x.5` device pixels at the current DPI. Verify at 100, 125, 150, 200 percent.

---

## 11. Checklist for pages not reviewed in depth

Apply to Apps, Clipboard, Palette, Features, Module, Connection (Wi-Fi and Bluetooth flyouts), the Output devices flyout, and the standalone settings app (`src/settings/main.cpp`).

1. Replace every literal radius, color, spacing and font size with tokens from sections 2 and 3.
2. Every list row is 40 or 44 DIP, with `TwoLine` text alignment, hover `Raised`, selected `AccentSoft`, no outlines.
3. Every card goes through `Card(rect, level)`. No page draws its own borders.
4. Inputs: `Inset` well, `RadiusControl`, 40 DIP tall, focus ring in accent, cue text in `Muted`.
5. Search fields: Fluent search glyph left, clear button right only when text exists.
6. Section labels use `Overline`. No bold paragraphs.
7. Scrollable bodies get a 16 DIP edge fade (top and bottom) when content overflows, thin overlay scrollbar (3 DIP, fades after 800 ms idle).
8. Empty states: one muted glyph, one `Body` line, one `Caption` hint. No raw sentences in the middle of a card.
9. The settings app uses the same tokens, same type scale and the same switch control (28 x 16 DIP pill, 12 DIP knob), with the same hover and press behavior. A sidebar of 200 DIP, content max width 560 DIP, section gap 24.
10. Overlap and clipping audit: for each page at 100, 150 and 200 percent text scale and at the narrow (under 640) and emergency layouts, no card intersects a sibling, no label is clipped without an ellipsis, no control is covered by the footer, and every hit target is at least 32 x 32.

---

## 12. Work plan (one commit per phase)

Each phase ends with: `.\scripts\build.ps1` clean at `/W4 /WX`, `.\scripts\check-style.ps1`, `.\scripts\verify.ps1`, `.\scripts\test-motion.ps1`. Regenerate fixtures at the end of the phase and attach before and after crops to the commit message.

| Phase | Work | Sections |
|---|---|---|
| 0 | Capture baselines. Run the fixture render once, copy the output to `docs/verification/design-baseline/`. Confirm VERIFY items G5, G7, L2, M2, M3 with crops or a forced `note`. Write results at the top of this file. | 0, 1 |
| 1 | Tokens: color, radius ladder, elevation, spacing in one header. Remove the `Fill()` radius clamp and outline branch. Add `Card()` helper. Fix hard-coded notch color. Recompute `AccentInk` per accent. | 2 |
| 2 | Typography: new roles, remove Consolas, tabular figures, `TwoLine`, share formats with dock strip. Icon cleanup (no Unicode symbols). | 3, 6 |
| 3 | Graphics: Bezier surface, geometry cache, new ring, bars, sparklines, sliders, shadow window, backdrop shape match. | 4 |
| 4 | Layout: `Solve()` as single authority, `Columns` and `Rows`, remove override and patched content heights, fix status line and add toast, rebuild Controls, Today, Stats, Power, Wallpaper layouts. | 5 |
| 5 | Motion: spring tokens with damping ratio, reveal choreography, tab indicator, stagger, micro-interactions, carousel physics. | 8 |
| 6 | Dock strip: icon quality, magnification, running dot, launch bounce, pill material, layout-driven y values. | 7 |
| 7 | Native chrome replacement: Power sheet, Today popover, inline prompts. | 9 |
| 8 | Remaining pages per checklist, settings app, high contrast and reduced motion audit, DPI sweep, docs (`docs/ARCHITECTURE.md` section on tokens, `docs/VERIFICATION.md` evidence). | 10, 11 |

### Definition of done

1. No page contains a literal color, radius or font size.
2. All tabs share the same top, bottom and side edges. Every column in a tab ends on the same line.
3. Zero visible jaggies on the island edge at 100 and 150 percent DPI, with and without backdrop.
4. Status messages are visible (L2 closed).
5. Reveal, tab change, hover, press, carousel and rings all animate with the named springs. Nothing animates at idle.
6. No native message box, popup menu or text prompt in the normal flow (folder picker excepted).
7. Reduced motion and high contrast behave exactly as before, plus pass the new tests.
8. Idle CPU and memory match or beat the existing verification numbers.
