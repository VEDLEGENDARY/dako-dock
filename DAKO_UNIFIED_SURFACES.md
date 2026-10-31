# DAKO_UNIFIED_SURFACES.md

Addendum to `DAKO_DESIGN_POLISH.md` and `DAKO_UPSCALE_AND_TASKBAR.md`. It fixes three problems the owner raised:

1. Corner radius differs across the whole app.
2. Every dock (taskbar) button opens a different kind of popup, which adds steps and breaks fluid navigation.
3. Popups attached to the dock have odd shapes instead of looking like a blended extension of the dock.

Where this file conflicts with the earlier two (radius ladder, popup, preview, sheet and flyout instructions), this file wins. Everything else in those files still applies.

Evidence below comes from reading code only (`DockShell.cpp`, `DockView.h`, `DockShell.h`, `DesktopIntegration.cpp`, `IslandPaint.cpp`, `IslandLayout.cpp`, `LayoutMetrics.h`, `LayoutEngine.h`, `DockMotion.h`). The reviewed tree is GitHub `main` at `083f8c1`. Verify each finding in the current local tree before changing it. Items marked VERIFY are hypotheses to confirm on a real render.

Ground rules unchanged: no behavior regressions, `/W4 /WX`, 120 columns, no new dependencies, zero idle cost, edge-relative (top and bottom), reduced motion and high contrast intact.

---

## 1. What the code does today

### 1.1 Radius map (problem 1)

Corner radii currently in use, found by reading paint and region code:

| Where | Radius | Source |
|---|---|---|
| Dock pill, floating mode | capsule, `pill.Height()` through `CreateRoundRectRgn`, painted 28 and 27 | `DockShell.cpp` `Rebuild`, `DockView.h` |
| Dock pill, island mode | `options.radius` through `dock::Attached` (default corner 22, shoulder 24) | `DockView.h`, `DockMotion.h` |
| Expanded panel | `theme::Radius` (= `SurfaceRadius` 18) while `Metrics::PanelRadius` says 20 | `LayoutMetrics.h`, `IslandPaint.cpp` |
| Backdrop window region | `theme::Radius * scale * 2` through `CreateRoundRectRgn`, a different shape than the painted outline | `IslandPaint.cpp` `PositionBackdrop` |
| Cards | 14 (Metrics), 16 (Stats, Today, Clipboard, Power uses 14), flattened to 10 by `Fill()` | pages, `Fill` |
| Controls and buttons | 8, 9, 10, 12, 17, 18 literals | pages, `DockView.h` |
| Dock hover and focus | 12 | `DockView.h` |
| Window preview popup | 8 and 10, inside a rectangular `WS_POPUP` with no rounding at all on the window itself | `DockView.h` `PaintPreview`, `DockShell.cpp` |
| Native popups | Windows 11 menu corners (about 8), native tooltips | `HMENU`, `TOOLTIPS_CLASSW` |

Literal counts in `Fill`, `FocusRing` and `RoundedRect` calls: 12 appears 11 times, 10 appears 10 times, 8 and 16 and 14 each about 9 times, plus 9, 17, 18 and 28. There is no single source of truth, and the outer silhouettes (pill, panel, preview, menus) do not share a radius at all.

### 1.2 Popup map (problem 2)

What each dock control does today (`DockShell.cpp` `Invoke`, `AppMenu`, `Overflow`, `ShowPreview`; `DesktopIntegration.cpp` `OpenFromDock`):

| Control | Result | Surface type |
|---|---|---|
| Apps, Search (as Recent) | island Apps tab, or the command palette for Search | island panel, two different layouts |
| Wi-Fi | `Flyout::Wifi`, a 460 wide wireless flyout | different width and layout |
| Volume | Controls tab, then focus moves to the volume slider | tab page |
| Clock, Battery, Weather | Today tab | tab page |
| "More" status | `Flyout::DockStatus` | another flyout |
| Show desktop, Task View | `SendInput` of Win+D and Win+Tab, no Dako surface | none |
| App icon hover | `WS_POPUP` window preview (`PreviewClass`), square window, floating 6 DIP above the bar (`bar.bounds.top - height - 6`) | separate unblended HWND |
| App icon right click | native `HMENU` (`AppMenu`) | native menu |
| Overflow button (no room) | native `HMENU` listing all apps | native menu |
| Dock context | `callbacks.context` | another path |
| Tooltips | native `TOOLTIPS_CLASSW` | native tooltip |

Result: five different surface styles from one strip (island tab, 460 flyout, status flyout, floating thumbnail window, native menu), three widths (800, 460, 344), different open and close behavior, and tab switching that makes some actions two or three steps (open panel, find the right tab, find the control).

`OpenFromDock` also does heavy work on every click: `ReleaseGraphics()`, a `SetWindowPos` jump of the whole envelope window, `RefreshGeometry()`. That recreates the render target and repositions the window each time, which is the source of the "different effect per button" feel and any flicker.

### 1.3 Shape map (problem 3)

1. The panel is built with `dock::Attached` (flared shoulders sized for attaching to a flat screen edge). In dock mode the envelope window is placed so the panel's base lands `4 * scale` pixels inside the pill (`barBounds.top - h + 4 * scale` in `Island::Place`). But the pill's top edge is not flat: it is a capsule or an `Attached` shape with its own rounded corners. The flat flared base of the panel therefore meets a curved pill top. VERIFY with a crop: expect a visible notch, step or hairline where the two shapes overlap.
2. The pill is painted into a different window (`Dako.Native.Dock` surface) than the panel (the island envelope window). Two windows with two regions, two outlines, two backdrops, and independent z-order. They cannot form one continuous silhouette.
3. The window preview is a third window with a rectangular frame, painted with its own style (Segoe UI text, 8 to 10 radii, `theme::Surface` fill), floating with a 6 DIP gap.
4. Widths 800, 460 and 344 are all centered on the dock anchor and clamped to the monitor, so wide and narrow surfaces make different stepped or floating outlines.

---

## 2. One shape language (fix for problem 1)

### 2.1 Single source of truth

Create `src/Shape.h` (header only, no other dependencies except `<cmath>` and `<algorithm>`). Everything that draws, clips or hit-tests a rounded shape reads from it.

```cpp
namespace shape {
// Depth 0: silhouettes that touch the screen or other silhouettes.
constexpr float Surface = 28;   // pill, expanded panel, previews, popovers, sheets, toasts
// Depth 1: cards and groups inside a surface (concentric with Surface, inset 12).
constexpr float Card = 16;
// Depth 2: controls inside cards (inset 4 to 8) and rows.
constexpr float Control = 12;
// Depth 3: tiny things (chips, key caps, thumbnails in lists).
constexpr float Chip = 8;
// Pills and sliders use half their own height.
inline float Pill(float height) { return height * 0.5f; }
// Concentric inner radius for any custom inset.
inline float Inner(float outer, float inset) { return std::max(Chip, outer - inset); }
// Concave blend radius where two surfaces join (section 4). Always equals Surface.
constexpr float Fillet = Surface;
}
```

Why 28: it equals half the current pill height (56), so the dock keeps its exact capsule look without special casing, and every other surface matches it. If the owner prefers a tighter look, change only `Surface` and `Card` here; nothing else may hard-code a value.

Rules:
1. No numeric radius literal in any `Fill`, `FocusRing`, `DrawRoundedRectangle`, `FillRoundedRectangle`, `RoundedRect`, `CreateRoundRectRgn` or `dock::Attached` call. Use a `shape::` token.
2. `Metrics::PanelRadius`, `CardRadius`, `ControlRadius`, `SurfaceRadius`, `SmallRadius` and `theme::Radius` are deleted or become aliases of the tokens above. `options.radius` (the dock "corner radius" setting) is removed from the schema or becomes a global "Roundness" preset (Tight 24, Default 28, Round 32) that changes `shape::Surface` and derived values together, so the whole app moves as one.
3. `Fill()` no longer clamps or special-cases radii and no longer draws implicit outlines. (Carries over from the earlier file, restated because it is the main cause of mixed corners.)
4. Add a lint to `scripts/check-style.ps1`: fail if any source file outside `Shape.h` matches a numeric second argument to the rounded-rect and region functions listed above, or `RoundedRect(.*, \d+(\.\d+)?f?, \d+`. Allow tokens and expressions that reference `shape::`.
5. Add a unit test that every token is positive, ordered `Surface > Card > Control > Chip`, and that `Fillet == Surface`.

### 2.2 Continuous corners

Circular arcs look slightly hard against straight sides. Use continuous (squircle style) corners for every depth-0 silhouette so the whole product reads as one family.

1. Implement `shape::RoundedPath(rect, radius, smoothing = 0.6f)` producing an `ID2D1PathGeometry` using cubic Beziers with the standard continuous-corner control points (smoothing 0 gives a circular arc, 0.6 gives the Apple-like curve; the curve extends 1.6x the radius along each side).
2. Cards and controls use smoothing 0.6 as well. Pills and circles stay true semicircles (smoothing 0).
3. Cache by (rect size, radius, smoothing) in the shared geometry LRU (see the earlier file, section 4.1).
4. Window regions are generated from the same path (section 4.4), never from a separate formula.

### 2.3 Migration order

1. Add `Shape.h`, lint and tests. Do not change visuals in the same commit.
2. Replace literals page by page (Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power, Palette, Features, Connection flyouts, settings app, dock strip, preview). One commit per area.
3. Remove the old constants. The lint then guarantees no regression.

---

## 3. One popup system (fix for problem 2)

### 3.1 Principle

The dock is one object. Everything that "pops up" is the same object growing out of it. There is exactly one expandable surface (the "Dock Surface", the existing island envelope) with one open animation, one close animation, one material, one shape language. Content changes; the container does not.

### 3.2 Interaction matrix

Every dock control maps to exactly one of three behaviors. No other behavior is allowed.

| Behavior | Controls | Result |
|---|---|---|
| Action | Show desktop, Task View, Search key press, Start, any one-shot command | Runs immediately. Press feedback only (scale 0.97, 120 ms). Never opens a surface. |
| Peek | Hover on an app icon, hover on a status icon, hover on the media activity | A compact surface extends from the pill (section 4), same shape language. Dismisses when the pointer leaves the pill and surface. |
| Panel | Click on Apps, Wi-Fi, Volume, Battery, Clock, Weather, "More", app icon (when it has several windows), right click on an app icon | The Dock Surface extends from the pill to the content for that control. |

No control opens a different style of window. The Wi-Fi flyout, Controls tab, Today tab, DockStatus flyout, thumbnail window and native menus all become content inside the Dock Surface.

### 3.3 The Quick Panel (collapse the tabs for system controls)

Currently Wi-Fi, Volume, Battery, Clock and Weather each take the user to different places (Wi-Fi flyout, Controls tab, Today tab). Replace with one deep-linked panel.

1. All five status buttons open the same Quick Panel (the current Controls layout plus a compact Today strip). The clicked button decides which section is focused and scrolled into view, not which page opens:
   - Wi-Fi: Wi-Fi tile is expanded in place (network list inside the tile region, no width change).
   - Volume: volume pill slider focused (keyboard arrows adjust immediately).
   - Battery: battery and power detail row focused.
   - Clock: calendar and focus timer section focused.
   - Weather: weather card focused.
2. Direct manipulation without opening anything: scroll wheel over Volume adjusts volume with a small HUD in the pill (section 4.5 of the earlier file's live activities), scroll over the clock does nothing, middle click on Volume mutes. These are Actions.
3. Maximum depth: dock click to the actual control is one step. Changing Wi-Fi network is two clicks total (click the Wi-Fi icon, click the network). Back is one step (Escape or the Back button). Closing is one step (click outside, Escape, click the same dock button again).
4. Switching between dock buttons while the surface is open does not close and reopen. The surface retargets: content crossfades with a shared-axis slide (8 DIP in the direction of the new button), the anchor slides to the new button x with a `Settle` spring, width and height retarget with the existing spring velocity preserved. No `ReleaseGraphics`, no `SetWindowPos` jump, no new render target.
5. The Apps and Search surfaces use the same container and the same open animation. The palette is a mode of the same surface (input at top, results below) and uses the same width class as Apps.
6. The full tab bar (Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power) remains for the expanded island, reached by clicking the island's own expand chevron or the "Dako" button. It is the "full app" view, not what status buttons open.

### 3.4 Width classes (replace 800 / 460 / 344)

Three classes only, each a multiple of 40 DIP, with identical corner treatment and animation:

| Class | Width | Used for |
|---|---|---|
| Compact | 360 | Peek (preview, media, status tooltip card), Quick Panel on narrow monitors, sheets and toasts |
| Standard | 480 | Quick Panel, Apps, Palette, Wi-Fi and device lists |
| Wide | 800 | Full tabbed island (Today, Stats, Clipboard, Wallpaper) |

The container height follows content (spring), max 80 percent of available height, scrolls inside. The window envelope stays the same size; only the painted surface morphs, so no window resize or move happens on open.

### 3.5 Replace native popups

1. App context menu (`AppMenu`, `HMENU`) becomes a Compact popover inside the surface: window list with titles, Open another instance, Pin or Unpin, Close window, Recent documents (loads in place, no second popup).
2. Overflow menu (`Overflow`) becomes a Compact scrollable list in the surface. Better: the overflow button scrolls the dock horizontally (already the first branch), and the list is only a fallback when no groups are visible.
3. Tooltips (`TOOLTIPS_CLASSW`) become a label chip drawn by the dock surface (28 DIP tall, `shape::Control` radius, same material, 500 ms delay, 100 ms fade, attached 8 DIP above the icon, flips with the edge). Keep UIA names unchanged.
4. Window preview thumbnails: see section 4.3.
5. Anything else that is `TrackPopupMenu`, `MessageBoxW` or `PromptText` in a dock-initiated flow becomes in-surface UI (the earlier file's sheets and popovers, now using the shape language here).

### 3.6 One open and close animation

Single choreography for every surface, from the earlier motion spec, restated as a hard requirement:

1. Open: height leads by about 40 ms, width follows, `Settle` spring (24 rad/s) with 0.9 ratio on height, content fades in after 60 percent of travel with an 8 DIP rise away from the attached edge. Total under 320 ms.
2. Close: content fades first (100 ms), then the shape shrinks into the pill.
3. Retarget: same springs, velocity preserved, content crossfade 160 ms.
4. Reduced motion: snap, no intermediate frames. High contrast: same layout, no shadow, solid border.
5. Nothing may implement its own timing. A single `SurfaceMotion` object owns open, close and retarget, and fixtures and `test-motion.ps1` assert the 320 ms bound for every control in the interaction matrix.

### 3.7 Click and hover behavior rules

1. Hover delay before a Peek: 280 ms on first show, 0 ms when moving between neighbors while one is already showing (instant retarget).
2. Leave grace: 450 ms, cancelled on re-entry, no polling (`OneShotDeadline`, `TrackMouseEvent`).
3. Click on the active button closes the surface. Click on another button retargets.
4. Escape closes one level (Back if in a sub view, otherwise closes). Alt+F4 does not close the dock.
5. Keyboard: Tab from the dock enters the surface; Shift+Tab returns. Focus restores to the dock button on close.
6. Touch and pen: no hover; first tap opens the Panel directly (Peek is skipped).

---

## 4. Blended extension geometry (fix for problem 3)

The surface must read as the dock growing, not a window placed on top of it.

### 4.1 Union silhouette

The pill and the extension are one closed outline. Where they meet, the outline flows through concave fillets of radius `shape::Fillet`, exactly like a tab joining a tab strip.

Canonical space: growth direction is up (bottom edge). Mirror for the top edge with a transform (`D2D1::Matrix3x2F` reflection about the horizontal axis through the envelope), never by duplicating code.

```
        ____________________
       /                    \         extension (width W, height H)
      |                      |
 ____/                        \____   concave fillets (radius Fillet) at the joins
|                                  |
|            dock pill             |   convex corners (radius Surface)
 \________________________________/
```

Algorithm for `shape::Union(pill, extension, edge)`:
1. Inputs: pill rect `P` (x0..x1, y0..y1), extension rect `E` whose bottom edge coincides with the pill top edge (`E.bottom == P.top`, no gap and no overlap hack).
2. Clamp `E.left` and `E.right` so that `E` overlaps the pill horizontally by at least `2 * Fillet` on both sides of the join; otherwise move `E` toward the pill center. This guarantees the fillets fit and never produce slivers.
3. If `E` is narrower than `P` (typical for Compact on a long dock): the outline goes along the pill top, a concave fillet turns up into `E`'s left side, a convex corner at `E` top-left, across, convex top-right, down `E`'s right side, concave fillet back onto the pill top, and on.
4. If `E` is wider than `P` (Wide over a short pill): both sides get a concave fillet turning outward from the pill sides into the extension's lower corners (the extension's bottom corners become the join). The pill is visually absorbed into the base of the extension.
5. If the pill is the same width as `E` (within `2 * Fillet`), snap `E` to the pill and draw straight sides with no fillets.
6. Concave fillets are drawn with `D2D1_ARC_SEGMENT` (sweep opposite to the convex ones). Convex corners use the continuous-corner builder from 2.2. Fillet radius equals the convex radius so the silhouette has one visual rhythm.
7. Output: one `ID2D1PathGeometry`.

### 4.2 Remove the hacks that cause odd shapes

1. Remove the `4 * scale` overlap in `Island::Place` and the 6 DIP gap in `ShowPreview`. Surfaces share an exact edge.
2. Remove `dock::Attached`'s screen-edge shoulders for dock-attached surfaces. `Attached` is only used when there is no dock (the notch mode on the screen edge); there the shoulders flare against the screen edge, which is flat. In dock mode the base is the pill, which uses `Union`.
3. The pill and the extension are painted by the same window (the island envelope) in the same frame, from the same geometry. See 4.3 for the thumbnail exception.
4. Anchoring: the extension is centered on the clicked or hovered button, then clamped to the pill (step 2 above) and to the monitor. A Peek from the leftmost icon therefore hugs the pill's left end rather than floating off it.

### 4.3 Preview thumbnails inside the same silhouette

`DwmRegisterThumbnail` requires a destination `HWND`, and the current preview uses its own window (`Dako.Native.WindowPreview`). Plan:

1. VERIFY whether a DWM thumbnail registered against the island envelope window (the one using DirectComposition with `WS_EX_NOREDIRECTIONBITMAP`) composites correctly above the swap chain. If yes, destroy the preview window and draw the preview card (title, thumbnail slot, buttons) inside the Dock Surface as a Compact Peek. Thumbnail rect is a rounded clip region drawn by DWM (thumbnails cannot be rounded, so inset them 12 DIP and give them a `shape::Chip` frame drawn above with a 1 px hairline; square thumbnail corners hidden by the inset).
2. If no, keep a satellite preview window but make it part of the silhouette: shape its window region from the same `Union` path (the preview is just the extension, painted by its own HWND), position it exactly on the pill top edge with zero gap, use the same material, hairline, shadow and open animation, and hide the pill's own top hairline where the two windows meet (draw the pill's top edge hairline only outside the join range). Keep `WS_EX_NOACTIVATE`.
3. Either way the preview behaves as Peek: hover to show, 0 ms retarget between neighbors, same leave grace, click activates, middle click closes the window, hover on the thumbnail shows a close button.
4. Multi window groups show up to 3 thumbnails in a row (Compact width), more as a scrollable list; never a wider custom size.

### 4.4 One geometry, many consumers

From the single `Union` path, derive everything:
1. Paint: `FillGeometry`, hairline `DrawGeometry`, highlight.
2. Window region: `ID2D1Geometry::Simplify` to polylines (`D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES`, flattening tolerance 0.25 device pixels) into a sink that collects points, then `CreatePolygonRgn`. No separate hand-built polygon (`AttachedPixels`, `NotchPixels`) for dock-attached shapes.
3. Hit testing: `FillContainsPoint` or the same polygon.
4. Backdrop window region and shadow bitmap: same path, so blur and shadow match the outline exactly (also removes the `CreateRoundRectRgn` mismatch from section 1.1).
5. Cache by key (pill rect, extension rect, edge, radii, DPI), LRU of 8.
6. Test: the polygon generated for any (pill, extension) combination must be simple (no self intersection), contain the pill and extension centers, exclude points outside the bounding union, and have all convex arcs equal to `shape::Surface` and all concave arcs equal to `shape::Fillet`.

### 4.5 Width, height and position summary

| Property | Rule |
|---|---|
| Base edge | Exactly the pill top edge (bottom dock) or bottom edge (top dock) |
| Horizontal | Centered on the clicked button, clamped to overlap the pill by at least `2 * Fillet`, and to the monitor with 12 DIP margin |
| Width | One of Compact 360, Standard 480, Wide 800 |
| Height | Content driven, spring, max 80 percent of available height |
| Convex radius | `shape::Surface` |
| Concave radius | `shape::Fillet` |
| Material | Same as the pill: surface fill, hairline, highlight, shared shadow |

---

## 5. Concrete code changes

1. `Island::OpenFromDock` (`DesktopIntegration.cpp`): remove `ReleaseGraphics()` and the `SetWindowPos` jump. Replace with `SurfaceMotion::Retarget(anchorX, contentKind, widthClass)`. Keep the monitor switch path (`panelMonitor != monitor`) as the only case that moves the window, and only when the target monitor actually changes.
2. Replace the `Tab next = ...` mapping with `ContentKind`: `QuickPanel(section)`, `Apps`, `Palette`, `FullIsland(tab)`, `AppContext(group)`, `Preview(group)`, `Status`. Map each `desktop::Dock*` action to one of the three behaviors in 3.2.
3. `Flyout::Wifi`, `Flyout::DockStatus`, `Flyout::Audio` become sections of the Quick Panel. Keep the enum values only as internal section ids if other code depends on them.
4. `DockShell.cpp`: delete `AppMenu` and the `HMENU` paths in `Overflow`; route to in-surface popovers through a new callback (`callbacks.popover(kind, group, monitor)`). Delete `TOOLTIPS_CLASSW` creation and draw label chips. Preview: section 4.3.
5. `DockView.h`: share text and icon formats with the island, use `shape::` tokens, derive pill and hover radii from `Pill()` and `Control`. The pill and the hover highlight (`Control` radius, inside the pill with 4 DIP inset) must be concentric with the pill.
6. `IslandLayout.cpp` `Place()`: stop computing `top` from `barBounds.top - h + 4 * scale`. The envelope window height already ends at the pill top; the panel base is the pill top edge.
7. `IslandPaint.cpp`: `IslandSurface()` paints the `Union` geometry. `DrawNotch` and `PaintNotch` remain only for the no-dock notch mode and use the same color token.
8. `LayoutEngine.h`: replace the three view widths with the three width classes and compute panel heights from content.
9. Settings schema: remove per-surface radius options, add `Roundness` preset if kept, remove anything that exposes separate popup styles.

---

## 6. Verification

1. Fixtures: every control in the interaction matrix opened at 100, 125, 150 and 200 percent DPI, top and bottom edges, Midnight, Paper, high contrast. Overlay checks: no gap between pill and extension, no overlap artifacts, silhouette is one closed outline.
2. Crop tests: zoomed crops of the join area for Compact, Standard and Wide, left, center and right anchors. Assert pixel continuity of the fill color across the join row (no hairline, no color step).
3. Radius audit script: renders each page, samples corner pixels at known rects, asserts radius tokens (a simple distance-to-corner probe).
4. Motion: `test-motion.ps1` asserts open under 320 ms, retarget under 240 ms, zero timers armed at rest, reduced motion produces no intermediate frames.
5. Navigation: scripted tests that each dock control reaches its target control in at most 2 clicks, Escape depth is 1, and clicking the active button closes.
6. No native menu, tooltip window, message box or prompt appears in any dock-initiated flow (assert by enumerating top-level windows during fixtures).

---

## 7. Work plan

| Phase | Work |
|---|---|
| 0 | Diff local tree against `083f8c1`. Confirm sections 1.2 and 1.3 findings (especially the pill-to-panel join). Capture baseline fixtures. |
| 1 | `Shape.h`, tokens, lint, tests. Replace every radius literal and delete old constants. Continuous-corner builder. |
| 2 | `Union` geometry, region, hit test, shadow and backdrop derived from it. Remove `Place()` overlap hack and the preview gap. |
| 3 | `SurfaceMotion` single choreography, retargeting without `ReleaseGraphics` or window jumps. Width classes. |
| 4 | Interaction matrix: Action, Peek, Panel. Quick Panel with deep-linked sections. Remove Wi-Fi, Today and DockStatus special paths from the dock. |
| 5 | Replace `HMENU`, tooltips and the preview window with in-surface content (or satellite per 4.3). |
| 6 | Fixtures, crop tests, radius audit, navigation tests, perf check against previous rest numbers. |

### Definition of done

1. One radius source (`Shape.h`), lint enforced, no literal radii anywhere.
2. Pill, panel, preview, popovers, sheets and toasts share silhouette radius and material.
3. Every dock control is an Action, a Peek or a Panel, and all Panels use the same surface, animation and width classes.
4. Switching between dock buttons retargets in place with no flicker, no window jump, no graphics reset.
5. The pill and any extension form one continuous outline with concave fillets, in all anchor positions and on both edges.
6. No native menus or tooltips in dock flows.
7. Reduced motion and high contrast pass, and idle cost is unchanged.
