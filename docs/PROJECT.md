# Dako 3.12 development — dock, module foundation and S1 tools

Dako is a C++20/Win32/Direct2D/DirectWrite/DirectComposition dock with a shared island. One current build and portable package are retained. The original V3 supported features and ongoing
V3_1 module/palette/clipboard work are documented in [MODULES.md](MODULES.md). The full suite is unfinished.

## Design principle: a dock, not a taskbar

One floating, content-fit pill contains launcher, merged app icons and native status. Remove edge-to-edge backgrounds,
separate center pills, app text labels, per-window dock buttons, Start tiles/recommendations, separate clock/tray strips,
and rows of tiny tray icons. Show desktop and Task view belong in the shared command palette; optional dock icons are off
by default. A status popover supplies native controls and widget choices, without cloning Explorer's private tray.

The capsule is 56 DIP tall inside a transparent 72-DIP strip, 8 DIP above the edge, bounded to 90% monitor width. The app
zone scrolls on overflow. The native window region follows the capsule so outside input reaches other-process windows;
WM_NCHITTEST also returns HTTRANSPARENT outside it. The latter alone only forwards within the same thread, as described
in Microsoft's [hit-test contract](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest).

`DockShell.h/.cpp` own geometry/grouping, per-monitor HWNDs, AppBar lifecycle, finite spring magnification, input, flash
and DWM previews. `DockView.h` paints the same capsule into DirectComposition and inert fixtures. `DockAccessible.h`
provides stable MSAA identities and safe disconnection. `WindowList.cpp` retains its request-only WinEvent-driven tracker,
AUMID/UWP identity, eligibility filter and per-monitor grouping. Duplicate pin aliases share one icon and preserve the
original persistence indices. No second tracker was added.

`main.cpp` owns startup; `Island.h/.cpp` own the shared panel and native inputs. `DesktopIntegration.h` connects the dock,
settings and panel anchors. `PaletteView.h` provides a shared Ctrl+Alt+K palette, status choices and recent documents.
Three view types reuse the island body, clipping, scrolling, keyboard traversal and accessibility semantics.

## Consent and recovery

`layoutMode=dock` is a preference, not permission. First interactive normal run asks once “Use Dako as my taskbar”. Without
consent, normal startup uses the notch without AppBar registration or Explorer changes. Preview can show a non-reserving
dock. Saved `bar` preferences migrate while preserving unknown settings. An explicit `--desktop-mode` opts in.

Opt-in starts the guard before suppressing Explorer and registering one invisible AppBar per monitor. Windows computes
the work area; dock mode never forces SPI_SETWORKAREA to the full monitor. QUNS/ABN fullscreen events hide the dock.
Switching modes removes the previous mode and restores managed state first. Explorer registration notifications recreate
registrations. The snapshot is bounded to 16 monitors. The native test simulates this notification path; it does not kill
the user's Explorer process.

`EffectsJournal.h` records original Dako-owned values before any write; failed capture prevents a change. `SystemEffects.cpp`
creates a private per-session journal in LocalAppData, restores theme, volume/mute on the original endpoint, per-monitor
brightness, radios by stable device ID and wallpaper/fit/background/slideshow metadata. The guard restores these on exit
and crash, after restoring Explorer/work areas so provider latency cannot delay desktop recovery. Recovery is reverse-order and idempotent; partially failed records remain for retry. `--restore-session="full journal path"` retries a retained record only inside
the per-user Recovery directory after its owning process has exited. Disconnected hardware,
removed images or unavailable providers can prevent restoration. DDC/WMI latency is driver-dependent. Pairing and network
connections open Windows Settings because credentials/pairing cannot be reliably restored. Preview writes remain local.

## Public parity capabilities

`ParityServices.cpp` reads recent/frequent documents with IApplicationDocumentLists on a request-only MTA worker. It
marshals plain document data back to the UI; only a selected item is opened. Lists are capped to twelve items. There is no
public enumeration of another app's custom Jump List tasks. The dock context menu exposes documents when an AUMID exists.

[ITaskbarList3](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-itaskbarlist3) supplies
SetOverlayIcon/SetProgressState/SetProgressValue, without an observer/getter for third-party apps. Badge/progress observation
is therefore unavailable, rather than implemented with invented data or private taskbar hooks.

The [notification listener](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/notification-listener)
requires the manifest capability and user consent. `--capability-check` records package identity, GetAccessStatus and the
public document-list probe and event subscription without requesting permission or reading notification contents.
This host reports Allowed, but registering NotificationChanged fails with `0x80070490`; the event-driven integration is
deferred. Access status alone is not treated as proof that the listener works. Native Wi-Fi/volume/battery/clock access is the chosen V3 tray alternative; third-party tray icons are not mirrored.

## Milestone 3.2 services

`ControlsService.cpp` owns a request-only MTA worker. Dark mode writes both documented
[Windows theme values](https://learn.microsoft.com/en-us/windows/apps/develop/settings/settings-common) and broadcasts
ImmersiveColorSet; it attempts to roll back a partial registry write. Dako's appearance preference stays independent.
Active audio outputs come from EnumAudioEndpoints, with default-output changes delegated to Windows Sound settings.

Brightness reads capabilities before showing the slider. Physical monitor handles are scoped and destroyed; DDC/CI
uses GetMonitorBrightness/SetMonitorBrightness. Internal panels use root/WMI brightness classes, matched to the selected
display's PnP identity (or the sole active panel on a single-monitor machine). WmiSetBrightness uses its uint32 timeout
and uint8 percentage parameters. Requests stay off the UI thread and drag writes are committed on release. Provider
latency depends on Windows and display drivers; synchronous DDC/WMI calls cannot be cancelled by the application.

Radio enumeration is read-only. The explicit Airplane action requests access from the UI context, then changes supported
radios on the worker through Radio.SetStateAsync. Radio events refresh the visible controls without a polling timer.
The tile shows actual radio states, not the OS flight-mode flag. Airplane-mode reversal uses original Radio objects/state pairs, while crash recovery uses stable device IDs,
so identically named adapters do not borrow one another's previous state. Denied requests are visible. The native worker
is not instantiated for render/fixture data, and preview write gates block theme, volume, brightness, radio and wallpaper writes.

`TelemetryModel.h` keeps sixty timestamped samples and per-PID CPU deltas including creation time. Gaps reset baselines;
PID reuse, counter resets and first samples do not invent rates. `HardwareStats` still blocks on requests at rest. While
Stats is visible a one-second timer requests CPU/RAM/network history, CPU/memory process ranks, fixed volumes (including volumes without drive letters), uptime,
battery estimate and GPU state. Missing counters remain unavailable. Histories are never persisted.

Wallpaper thumbnails stay bounded to 320x200 pixels, with only the visible page uploaded to D2D. The old filmstrip's
high-resolution detail requests are removed. Collection, folder and current-image discovery share one grid. Current
images and stable monitor device paths come from IDesktopWallpaper. The selected display is validated again before Apply;
there is no fallback from a disconnected display to applying everywhere. SetPosition is global, clearly labeled in the
UI. `WallpaperModel.h` tests validation, fit failure, image failure and rollback without touching Windows settings.

Apply uses the normal Windows transition and never calls WallpaperReveal. The legacy explicit effects diagnostic remains,
but rejects captures above 2,073,600 pixels (about 8 MiB BGRA) and skips capture on larger desktops. There is no normal-run
capture toggle. Optional day/night pairs and slideshows remain deferred.

## Settings

`SettingsStore.h` owns the versioned UTF-16 `settings.ini` document under `%LOCALAPPDATA%\Dako`. Schema version 3
identifies the merged format. Legacy appearance, task and app files are read together when the new document is
absent. First normal launch commits migration atomically and retains all legacy files. Preview reads without writes.
Each update reloads the full document, replaces one section, flushes a temporary file and atomically replaces the
committed file. A process-wide mutex serializes UI and weather-worker updates. Unknown future schema versions
cannot be overwritten. Failed task saves retain the draft and restore the previous task list.

## Idle behavior

Visible composition surfaces share a weakly held hardware device, released with the final surface. Device creation
requests no internal driver worker threads for this small UI workload. The change addresses measured idle overhead;
full lifecycle and CPU evidence is in VERIFICATION.md. There is no strong process-lifetime GPU cache.


Dock mode installs no proximity timer or hot zone. Its clock timer is aligned to the next minute and runs only while
at least one dock is visible. Flash feedback runs for 1.8 seconds, then leaves a static indicator until activation.
Preview dismissal uses a timer only while the popup is visible. Window-list workers block on requests at rest.

Notch mode retains a 2-DIP mouse hot zone. The old 100 ms sampler runs only near the notch or while its panel is open.
DesktopCatalog reads the initial wallpaper once and then waits without a timeout; settings changes and successful
wallpaper application request an update. Wi-Fi status uses existing WLAN notifications, without background scans.
Stats, media and wireless-discovery sampling remain view-scoped. An explicitly started focus countdown continues
while closed. Opt-in desktop suppression retains the existing one-second recovery reconciliation timer.

## Today services

Weather uses WinHTTP and [Open-Meteo](https://open-meteo.com/en/docs). It is off by default. Entering a city opts in;
only that city and its resolved coordinates are sent to Open-Meteo. The first geocoding result is used. The display
uses Celsius. A successful forecast caches locally for 30 minutes. Requests happen on opening Today or its existing
visible clock tick, with cache/retry gates; there is no network timer while hidden. No location permission is requested.

Calendar subscriptions use an explicitly entered HTTPS ICS URL. The URL is stored in the settings document and is
never included in UI status, exceptions or diagnostic logs. Response bodies stay in memory, bounded to 2 MiB.
WinHTTP uses normal certificate verification, bounded timeouts and disallows HTTPS-to-HTTP redirects. A subscription
refreshes at most every 30 minutes while Today is open; failures can retry after one minute. Worker shutdown waits
for bounded in-flight IO. User-triggered reconfiguration may also wait for that bounded request.

The [RFC 5545](https://www.rfc-editor.org/rfc/rfc5545.html) parser handles unfolded lines, escaped titles, DATE/floating/
UTC timestamps, non-recurring events and DAILY/WEEKLY/MONTHLY RRULEs with INTERVAL, COUNT, UNTIL, BYDAY,
BYMONTHDAY, WKST and EXDATE. Expansion is bounded. Windows time-zone keys and the explicitly mapped IANA zones in
`CalendarService.h` are supported, including per-occurrence DST conversion. Arbitrary VTIMEZONE definitions,
RECURRENCE-ID overrides and other recurrence rules are not supported. Unknown zones/rules are skipped and reported.
Local events are stored in a separate settings section, with a 256-event bound. Task capacity is 20; only the task
list scrolls within its card. Narrow displays additionally scroll the shared body to reach stacked cards.

World clocks and stopwatch open Windows Clock. Clipboard is removed from product navigation; legacy fixture-only
scenes remain to preserve the existing render assertions. Power commands are in the common footer Menu.

## Build and package

`scripts/build.ps1` creates `build/Dako.exe` and its offline wallpaper assets. The requested version must match
the source version resource. Compiler outputs go in `build/obj`; each test suite uses `build/tests/<suite>`.
`scripts/package.ps1` creates `dist/Dako-3.12.0-windows-x64.zip` and `dist/SHA256SUMS.txt` without a second loose
executable. Generated build/package/test outputs are ignored by Git. `build/Review.cmd` opens the current preview.
The October 27 cleanup replaces earlier milestone-retention instructions with one current build.