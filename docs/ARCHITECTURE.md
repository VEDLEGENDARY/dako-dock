# Architecture

The [V4 plan](v4/DAKO_V4_PLAN.md) and its locked decisions govern the current runtime.
The island uses native windows and DirectComposition, with no browser engine or script runtime.

## Runtime boundaries

| Source | Responsibility |
|---|---|
| `main.cpp` | CLI/helper dispatch, single instance, startup and message loop |
| `Island.h`, `Island.cpp` | Shared state, initialization, graphics lifetime |
| `IslandInput/Layout/State/Paint/Accessibility/Fixtures.cpp` | Input, layout, interaction, rendering, MSAA, inert scenes |
| `*Page.cpp`, `IslandBodyScope.h` | Page content and shared body clipping/scrolling |
| `Anchor.h`, `DockMotion.h`, `LayoutMetrics.h`, `LayoutEngine.h` | Edge reflection, common polygon, spacing and solver |
| `IslandState.h` | Pure Dock/Peek/Expanded transition machine |
| `DesktopIntegration.cpp`, `DockShell.*`, `DockView.h` | App grouping, per-monitor strips, AppBar lifecycle |
| `DesktopShell.h`, `SystemEffects.cpp` | Independent Explorer/work-area recovery and managed effects |
| `V4Integration.cpp` | Schema preferences to runtime behavior and committed/live updates |
| `SettingsSchema.h`, `SettingsStore.h`, `SettingsCore.*` | Validated defaults, atomic schema-4 store, shared IPC library |
| `settings/main.cpp` | Independent native settings application |
| `BundledFont.h`, `ThemePalette.h` | Private in-memory Rubik collection, theme/high-contrast tokens |
| `NativeServices.h`, `ControlsService.cpp`, `SpeedTest.h` | Event-driven media, local controls, bounded explicit speed test |
| `ClipboardModule.*`, `ClipboardTransfer.*`, `ClipboardPaste.h` | Optional history, isolated clipboard IO, targeted paste |
| `ModuleRegistry.*` | Optional module lifecycle, consent, capability declarations and dispatch |

Production pages are ordinary translation units. `scripts/sources.ps1` supplies application and lifecycle
source lists. `SettingsCore.cpp` builds into `dako_core.lib` for both executables. Release compilation emits
function/data sections, removes unreferenced code and writes `build/Dako.map` for audits. The unused rail
solver, old view implementation headers and unused island wallpaper-capture member were removed. Explicit
legacy diagnostics/CLI migration remain for compatibility, with historical decisions archived.

## Interaction and idle work

`Anchor` reflects one polygon for top/bottom painting, native regions and hit testing. Flyouts choose their
edge-relative direction and clamp to monitor bounds. A narrow edge affordance remains when auto-hide is on.
The production strip is a floating overlay with an 8 DIP edge gap. It does not register an AppBar or reserve
application work area, including when the user explicitly replaces Explorer's taskbar. The legacy AppBar
tracking/recovery path remains available to the shell tests. Bottom navigation and Menu share the edge row;
top placement mirrors that row. Secondary page commands live in Menu instead of an extra global footer.

The pure state machine receives pointer, media, wheel, click, hotkey and dismiss events. App-icon wheel input
scrolls before expansion routing, so both directions remain reachable. Playing-only Peek defaults on. Launcher-only hover dwell and media
changes debounce; leave grace includes owned previews/flyouts. A finite 16 ms interaction timer exists only
while a deadline is pending. The old 100 ms proximity polling timer is gone. Named spring specifications
drive height first, delay width by 40 ms, then reveal the content. They scale with animation preference
and stop at rest. Reduced motion snaps transitions.

SMTC metadata/playback events stay subscribed while hidden; timeline sampling runs only while media is visible.
Stats samples only while visible. Today owns the visible clock timer; explicit focus sessions own their countdown.
Window lists use WinEvent notifications and request-only workers. Foreground changes reassert topmost and
apply fullscreen/per-executable policies without polling. Higher Windows bands and exclusive games retain
platform limitations. No uiAccess, global keyboard hook or elevated input broker is used.

## Settings and recovery

See [settings](SETTINGS.md) for schema generation, atomic writes, user-scoped ACLs, event waits and local named
pipe previews. Closing/crashing the settings sender rolls uncommitted changes back. Imported settings cannot
grant desktop consent, import module consent or weaken Offline.

Taskbar replacement starts the independent guard before suppressing Explorer. Mode transitions restore the
old session before changing consent-dependent ownership. Managed theme/audio/brightness/radio/wallpaper writes
capture original values first and restore in reverse order. Disconnected hardware can leave a journal under
`%LOCALAPPDATA%\Dako\Recovery` for explicit retry. Fixtures and isolated preview block real system mutation.

## Clipboard and optional capabilities

History is separately consented, bounded and memory-only by default; DPAPI persistence is explicit. Metadata
snapshots omit image bytes and bound text previews. Full transfers run in a one-second helper job with a
64 MiB process commit bound. Explicit paste captures the previous standard Edit/RichEdit HWND, checks identity,
session, user, integrity, password/read-only flags, foreground and focused control, then uses targeted WM_PASTE
with a 300 ms timeout. It sends no global keys. Unsupported targets retain Copy/manual paste. This narrow
implementation supersedes the earlier blanket insertion deferral; automatic snippet expansion remains deferred.

Modules unload resources when disabled. Consent precedes construction; stale action IDs are rejected. The privacy
ledger omits payloads. Public recent documents are supported; third-party Jump List tasks and badge/progress
observation remain unavailable. Notification subscription failed with `0x80070490` on the unpackaged host, so
package identity/MSIX notifications remain later work. V4 ships as an unsigned portable ZIP. Per-app hiding and
redacted diagnostics are the selected section-10 additions; phone sync, capture, plugins and other platforms
remain roadmap ideas. See [privacy](PRIVACY.md) and [verification](VERIFICATION.md).


Slideshow configuration is owned by `SlideshowIntegration.cpp`, with pure transition guards in
`SlideshowPlan.h` and a cancellable, zero-period thread-pool timer in `OneShotDeadline.h`. A separate bounded
catalog/playlist preserves page browsing. Configuration equality avoids rescans and deadline resets on
unrelated saves. Catalog and apply messages carry generations so stale work cannot rearm a new schedule.

## Design tokens, rendering and in-island chrome

`palette::Tokens` owns neutral surfaces, text, edge alpha and danger roles. Paper and Windows high contrast
map the same roles. `ApplyAccent` recalculates readable ink as the accent changes. `LayoutMetrics` owns the
24/16/12/8 corner ladder, spacing and minimum hit size; `LayoutEngine::Solve` owns the panel, scroll canvas,
card columns and footer. Pages consume the solved geometry instead of patching content heights.
Primary pages use the owner-reference content dimensions, with viewport clamping/reflow and spring-driven
resizing anchored at the dock edge. The relocated navigation retains 32 DIP minimum targets.

`type::Acquire` in `UiTypography.h` shares Rubik formats, Fluent/MDL2 icon selection and tabular-number typography between
the island and dock. Rubik is also registered privately in process memory for native EDIT controls; no font
is installed on the machine. `TwoLine`, `ListRow`, `Card` and `EmptyState` keep text and elevation consistent.
The settings window uses the same palette, typography and spring definitions. Its native controls preserve
IDs, check states, accessibility and save/preview semantics while custom buttons paint the shared switch.

`GeometryCache` bounds Bezier surface and ring paths to eight entries, with half-DIP size keys. Window regions
use the matching sampled outline (24 curve segments above 144 DPI). The backdrop uses that exact region.
`AmbientShadow` supplies a click-through companion window from a cached Gaussian nine-slice and layered
bitmap. Capacity grows in buckets to cover the transition target, so resizing reuses the backing store.
The shadow has no GPU presentation queue. `DepthCache` provides short popover/card shadows.
Opaque surfaces retain their shadows; high contrast removes them.
Hidden companions release their window and full DIB, retaining only the small Gaussian source mask.
Device caches and text layouts are also released at collapse. The last retired GPU device flushes deferred
destruction and trims driver scratch buffers, following Microsoft's
[Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush) and
[Trim](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgidevice3-trim) guidance.
Photo brushes clip to rounded shapes;
the catalog retains its original bounded 320x200 decode, below the requested 720px ceiling. Dock icons request
larger source images and retain two WIC-downsampled sizes per key in a bounded LRU.

`dock::motion` names the finite spring responses. Existing FrameClock/scheduler ownership drives reveal,
sliding tabs, per-card stagger, fractional carousel poses, press/hover/focus, task completion and dock feedback.
Scrollbars use a single 800ms deadline followed by a finite fade; no resting polling worker was added.

`IslandChrome.cpp` paints confirmation sheets, menus/submenus and inline editors inside the island. The native
EDIT remains responsible for text, IME, selection and caret. A modal message loop waits for those controls,
consumes background commands, defaults confirmation to Cancel and restores focus. Menu descriptors retain
original command IDs, enabled/checked states and submenus. `NativeChrome.h` routes confirmations from module
helpers to their UI-thread owner. Folder/file pickers stay native. Consent and system-write gates remain at
their original call sites; inert fixtures and isolated preview still refuse system changes.
