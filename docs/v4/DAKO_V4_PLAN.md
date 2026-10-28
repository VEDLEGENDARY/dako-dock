# Dako V4 — bottom island, settings app, cleanup

Written against `VEDLEGENDARY/dako-dock` @ `0ea47ab` (3.12 development). Audience: you, or an AI coding agent
working in your repo on Windows with MSVC. Read sections 0–2 first; section 9 lists the questions I need answered.

## 0. Why this is a plan and not a zip

Dako is ~100 C++20/Win32/Direct2D/DirectComposition files (`Island.cpp` alone is 233 KB, 81 methods) built with
`/W4 /WX`. I have no MSVC or Windows here, so I cannot compile, run, or look at anything I change. A zip of
unbuilt edits to a codebase this size would most likely fail the first build and could leave the AppBar / Explorer
recovery code in a bad state. This document is written so each step can be implemented and checked on your machine.

## 0.5 Decisions (locked from your answers on 2026-10-27) — these override anything below that disagrees

1. **Mirror = flip.** The top-notch behavior is reflected for the bottom edge: shape, growth direction, flyouts, springs,
   hot zone. Implemented and tested in `Anchor.h` (see the foundation zip).
2. **One bottom object that shows apps.** At rest the island is a slim bottom pill holding **pinned + running app icons**
   (merged by AppUserModelID, running dot, drag to reorder). It is the notch-shaped dock: flared shoulders, content-fit width.
   Hover with media playing → **Peek** (media card grows out of the pill). Hover with no media → **Expanded**.
   Scroll toward the screen centre (wheel-up) or click or hotkey → Expanded. This replaces the separate top notch and the
   separate floating dock; keep the AppBar reservation only if the user enables "Replace taskbar".
3. **No mascot.** Drop the water-drop idle look. (Screenshot 1 is ignored.)
4. **Settings: curated, not exhaustive.** Ship the ~40 options people actually change (section 8, trimmed list below),
   everything else under an "Advanced" page.
5. **Network features default to Offline.** Weather and the speed test are opt-in; speed test uses Cloudflare's public
   endpoint (no account, no key); weather stays Open-Meteo. The existing Offline switch still gates all of it.
6. **Signing / platform.** Target Windows 11 (10 best-effort). Do **not** depend on a signing certificate: overlay works over
   borderless fullscreen and browsers without it. `uiAccess` (above Start/Task View) is an optional later milestone.
   Distribution: portable zip first; installer later.

Trimmed settings list (v1): edge (top/bottom), monitor (primary/cursor/each), scale, corner radius, idle opacity,
auto-hide, icon size, magnification, labels on/off, running dots, theme (Midnight/Paper/System), accent colour, animation speed,
reduced motion, hover delay, leave delay, scroll direction, hotkey, fullscreen policy (Always show / Hide over games /
Hide over fullscreen), per-app hide list, tab order/visibility, media sources, clipboard size/retention/ignore-apps,
wallpaper folders/slideshow, weather city, speed-test opt-in, Offline, start with Windows, replace taskbar, reset/import/export.

## 0.6 Review refinements — 2026-10-28

The owner's later code review refines the locked interaction: only the Dako launcher starts hover dwell;
wheel input over app icons scrolls either direction, while the launcher retains edge-relative expansion.
Playing-only Peek defaults on, with a setting to include paused sessions. Slideshow uses a separate playlist
and completion-driven one-shot deadlines; unrelated settings must not reset it. The build remains
4.0.0-rc.1 pending acceptance, with reproducible text evidence checked in under docs/verification.

## 1. What the code already has (so we don't rebuild it)

| Your ask | What exists today | Where |
|---|---|---|
| Notch shape from the screenshots | Flared-shoulder polygon, already y-flipped inside the function | `DockMotion.h` `dock::Notch`, `dock::Attached`; `DesktopShell.h` ~L318 builds the window region; `Island.h` `PaintNotch/DrawNotch` |
| Hover only shows media | A `View::Media` mini-panel (344×116 DIP) and `MediaCard(compact)` already exist | `LayoutEngine.h` L17/26/33/36; `ControlsView.h` `MediaCard` |
| Scroll expands | Wheel-down on the collapsed notch calls `SetExpanded(true)` | `Island.cpp` `WM_MOUSEWHEEL` (~L5203) |
| Always-on-top | Window is `WS_EX_TOPMOST`, hot zone is `HWND_TOPMOST` | `main.cpp` L253; `NotchHotZone.h` `Place` |
| Media (SMTC) | `MediaService` on its own MTA worker | `NativeServices.h` |
| Controls/Today/Stats/Apps/Wallpaper/Clipboard/Power | All implemented as views | `*View.h` |
| Settings persistence | Versioned `settings.ini` with atomic replace, schema 3, export/import, per-monitor/app overrides | `SettingsStore.h`, `docs/MODULES.md` |
| Module system | Registry with capabilities, presets, consent | `ModuleRegistry.*`, `FeaturesView.h` |

So the real work is **re-anchoring to the bottom, a hover/scroll state machine, visual parity with your
screenshots, a standalone settings app, and cleanup** — not a rewrite.

### Things in the repo that fight your goals

1. **Fullscreen is deliberately hidden.** `HideForNotificationState` (`DockShell.h` L39), `DockShell.cpp` ~L254/L652,
   `Island.cpp` L503 (`fullscreenContext`) all hide the dock for fullscreen/D3D/presentation. Your "overlay everything"
   requirement reverses this default.
2. **The screenshots are a different product shape than the docs.** Your images show 7 tabs
   (Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power); `V3.md` removed Clipboard and Power from the tab bar
   and `PrimaryTabs` is 5 tabs. The screenshots are a dark surface with a gold accent; the code is paper/ink/orange
   (`theme::Paper/Ink/Signal`). The docs and code assume the opposite of what you want.
3. **Polling remains.** `ProximityTimer` (100 ms coalesced, `Island.cpp` ~L994, 4734, 4805) drives hover detection.
   Fine for now, but the new state machine should use mouse events + a short-lived timer (section 4).
4. **Docs sprawl.** 16 files in `docs/` plus `V3.md`, `V3_1.md`, `CHANGELOG.md`, with overlapping "decision"
   records. Several are historical agent-brief text ("Read this whole message before editing") rather than docs.
5. **`Island.cpp` is a god-file** (233 KB; owns every view, input, layout, animation). Most "logic in usage" and
   "spacing" bugs live where layout constants and state are interleaved with painting.
6. **Test surface is large** (22 test files, 18 scripts, "1,052 fixture PNGs"). Any visual change to match your
   screenshots will invalidate the fixture baselines — budget for regenerating them deliberately, not fighting them.

## 2. Target spec, read from your 8 screenshots

State machine (mirrored to the bottom of the screen in V4):

| # | State | Screenshot | Content |
|---|---|---|---|
| S0 | **Idle (Dock)** | 2 | Slim notch-shaped pill on the **bottom** edge holding pinned + running app icons (decision 2) |
| S0b | **Idle, hover, nothing playing** | — | Goes straight to the full island (rule 3) |
| S1 | ~~Mascot~~ | 1 | Dropped (decision 3) |
| S2 | **Peek: media playing, hover** | 3 | Title, artist, scrubber (0:49 / 62:15), prev / play-pause / next. ~340×130, album art left |
| S3 | **Expanded** (scroll, click or hotkey) | 4–8 | Tab bar + page. Width ≈ 580–820, height ≈ 300–390 |

The screenshots show the top edge; V4 renders the mirror image at the bottom.

Pages to match exactly (all dark `#0B0B0C` surface, `#161617` cards, gold accent ≈ `#D4B35C`, pill-shaped active tab,
Rubik-style rounded sans):

- **Controls (4):** tiles — Wi-Fi (Home 5G ▾), Bluetooth (Off ▾), Audio (Speakers ▾), Night Light (Off ▾), Dark mode (On);
  full-width volume slider with % label (28%); large media card with art, title, artist, prev/play/next and scrubber.
  Active tiles are gold-filled; inactive are dark.
- **Today (7):** left column large `09 / 41` clock + `AM · Tue 29` + weather (`32°C Mostly clear`); centre "This week"
  strip (Mon–Sun with selected day gold) with "Next · 11:00 AM" event chip, and a focus timer card (ring `05:00`,
  h/min fields, 1m 5m 10m 15m 20m presets, Start / Reset); right "Today" to-do card with `1/1` ring and "Add a to-do…".
- **Stats (8):** three ring gauges (CPU 37% with model/clock/process count, GPU 12% with model/VRAM, RAM 61% 19.4/31.9 GB),
  Disk C:, Network ↓/↑, Up for; right column "Top apps · RAM" with CPU⇄RAM toggle and bars; "Internet speed" card with
  last result + "Run speed test" button.
- **Clipboard (6):** search field with All / Text / Images / Links filter chips + settings gear; Pinned / Today / Yesterday
  groups with kind icons and relative times; right preview pane with Type / Size / Copied metadata; keyboard legend
  (Enter paste, Ctrl C copy, Ctrl P pin, Del remove, ↑↓ move, Ctrl/Shift multi-select, Tab filter).
- **Wallpaper (5):** Recent / My folder chips, "5 pictures" count, folder button, skewed 3D carousel with the centre card large.
- **Apps, Power:** not in the screenshots — keep current behavior, restyle to match.

**Anything in these screenshots that conflicts with the repo's rules needs your call (section 9):** the weather card
and speed test send network requests; the clipboard page and "Run speed test" contradict current privacy defaults
(history memory-only, no speed-test code exists); GPU model/VRAM needs DXGI (already linked, `dxgi.lib`).

## 3. Workstream A — Cleanup first (do this before any visual work)

Goal: fewer files, one place per concern, so the later steps don't compound the mess.

1. **Delete dead and duplicate code.** Start from the audit already in `docs/DOCK_RECONCILE.md` and `V3.md` "Findings".
   Concrete checks: `rail_/RailProcedure/RailCallback` remnants in `DesktopShell.h`; legacy fixture-only Clipboard/Power
   scenes ("legacy fixture-only scenes remain to preserve render assertions" — `docs/PROJECT.md`); the
   `WallpaperReveal` capture path (only used by `--effects-test`); the `bar` migration shims once settings v4 lands;
   `layout::Rail` if the dock app zone doesn't use it. Use `/Gy /Gw /OPT:REF` map output plus grep to confirm unreferenced symbols.
2. **Split `Island.cpp` by responsibility**, no behavior change, one commit per split, build + `verify.ps1` after each:
   `IslandInput.cpp` (WndProc, keys, wheel, hit-testing), `IslandLayout.cpp`, `IslandPaint.cpp`, one `*Page.cpp` per tab,
   `IslandState.cpp` (the new state machine). Add the new files to the `cl.exe` list in `scripts/build.ps1`
   (it lists sources by hand — switch to a glob so splits don't silently drop files).
3. **One layout/spacing source of truth.** Replace scattered literals (`LayoutEngine.h` has 344, 116, 164, 372, 360, 476, 1104…
   inline) with a `Metrics` struct (spacing scale 4/8/12/16/20/24, radii, tile heights) and have every page take its rects
   from the solver. Most "spacing" bugs come from pages computing their own offsets.
4. **Consolidate docs** into: `README.md` (what/build/run), `docs/ARCHITECTURE.md`, `docs/SETTINGS.md`,
   `docs/VERIFICATION.md`, `docs/PRIVACY.md`, `CHANGELOG.md`. Move `V3*.md`, `*_DECISION.md`, `DOCK_*.md` into
   `docs/archive/`. Remove the agent-brief prose.
5. **Gate:** zero behavior change, `build.ps1` clean at `/W4 /WX`, `verify.ps1` and `test-motion.ps1` pass.

## 4. Workstream B — Bottom placement and mirroring

Mirror rule: every geometric/animation decision that references "top" or "down" becomes edge-relative.

1. Introduce `enum class Edge { Top, Bottom }` in `DockMotion.h` and a single `Anchor` helper that returns
   (origin, growth direction ±1, flip matrix). **No other file may compare against `top`/`bounds.top` for notch logic.**
2. `dock::Notch` already flips y — verify its output and drive it from `Edge`. `dock::Attached` (flared shoulders around the
   expanded panel) is top-anchored (`curve(0,{left-shoulder, top}…)`); add the bottom variant by reflecting y about the panel centre
   rather than writing a second polygon.
3. `DesktopShell.h` ~L318 builds the window region with `Notch(...)`; make region + painting use the same polygon so there is no
   hit-test/visual drift. `NotchHotZone::Place` uses `bounds.top`; hot zone at the bottom edge must be 2 DIP **inside** the
   monitor so it still receives mouse events (a 1–2 px bottom strip is also where Windows' own auto-hidden taskbar triggers).
4. Springs: `Spring` is direction-agnostic; feed it signed displacement from `Anchor`. Flyouts (Wi-Fi/Audio lists, palette,
   dock-status, tooltips) open **upward** from the clicked tile, and content that would overflow the screen top flips
   to open downward. Page transitions slide horizontally and need no change.
5. Scroll direction: today wheel-down expands. With a bottom island the natural gesture is wheel-**up** to expand
   (toward the screen centre). Make it a setting, default wheel-up for Bottom, wheel-down for Top.
6. Multi-monitor: one island per monitor was already designed for the dock; decide whether the island follows the cursor
   monitor or the primary one (question 6).
7. The existing bottom **dock** (app icons, AppBar reservation) and this notch-island can coexist: dock = apps strip,
   island = notch above/at the same edge. Decide whether they merge into one object (question 3).
8. Tests: extend `DockMotionTests.cpp` with Top/Bottom symmetry tests — for any point `p`, `Contains(Bottom, p) ==
   Contains(Top, reflect(p))`. Extend `NativeShellTests` for bottom hit-testing and click-through.

## 5. Workstream C — Overlay everything (fullscreen apps, browsers, menus)

What Windows allows, honestly:

- **Works:** `HWND_TOPMOST` + `WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW` stays above borderless-fullscreen apps and F11 browser
  fullscreen. This is the case you described ("app/browser etc.").
- **Works with a cost:** over exclusive/flip-model D3D games, a topmost window can force DWM composition and add latency,
  and some anti-cheat engines flag overlays. Keep the QUNS check as a per-app rule (Settings → Overlay → "Hide over games").
- **Doesn't work without more:** above the Start menu, Task View, Alt-Tab, Snipping Tool, secure desktop, UAC, and the lock screen.
  Those live in higher z-bands. The only supported way up is a **`uiAccess="true"` manifest**, which requires a code-signing
  certificate and installation in a trusted location (e.g. `Program Files`). Your builds are unsigned today, so this is a
  later milestone, not a flag.

Steps:
1. Replace the fullscreen *hide* logic (`DockShell.h` L39, `DockShell.cpp` ~L254/652, `Island.cpp` L503) with a policy enum:
   `Always show` (default per your ask) / `Hide over games` / `Hide over any fullscreen`.
2. Re-assert z-order cheaply: handle `WM_WINDOWPOSCHANGING` + `EVENT_SYSTEM_FOREGROUND`
   (already hooked in `NotchHotZone`) and re-`SetWindowPos(HWND_TOPMOST)` only when something else became topmost. **No timer.**
3. Don't reserve work area in notch mode (already true); in dock mode keep AppBar reservation but ignore `ABN_FULLSCREENAPP`
   when policy is Always show.
4. Fullscreen video: with Always show, the notch must stay inert until hovered (no paint, no sampling) to avoid
   dropping frames; verify with PresentMon on a 4K60 video.
5. Click-through everywhere outside the notch region is already done via the window region — keep it, and re-test over
   Chrome F11, YouTube fullscreen, a borderless game, and an exclusive-fullscreen game.

## 6. Workstream D — Hover / scroll behavior

Implemented as one pure state machine (`IslandState.h`, **already written and tested in the foundation zip**), driven by events. States are Dock / Peek / Expanded:

```
            hover(notch)+media            scroll-up/click/hotkey
  Dock ───────────────────► Peek ─────────────────────────────► Expanded
     │ hover(notch), no media               ▲                         │
     └──────────────────────────────────────┼────────────────────────►│  (straight to Expanded)
  Dock ◄──── leave + 450 ms grace ─── Peek ◄──── leave + 550 ms ────┘
```

Rules:
- `hasMedia` = SMTC session exists **and** playback status is Playing or Paused with metadata. Debounce 300 ms so track
  changes don't flicker between Peek and Expanded.
- Peek is only the media card (screenshot 3). Hover over Peek never expands it; **scroll** (up for bottom edge) or click does.
- No media: hover dwell 160 ms → Expanded (existing `Proximity` struct does dwell/outside timing — reuse it, delete the
  duplicated timers).
- Leaving: 550 ms outside (already `Proximity::Step`) → collapse; moving into a flyout/tooltip counts as inside.
- Replace the 100 ms `ProximityTimer` with: `WM_MOUSEMOVE` on the hot zone starts a 16 ms frame-clock-driven dwell that
  stops when the state settles. Idle CPU target: 0 wakeups/s with nothing hovered (verify with `measure.ps1`).
- Keyboard: global hotkey toggles Expanded (existing `Ctrl+Alt+Space`), Esc steps back one level (flyout → Expanded → Idle).
- Animation: one spring (`Spring` @ 23 Hz as today) for width/height/corner radius together, content cross-fades at 40–60%
  of the travel; never animate layout properties separately or the shape and content drift apart (this is the usual
  cause of "not smooth").
- Tests: table-driven transitions in a new `IslandStateTests.cpp` (every event × state), plus a replay test that feeds a
  recorded mouse trace and asserts no more than one state change per 100 ms (no flicker).

## 7. Workstream E — Visual parity with the screenshots

1. Add a `Theme` set: `Midnight` (the screenshots) alongside the existing `Paper`. Make Midnight the default.
   Tokens: `surface #0B0B0C`, `card #161617`, `cardHover #1C1C1E`, `hairline #232325`, `text #F2F2F2`, `muted #8C8C91`,
   `accent #D4B35C` (sample from your PNGs with a colour picker and put exact values in `ThemePalette.h`),
   `danger`, `ok`. Gauges use accent / violet / green as in screenshot 8.
2. Fonts: the screenshots use a rounded geometric sans (looks like Rubik). Ship it in `assets/fonts` and load via
   `IDWriteFactory5` in-memory font collection so there's no install step; keep the existing fallback chain.
3. Re-do pages in this order, each as its own page file with fixture PNGs compared side-by-side against your screenshot at
   100% DPI: Peek → Controls → Today → Stats → Clipboard → Wallpaper → Apps → Power.
4. Tabs: restore Clipboard and Power as tabs; `PrimaryTabs` becomes
   `{Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power}` in the screenshot order. Make tab visibility a setting.
5. New pieces the screenshots need that don't exist yet: ring gauge, skewed wallpaper carousel (`SkewContains` already exists in
   `LayoutEngine.h`), filter chips, key-cap legend, week strip with event chip, speed-test card.
6. Accessibility stays: keep `IslandAccessible` names and high-contrast/reduced-motion paths (existing requirement);
   Midnight needs a high-contrast override.

## 8. Workstream F — Settings app (`DakoSettings.exe`, independent of the dock)

Why separate: the dock can crash, be hidden over a game, or be mid-recovery, and you still need to change settings.
Native C++/Win32 keeps your "no web engine" rule; a separate process also keeps settings UI memory out of the dock.

Architecture:
- New target `DakoSettings.exe`, same build script. Shares `SettingsStore.h` (already atomic, versioned, mutex-protected) —
  move it into a small static lib `dako_core` used by both.
- **Dock ↔ settings sync without polling:** settings app writes `settings.ini` atomically, then signals a named event
  (`Local\Dako.SettingsChanged`); the dock waits on it with a registered wait (no timer) and re-reads.
  Live preview: for sliders (size, blur, spacing) send values over a per-user named pipe so the dock previews instantly and
  the settings app commits on release/Apply. Crash-safe: dock ignores uncommitted previews on restart.
- UI: left nav + search ("search settings"), Windows 11-style cards, built on the same Direct2D/DirectComposition stack
  (reuse `CompositionSurface.h`, `ThemePalette.h`) so it matches the Dako look. Include **Reset page / Reset all / Export /
  Import** (the store already supports export/import).
- Every setting is declared once in a schema (`SettingsSchema.h`: key, type, bounds, default, label, description, page,
  `appliesLive`, `requiresRestart`). The UI, validation, export, search and docs are generated from it. This also prevents
  the current problem of defaults/labels/validation drifting apart across files.

Pages (initial list — trim/extend with question 7):
1. **General:** startup with Windows, language, update channel, reset.
2. **Island:** edge (Top/Bottom), monitor (primary/cursor/per-monitor), width/height scale, corner radius, shoulder flare,
   idle look (notch / mascot / hidden), shadow, opacity/blur, offset from the edge, auto-hide, hot-zone width.
3. **Behavior:** hover dwell, leave delay, scroll direction & sensitivity, click action, hotkeys (rebindable), media-peek rules,
   per-app rules (hide in these apps / always show in these).
4. **Overlay:** fullscreen policy, game list, z-order options, UIAccess status.
5. **Appearance:** theme (Midnight / Paper / System / Custom), accent colour, font, icon style, density, animation speed and
   curve, reduced motion, live preview.
6. **Tabs & widgets:** enable/reorder tabs, per-tab widget toggles (e.g. Today cards, Stats gauges), default tab.
7. **Media:** source apps allowed/blocked, art shape, scrubber on/off, lyrics (optional).
8. **Clipboard:** history size, kinds, retention, encrypted-at-rest toggle (existing DPAPI option), ignore-apps list,
   sensitive-content skipping (existing format flags).
9. **Apps / Dock:** pinned apps, grouping, labels, icon size, magnification, running indicators, per-monitor, taskbar
   replacement (the existing "Use Dako as my taskbar" consent), Explorer recovery tools.
10. **Windows:** taskbar hide/restore, "customize Windows" toggles (see below).
11. **Wallpaper:** folders, slideshow, per-monitor, day/night pairs, fit.
12. **Privacy & network:** Offline switch, weather city, calendar ICS URLs, speed-test provider, capability ledger (existing).
13. **Performance:** sampling intervals (only-while-visible, per existing rule), GPU/CPU budget readout (`measure.ps1` data).
14. **Advanced / About:** logs, diagnostics export, safe mode, version, licenses.

"Customizability of the windows": decide scope before building (question 5). Safe options Windows supports officially:
taskbar/Explorer hiding with recovery, accent/dark mode, window-snap and virtual-desktop shortcuts, Win-key remaps via
registered hotkeys. Anything involving DWM window styling (rounded corners/borders for other apps' windows) is
undocumented territory — I'd keep it out of V4.

## 9. Questions (answered — see section 0.5; kept for the record)

1. **"Mirror everything like menus and animations" — I read this as "flip the top notch behavior vertically for the bottom edge."**
   Is that right, or do you mean Dako should *mirror/replicate Windows' own menus* (Start, tray, context menus) inside itself?
2. **Dock vs notch.** The repo has both a floating app dock (bottom) and a notch island (top). Your screenshots only show the
   island. Should V4 be (a) one bottom island that also holds app icons, (b) island + separate app dock, or (c) island only,
   keeping the Windows taskbar?
3. **Idle with media paused:** is Peek shown for paused media too, or only while actually playing?
4. **Screenshot 1 (mascot) vs 2 (empty notch):** two idle skins, or a state (e.g. mascot shows while loading/notifications)?
5. **"Windows customization":** which of these do you actually want — hide taskbar, accent/dark mode, wallpaper, window snapping,
   virtual desktops, start-with-Windows, system tweaks (registry), other?
6. **Multi-monitor:** island on primary only, follow the cursor, or one per monitor?
7. **Network features in the screenshots:** weather and internet speed test need online requests. Allowed by default, or opt-in as today?
   Which speed-test provider is acceptable (Cloudflare, Fast.com, your own)?
8. **Licensing/signing:** are you willing to get a code-signing certificate? It unlocks `uiAccess` (overlay above Start/Task View)
   and removes SmartScreen warnings.
9. **Distribution:** portable zip only, or an installer (MSIX/Inno) with auto-update?
10. **Hitodama comparison:** hitodama.in wouldn't load for me, so I couldn't check its feature list. Send me a list/screenshots and I'll
    produce a feature-gap table.
11. **Target Windows versions:** Windows 11 only, or 10 too? (Affects acrylic/Mica, SMTC details and some Settings URIs.)

## 10. Ideas to go beyond a taskbar replacement

High value, low risk:
- **Per-app profiles:** island layout/behavior changes by foreground app (e.g. hide in games, always-peek in Spotify).
- **Notification center:** your notes say `UserNotificationListener` fails with `0x80070490`; that API is documented to need package identity / the manifest capability, so this is worth testing.
  Packaging as MSIX (sparse package) is the usual fix and unlocks notifications, badges and progress on taskbar-style icons.
- **Command palette expansion:** unit/currency conversion, calculator (exists), window switcher with live previews,
  quick actions, plugin commands. Already routed through `CommandCatalog.h`.
- **Quick capture:** screenshot/OCR (Windows.Media.Ocr is on-device), color picker, screen recorder entry.
- **Live activities:** timers, downloads, file copy progress, meeting countdown, charging — shown as a compact bar in Peek.
- **Calendar/Tasks integrations:** ICS exists; add Google/Microsoft Graph sign-in later.
- **Media extras:** per-source volume, lyrics (LRC), album-art-tinted accent, scrub on hover.
- **Gestures:** swipe on a precision touchpad, edge scroll, middle-click actions.
- **Profiles/Sync:** export/import already exists; add named profiles (Work / Gaming / Focus) with hotkeys.
- **Focus mode:** hide island, mute notifications, timer — ties into the existing focus timer.
- **Theming:** user theme files (JSON) and a gallery.
- **Plugin API:** the module registry already has capability consent; expose a signed-module or out-of-process plugin model
  (not DLL injection) for third-party widgets.
- **Telemetry-free diagnostics:** one-click "export logs + settings (redacted)" for bug reports.

Bigger bets:
- **Virtual desktops overview and window tiling** (documented Virtual Desktop APIs are limited — use the community
  `IVirtualDesktopManagerInternal` only behind an isolated, version-checked file).
- **Remote/phone companion** (battery, notifications, media control) over local network.
- **Multi-platform:** the state machine and settings schema are portable; rendering is not. Don't plan this before V4 ships.

## 11. Suggested milestone order

| M | Deliverable | Gate |
|---|---|---|
| M0 | Workstream A cleanup, no behavior change | Build clean, all existing verify scripts pass |
| M1 | `IslandState` machine (**written, tested on Linux g++**) wired into `Island.cpp`; old 100 ms timer removed | Idle wakeups = 0, replay test passes |
| M2 | `Anchor.h` (**written, tested**) wired into shell/paint/hot zone; mirrored shapes/flyouts/hot zone | Symmetry tests, manual check on 100/150/200% DPI and 2 monitors |
| M3 | Overlay policy (no fullscreen hide) | Manual matrix: Chrome F11, YouTube FS, borderless game, exclusive game |
| M4 | Midnight theme + Peek + Controls + Today pages matching screenshots | Side-by-side PNG review |
| M5 | Stats, Clipboard, Wallpaper, Apps, Power restyle | Fixture regeneration, accessibility pass |
| M6 | `DakoSettings.exe`, schema-driven pages, live sync | Settings round-trip, crash-safety test |
| M7 | Per-app rules, notification/packaging decision, extras from section 10 | By priority |

## 12. Verification checklist (record real measurements, not assumptions)

- `scripts/build.ps1` at `/W4 /WX`; `verify.ps1`; `test-motion.ps1`; `test-live.ps1`; `test-shell.ps1`.
- Bottom-edge hit-testing and click-through at 100/125/150/200% DPI, mixed-DPI monitors, auto-hide taskbar present.
- Idle CPU over 60 s with and without media; PresentMon over fullscreen video and a game.
- Explorer restart, forced kill, sleep/resume, display hot-plug: island re-anchors and the taskbar/work area is restored.
- Settings: export → reset → import produces an identical store; corrupted `settings.ini` falls back to defaults without data loss (backup).
- Screenshot parity: render each page at 1× and compare with your PNGs (visual diff, with tolerance for font hinting).
