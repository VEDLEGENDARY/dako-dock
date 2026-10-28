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
The default strip is an ordinary overlay; AppBar work-area ownership starts only with taskbar consent.

The pure state machine receives pointer, media, wheel, click, hotkey and dismiss events. App-icon wheel input
scrolls before expansion routing, so both directions remain reachable. Playing-only Peek defaults on. Launcher-only hover dwell and media
changes debounce; leave grace includes owned previews/flyouts. A finite 16 ms interaction timer exists only
while a deadline is pending. The old 100 ms proximity polling timer is gone. Shape springs move together at
23 Hz, scaled by animation preference, and stop at rest. Reduced motion snaps transitions.

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
