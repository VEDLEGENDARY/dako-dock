# Dako 3.12 development

The [attached dock brief](docs/DOCK_REQUIREMENTS.md) is the design authority. See the
[compliance checklist](docs/DOCK_COMPLIANCE.md) for implemented requirements and open acceptance checks.

A native Windows floating dock and desktop island in paper, ink and orange. One rounded capsule holds pinned and running apps,
window previews, weather, Wi-Fi, volume, battery and clock. The island opens upward with five tabs: Controls,
Today, Apps, Stats and Wallpaper. C++20, Win32, Direct2D, DirectWrite and DirectComposition; no web engine or AI.

## Build and review

```powershell
.\scripts\build.ps1
.\build\Review.cmd
```

Requires the x64 MSVC C++ tools and a Windows 10/11 SDK, available through Visual Studio Build Tools or Community.
The development build uses `/W4 /WX`, a static C++ runtime and version resource 3.12.0.0. Builds are unsigned.

One current build lives in `build/`. `Review.cmd` opens its non-reserving dock preview.
`scripts/package.ps1` creates one portable ZIP and checksum in `dist/`. Old milestone outputs and duplicate reports were removed.
Preview opens the island, reads native device/app state, and keeps task/preference/pin changes in memory. It does
not reserve desktop space or change Explorer. Power and wallpaper actions are disabled; app launches still work.
Normal launches share a single running instance. Exit Dako before rebuilding if Windows has locked the executable.

## Optional features (development)

V3_1.md remains in progress. S0 provides a Features page, presets, module lifecycle and capability ledger.
Open Features from the footer Menu or command palette. Windows shortcuts adds 32 named Settings pages and
a validated `ms-settings:page` entry for other page IDs. Typed links ask for confirmation. Documented app/device
parameters are supported for Default apps, App features, Camera and Sound properties; other parameters are rejected.
S1 adds calculator/units/Clean URL, bounded memory clipboard
history, a named snippet copy menu and timed keep-awake. Each is optional and disabled in Minimal. The palette
uses one catalog with local learned usage; clipboard entries and temporary query results are excluded from ranking.
Keep awake also offers explicitly started rules for AC power, external displays and selected desktop apps.
Rule preferences save locally; monitoring does not auto-start on launch. See [rule behavior and limits](docs/KEEP_AWAKE_DECISION.md).
The optional Indexed files source searches local indexed filename prefixes: type `files report`, or open its
island page. Windows Search must be running and the folder indexed. Slow queries cancel without blocking the UI.
First normal run opens the preset picker. See [implemented scope and remaining work](docs/MODULES.md).

Use `--preview --layout=dock --features` for an isolated review, or `--module-check` for the 50-cycle
unload diagnostic. `scripts/test-modules.ps1` tests lifecycle, settings retention and failed startup cleanup.
`build/Features.cmd` opens the new page directly in preview. Preview cannot read/write the clipboard or
start keep-awake requests. History defaults to memory, with opt-in CurrentUser DPAPI storage. Preview allows
explicit file search/launch like app launching; render/test fixtures use synthetic files. The optional Shelf
parks dropped files, text and links in memory inside the island. Preview an item, then click Copy or hold and
drag that button to another app. Original files stay in place. It clears on lock, sleep and removal; preview
uses synthetic records and cannot receive real drops. Sleep, restart and dark-mode palette commands reuse
the existing controls and confirmations. Optional Recycle Bin and Removable drives modules read metadata only
on explicit refresh. Bin emptying and Windows safe-removal requests require target confirmation and are blocked
in preview/fixtures. Data deletion/removal cannot be undone by Dako. Icon visibility, direct insertion,
other S1 acceptance and S2–S8 remain pending. No paid addition was made.

## Run

```powershell
.\build\Dako.exe
.\build\Dako.exe --inspect
.\build\Dako.exe --desktop-mode
.\build\Dako.exe --layout=notch
```

The first interactive normal run asks once, “Use Dako as my taskbar”. Declining shows the top notch without
changing Explorer or registering an AppBar. `--desktop-mode` opts in explicitly. Desktop mode starts the recovery
guard, suppresses Explorer's taskbar and reserves a transparent 72-DIP strip per monitor, scaled by DPI/text size.
The only visible surface is a centered, content-fit 56-DIP capsule, 8 DIP above the edge, limited to 90% width.
Clicks outside its rounded capsule pass through. Preview shows the dock without reserving space or hiding Explorer.
The context menu or Ctrl+Alt+Shift+T restores Explorer. Layout preference is `dock` or `notch`; old `bar` settings migrate.

The mark opens Apps; Search and Ctrl+Alt+K open one command palette for Dako views, desktop actions, apps and windows.
Clock, weather and battery open Today; Wi-Fi opens its native list; volume opens Controls. The island grows upward
from the clicked zone. The status chevron opens widget choices and native status controls; at most five status widgets
are shown, with only the clock as text. Show desktop and Task view are palette commands; optional dock icons default off.
The app zone scrolls with the wheel or overflow chevron. Hover magnification stops at rest and is disabled with reduced
motion. Fullscreen notifications hide the dock. Each monitor retains pins and its own grouped running windows.

## Apps and keyboard

- Click a running app to activate/restore it; clicking its sole active window minimizes it. Grouped windows cycle.
- Hover a running app, or focus it and press Up, for a DWM preview with previous/next, Open and Close.
- Middle-click, or Shift+Enter, requests another instance. Apps may enforce their own single-instance behavior.
- Drag pinned apps, or use Ctrl+Left/Right while a pin is focused, to reorder. Right-click pins/running apps to pin,
  unpin, launch, close, or open public recent/frequent documents when an AppUserModelID is available. Apps retains searchable installed apps and recents, with pinned and paged Running sections.
- Tab/arrows traverse the dock; Enter/Space activates. Escape closes previews. The context menu's Focus dock controls
  command moves keyboard focus there. Ctrl+Alt+Space opens the island; Ctrl+Alt+Shift+Q exits and restores Windows.
- Inside the island, Tab/Shift+Tab traverses controls/native fields. Ctrl+Page Up/Down changes tabs; F2 reads full
  details; Page Up/Down or the wheel scrolls constrained content. Escape returns from a flyout, then dismisses.

Win+number registration is attempted only while desktop suppression is enabled; Windows or other software may own
those shortcuts. Dako reports failed window activation/closing instead of silently assuming success. Elevated windows
can impose additional Windows foreground/UIPI restrictions; no elevated helper or keyboard hook is installed.

## Island views

Controls adds a real Windows dark-mode toggle, a brightness slider for supported displays, and an active audio-output
list with an explicit Sound settings link. Default-output switching stays in Windows Settings; no undocumented helper
is used. Night light remains a Settings shortcut. Project and Screen snip open the native Windows tools.

Brightness targets the display that opened the island, using DDC/CI externally or WMI for a matching laptop panel.
Unsupported brightness and unavailable radio controls disappear. Drag brightness to a value and release, or use arrow
keys in five-percent steps. The Airplane mode tile controls available Wi-Fi/Bluetooth/cellular radios through WinRT;
it does not change Windows' global flight-mode flag. Actual radio states and denied access are reported. Turning radios
back on restores the states saved by Dako in this session. Radio settings remains available as a shortcut.

Dako-owned volume/mute, theme, brightness, radio and wallpaper changes apply only after an explicit normal-run action.
Their original values are journaled before writing and restored by the guard on exit, mode switch or crash. Failed
restores retain a recovery file under `%LOCALAPPDATA%\Dako\Recovery`; disconnected hardware may require reconnection.
Pairing and network connection changes open Windows Settings, where the user manages persistent credentials and pairing.
After reconnecting unavailable hardware, a retained journal can be retried with `--restore-session="full journal path"`;
Dako refuses journals outside its Recovery folder or belonging to a live process. Review/preview keeps dark-mode, brightness,
radio, volume and wallpaper changes local to the preview. Windows dark mode and Dako's own appearance preference are separate;
choose Follow system appearance in Dako's menu to use the Windows theme.

Today combines a large clock/date, city weather, month calendar and next events, twenty scrolling tasks and a compact
focus timer. Battery is in the header. Today settings offers calendar subscriptions and focus presets. World clocks
and stopwatch open Windows Clock. Drafts survive tab/DPI changes; failed saves keep the draft.

Weather is off until you enter a city. [Open-Meteo](https://open-meteo.com/) receives the city search and its resolved
coordinates; successful forecasts cache locally for 30 minutes and display Celsius. The first city match is used;
no location permission is requested. Refreshes happen while Today is visible, with no hidden weather timer.

Calendar subscriptions accept HTTPS ICS URLs, stored locally and never logged. Parsing supports a bounded subset
of non-recurring events and DAILY/WEEKLY/MONTHLY recurrence. Local events stay on this PC. See
[service limits and architecture](docs/PROJECT.md).

Stats keeps a 60-second CPU/RAM/network history in memory, CPU-ranked processes with a Memory toggle, every fixed
volume, uptime, battery estimates and GPU status. Sampling runs once per second only while Stats is visible; reopening
after a gap starts a fresh contiguous history. Per-process CPU uses PID/creation-time deltas and total logical capacity.

Wallpaper uses one paged grid with Collection, My folder and Current filters. Choose a target display and fit, then
press Apply. Fit is a Windows setting shared by all displays, even when applying an image to one display. Failed image
application attempts to restore the previous fit and reports a failed rollback. Wallpaper changes use Windows' normal
transition; Dako does not capture the desktop during Apply. Optional day/night pairs and slideshows are deferred.

Power commands are in the footer
Menu, with immediate Lock and default-cancel Sleep/Restart/Shut down confirmations. Clipboard is absent from navigation.

Tasks, appearance, favorites/order, recents and Today configuration share `%LOCALAPPDATA%\Dako\settings.ini`.
First normal launch migrates the old appearance/task/app files without deleting them. Preview does not migrate or save.
The 18 photos ship beside the executable under `assets/wallpapers`; see [sources](docs/wallpapers/README.md).

## Verify and package

```powershell
.\scripts\test-motion.ps1
.\scripts\verify.ps1
.\scripts\test-live.ps1
.\scripts\test-shell.ps1
.\build\Dako.exe --runtime-check
.\scripts\package.ps1
```

Verification includes 1,052 fixture PNGs, motion/frame/async/lifecycle checks, settings/ICS tests, inert controls,
telemetry/rollback tests, capsule geometry and app grouping. The dock, overflow, magnification, reduced motion, high
contrast, palette, status and recent-document views cover five DPI and four text-size tiers. The shell harness tests
real AppBar reservations, DWM previews, click-through, guard restoration and forced-kill recovery. Run it during a
deliberate desktop validation session. `-DockOnly` isolates its dock cases (`-BarOnly` remains an alias).

The dock installs no proximity sampler or permanent animation timer. The clock aligns to the next minute and pauses
when every dock is hidden. Flash and magnification are finite. Stats and device sampling remain view-scoped; desktop
suppression keeps its existing recovery reconciliation timer. See [measured results and limits](docs/VERIFICATION.md).

Recent/frequent documents use public `IApplicationDocumentLists`. Third-party custom Jump List tasks, badge/progress
observation remain unavailable: `ITaskbarList3` provides setters. On this PC notification access reports Allowed,
but event subscription fails with `0x80070490`, so notifications are deferred. `--capability-check` records a read-only spike without asking permission or reading
notification content. The status popover uses native controls, with no private Explorer tray-icon mirror.
