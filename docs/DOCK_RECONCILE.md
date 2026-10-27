# Dock reconciliation audit

The supplied brief is preserved verbatim in [DOCK_REQUIREMENTS.md](DOCK_REQUIREMENTS.md). The current requirement-by-
requirement status is [DOCK_COMPLIANCE.md](DOCK_COMPLIANCE.md); pre-implementation findings below remain historical.

Audit completed before implementation edits, October 25, 2026. The pasted reconciliation request takes precedence over
V3.md's old full-width bar decision. Scope: reconcile the dock, fix affected earlier behavior, and finish or explicitly
resolve the remaining original V3.md 3.3 parity work. The separate V3_1 suite roadmap is not part of this task.

## Inventory

| Location | Responsibility / existing behavior | Required change |
|---|---|---|
| src/BarShell.h | AppBar math, AUMID/path grouping, full-width three-cluster layout and hit rectangles | Rename DockShell; one content-fit pill, 56 DIP inside a 72-DIP invisible strip; rounded hit testing, app overflow |
| src/BarShell.cpp | Per-monitor HWNDs, AppBar lifecycle, D2D HWND painting, input, menus, tooltips, flash, DWM previews, software fixtures | Rename first; preserve lifecycle and services; transparent DirectComposition, pill-only input, finite spring magnification, status overflow |
| src/BarView.h | Opaque full-width background, top line, small center pill, text statuses/app initial fallbacks, preview painting | One paper/ink capsule, outline/shadow, three zones; actual icons/glyph fallbacks, running dots and divider, no edge-to-edge paint |
| src/BarAccessible.h | Stable MSAA children, keyboard focus, provider disconnection | Dock names, moving icon bounds and the same three-zone traversal |
| src/WindowList.h/.cpp | Eligibility, event-driven worker, AUMID/UWP identity, monitor filtering, native activation/close | Preserve; use existing grouping and avoid a second tracker |
| src/DesktopIntegration.h | Start/stop orchestration, dock/island routing, pin persistence, running section | Gate reservation/suppression behind desktop opt-in; anchor upward to clicked dock zone; palette search |
| src/Island.h/.cpp | barMode/layoutMode, settings, context menu, island geometry, fixtures and status routing | dock/notch migration, one-time consent, dock settings, palette registry and matching view routing |
| src/main.cpp | CLI layout overrides, regular/preview/render startup, singleton | dock CLI, safe ordinary startup, consent only in interactive normal runs; keep preview inert |
| src/DesktopShell.h | Recovery guard, Explorer taskbar management, tracked AppBar HWNDs, work-area restoration | Preserve guard-before-suppression and restore paths; dock must not force SPI work area |
| src/CompositionSurface.h, FrameClock.h, DockMotion.h | Existing premultiplied DirectComposition and finite spring machinery | Reuse for transparent dock and pointer-only motion; no idle frame loop |
| src/SettingsStore.h | Versioned settings, merged sections and migration | Preserve; migrate bar to dock without losing unrelated sections/consent/status choices |
| src/AppsView.h | Searchable app list, pins, Running rows; no Start tiles/recommended section | Preserve useful island launcher; dock itself gets icons, never per-window buttons or app labels |
| scripts/build.ps1, test-live.ps1, test-shell.ps1 | Bar source/test references; versioned output | Update renamed paths, use new v3.3 output, retain earlier v3 builds |
| scripts/verify.ps1 and Island::RenderSuite | bar and preview fixtures, DPI/text matrix; 324 existing scenes | Retain regression scenes under dock names; add pill/overflow/reduced-motion/high-contrast cases |
| tests/FoundationTests.cpp, NativeBarTests.cpp, NativeLifecycleTests.cpp | Geometry, grouping, native maximization, recovery, preview/input tests | Rename; update 72-DIP expectation; add real outside-pill click-through, transparent rendering and safe startup coverage |
| README.md, docs/PROJECT.md, docs/VERIFICATION.md, CHANGELOG.md | Full-width bar/product claims; 3.0-3.2 evidence | Describe a dock; preserve historical reports; record measured comparison and limits |
| V3.md | Old bar default, clusters, taskbar terminology, pending 3.3 | Replace conflicting design wording; record completed work and supported/deferred parity decisions |

The identifiers ABM_*, APPBARDATA and Explorer taskbar names remain Windows API terminology; renaming them would be
incorrect. Historical verification files/build folders retain their original names and measurements.

## Already correct and preserved

- A single island with five tabs, palette colors, fixed body, spring motion, native controls, stats and wallpaper.
- AUMID-first pinned/running merging, copied icons, UWP frame handling and per-monitor filtering.
- DWM thumbnail lifetime, flash shell events, native activation/close failure reporting and pin reordering/new instances.
- Fullscreen QUNS/ABN callbacks, Explorer restart registration, guard-owned AppBar cleanup and work-area restoration.
- Opt-in Explorer suppression, inert preview writes, native MSAA, reduced motion/high contrast and bounded fixture IO.

## Conflicts to remove

Opaque edge-to-edge painting, full-width interactive input, wordmarked Apps/Start button, detached center pill, weather
and battery text strips, always-on Show desktop/Task view buttons, and AppBar reservation without desktop consent.
Search currently opens the app list; it must open one command palette shared by dock search and keyboard actions.
The preview controls operate on one grouped window at a time and remain above the dock, rather than becoming dock buttons.

## Remaining original V3 work

The repository is at 3.2, not at the end of V3.md. Recent/frequent application documents can use the public Jump List API.
Third-party taskbar badge/progress reading and unpackaged notification access need honest capability spikes: setters on
ITaskbarList3 are not evidence that an observer API exists. Do not invent data or ship a denied notification listener.
Tray/status access will be a dock overflow popover, with the dock's native status controls retained.

## Verification plan

Run all existing scripts before/after; preserve 3.0-3.2 binaries. Add pure capsule/click-through/DPI/migration tests and
native transparent composition, per-monitor reservation, maximized content, fullscreen/restart and forced-kill tests.
Capture Explorer's taskbar on this host for a visual comparison with a fixture; distinguish native automated probes from
physical browser/mixed-DPI/elevated/game acceptance. Measure the same resting-preview workload for 60 seconds before/after.

## Reconciliation outcome

The current release is 3.3. The full-width painted/interacting bar is replaced by one transparent capsule; the AppBar
is an invisible, desktop-consent-only reservation. The mark/search/app/status zones, finite magnification, app overflow,
status choices, palette and public recent documents share the existing island and native services. Default-off optional
desktop/task-view icons replace their former permanent buttons. Bar preferences migrate without dropping other settings.

Prior controls now journal original Dako-owned settings before writes and restore them through the guard. Explorer and
work areas recover before provider replay. Persistent network connections and pairing open Windows Settings. Palette
and status transitions stop sampling from the previous view. Device sharing/no internal driver workers resolves the
measured DirectComposition CPU increase; final resting CPU is 0.0024% normalized versus 0.0071% before on this host.

504 fixtures, fifty-cycle lifecycle and dock resource checks, native input/reservation/DWM/recovery probes, a targeted
final view-transition run and the capability spike pass. Public badge/progress/custom-task observation is unavailable;
notification access is Allowed but event subscription fails with 0x80070490. Those are recorded decisions, not simulated
features. Exact results, the unavailable Explorer screenshot, 0/9 Win+number registrations and physical acceptance still
needed are in VERIFICATION.md. V3_1.md remains outside this task.
