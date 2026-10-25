# Dako 2.0

A native Windows dock and desktop island with an editorial design adapted from [vedp.dev](https://vedp.dev): warm paper, dark ink, orange details, serif headings, monospace labels and thin dividers. Shared navigation stays in place across all seven views; controls respond during motion. C++20, Win32, Direct2D, DirectWrite and DirectComposition; no web engine or AI integration. See the [design revamp record](docs/DESIGN_REVAMP.md).

## Build and run

Download the Windows x64 portable ZIP from [GitHub Releases](https://github.com/VEDLEGENDARY/dako-dock/releases/latest), extract it, and run `Dako.exe`. Keep `assets/wallpapers` beside the executable for the offline collection. The standalone EXE is also available. These builds are unsigned.

```powershell
.\scripts\build.ps1
.\build\Dako.exe
```

Requires installed Visual Studio x64 C++ Build Tools and a Windows 10/11 SDK. Supported Windows 11 builds provide the transient backdrop; unsupported composition/material paths use the opaque native renderer. Build does not download a runtime.

Normal launch preserves Explorer, work areas, other windows' sizes, and browser regions. The top-center reveal affordance yields to recognized browser chrome. Ctrl+Alt+Space and deliberate edge proximity reveal the transient panel. Browser clearance defaults to 112 DIP and is configurable; other panels float 12 DIP below the edge. Smaller displays use compact navigation, responsive cards, scrolling content, and fixed action/status areas.

```powershell
.\scripts\build.ps1 -OutputName Dako.preview.exe
.\build\Dako.preview.exe
```

The isolated preview exposes a normal app window, stays open for review, skips the singleton, and keeps settings/tasks/star changes in memory. Power, wallpaper application, and desktop-policy changes are disabled. It still reads native device/media/catalog state. `--preview` enables this mode on the normal executable; `--inspect` starts normal Dako open without enabling desktop mode.

To create the portable ZIP, standalone EXE and SHA-256 checksums after building:

```powershell
.\scripts\package.ps1 -Version 2.0.0
```

Packages are written to `dist/`. Compiler output, generated verification captures, release packages and local credentials are excluded from Git.

## Use

- Controls: Wi-Fi/Bluetooth flyouts, endpoint volume, media transport, and explicitly labeled Windows Settings shortcuts.
- Today: locale-aware clock/week, power status, focus presets, and five tasks with visible Delete. Drafts survive tab/DPI changes; failed saves retain them.
- Stats: CPU/GPU/RAM rings, process working sets and interface transfer rates. Unavailable GPU samples show a dash.
- Clipboard: one current-text reader with native scrolling/selection, Refresh and Clear preview. No history; text clears on panel close. The 8,192-unit cap is disclosed.
- Apps: native discovery/search, starred apps and Dako launch recency, asynchronous icons, a bounded magnifying rail, and measured pagination. Short panels prioritize the searchable list.
- Wallpaper: bundled offline collection, personal folders, symmetric navigation, transformed card hits, explicit Apply, distinct pending/applied identity, and reserved failure feedback. Nearby GPU thumbnails and one selected-image detail tier are bounded.
- Power: immediate Lock; default-cancel confirmation for Sleep, Restart and Shut down. Preview actions are inert.

Tab/Shift+Tab traverses controls and native fields. Enter/Space activates; focused volume uses arrows; tab arrows switch tabs; wallpaper arrows change selection. Page Up/Down or the wheel scrolls constrained content. Escape returns from a flyout, then dismisses. Ctrl+Alt+Shift+Q exits. F2 reads full details; Ctrl+Page Up/Down changes views, including from native fields. The context menu offers compact media, appearance, opaque material, reduced motion, accent, browser clearance, and restore/exit. Windows reduced motion and high contrast take precedence.

Desktop integration is explicit: `--desktop-mode` enables reversible taskbar suppression after recovery is ready. The context menu or Ctrl+Alt+Shift+T restores it. New-window maximization is independently opt-in. Normal proximity never clips another application's window. Startup installation and shell replacement are not enabled.

Tasks/preferences live under `%LOCALAPPDATA%\Dako`; wallpaper preservation can retain original-path metadata and a fallback image there. Credentials, clipboard text and desktop captures are excluded from diagnostic files. The 18 bundled photos ship beside the executable; see [sources](docs/wallpapers/README.md).

## Verify

```powershell
.\scripts\test-motion.ps1
.\scripts\verify.ps1
.\scripts\test-live.ps1
.\build\Dako.exe --runtime-check
```

Verification includes 127 fixture PNGs at five DPI tiers and four separate Windows text-size tiers, paper/dark/high-contrast variants, layout/identity/catalog assertions, expanded MSAA checks, hidden native EDIT draft/caret preservation across a synthetic DPI transaction, and task persistence/failure checks. The native lifecycle harness also exercises 28 consecutive mouse navigations without waiting for a frame, content buttons and fields during motion, contrast, and non-overlapping wallpaper cards. Fixtures do not establish live presentation quality, screen-reader usability or hardware connection success.

`scripts/test-shell.ps1` deliberately exercises taskbar suppression/crash recovery in an intentional desktop validation session. `scripts/measure.ps1 -Seconds 10 -ProcessIds <id>` isolates selected processes. Optional `--diagnostics=<path>` records bounded timer/CPU paint timing, separately from presented-frame measurements.

See [current verification and limitations](docs/VERIFICATION.md), the [design record](docs/DESIGN_REVAMP.md), and [wireless support](docs/NATIVE_WIRELESS.md). Physical hardware pairing/reconnection, a screen-reader journey, mixed-monitor changes and full GPU/DWM tracing remain acceptance work.
