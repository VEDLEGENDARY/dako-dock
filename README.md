# Dako

A native Windows desktop shell project. The visual target is the desktop island shown in the public [Hitodama demonstrations](https://hitodama.in/#island), excluding voice/AI, branding, and the mascot.

## First native milestone

C++20, Win32, Direct2D, DirectWrite, Core Audio, and Windows media-session APIs. No Electron, Chromium, WebView, JavaScript runtime, or background web server.

```powershell
.\scripts\build.ps1
.\build\Dako.exe
```

Requires the Visual Studio C++ x64 build tools and a Windows 10/11 SDK. The build script discovers the existing installation; it does not install anything.

- Move near the flared notch at the top center and pause briefly to reveal the dock downward. Move away to dismiss it.
- The 216 × 18 DIP notch sits directly on the screen edge. No horizontal rail or wallpaper strip is reserved above maximized apps.
- Ordinary popups attach to the top with curved shoulders. Only an active recognized browser detaches the popup, placing it 112 DIP below the top with a click-through toolbar gap. Context is frozen during interaction; tab headers stay in place as panels resize.
- The selected maximized browser uses the full monitor with only the notch excluded from its native window region. Its previous region is restored on app switches, manual restoration, Dako exit or crash. The cutout clips drawing; it does not rearrange browser tabs.
- Escape returns from a wireless flyout, then dismisses the dock. Dragging windows over the edge does not trigger a reveal.
- Ctrl+Alt+Space toggles the island.
- Ctrl+Alt+Shift+Q exits. Right-click also offers Exit.
- Tab/Shift+Tab changes focus, Enter/Space invokes a control, and arrow keys adjust focused volume.
- `--expanded` starts open for development review. `--tab=stats` opens the resource view. A second launch reveals the existing instance.
- Dako registers a Windows tray icon and stays out of Alt+Tab and the taskbar. `--inspect` now only starts the review open; it does not expose an app window.
- Normal Maximize and Snap use the full monitor. New resizable, unowned app windows maximize once; manual restoration/resizing is then respected.
- Right-click the notch for Show/Hide, taskbar restoration and Exit. Ctrl+Alt+Shift+T restores/suppresses the Windows taskbar. Exit also restores the prior taskbar mode and work areas.
- The context menu offers an attached compact media player. Click its title to return to full controls.
- Highlight colors follow the current wallpaper automatically. Set wallpaper uses a finite circular reveal from the center; reduced-motion mode applies immediately.

The source includes in-app Wi-Fi/Bluetooth flyouts, installed-app discovery/search, five persistent tasks, a focus timer, native telemetry, an animated wallpaper carousel and folder picker, and power/session actions with confirmation for sleep/restart/shutdown. Tasks are saved to `%LOCALAPPDATA%\Dako\tasks.ini`. Wallpaper opens the curated 18-photo collection bundled beside the executable for offline use, followed by the user's current Windows wallpaper as "Your wallpaper" when available. My folder browses personal photos; Collection returns to the curated set plus the current wallpaper. The build packages `assets/wallpapers`; if restoring missing photo assets, run `scripts/fetch-wallpapers.ps1` once before building. See [wallpaper sources](docs/wallpapers/README.md).

The user reauthorized testing on October 23, 2026. Native preview and validation may run. Keep shell replacement disabled during this development milestone.

This build suppresses Explorer's main and secondary taskbar windows while Dako runs, using show-event notifications plus a one-second fallback check. A small native recovery process waits without polling and restores the taskbar/work areas on normal exit or a Dako crash. It must start successfully before suppression is enabled. Explorer still initializes; unsupported shell replacement is not installed. Windows can briefly race suppression during shell restarts or system transitions. There is no startup installation yet, so suppression begins when Dako launches.

The tray icon is registered with Explorer; the native notification area is hidden along with its taskbar. Use the notch/context menu or keyboard controls while it is hidden. Wireless changes happen only through flyout actions you choose. Audio output, display and color controls still use Settings shortcuts. Notification integration remains pending.

Browser cutouts use [native window regions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn), with original region data retained for recovery. Browser clearance is a conservative fixed allowance rather than measurement of every extension/custom toolbar. Intentional open panels overlay content below the toolbar. Protected, elevated or unresponsive app windows can reject changes.

Apps stores starred/recent groups in `%LOCALAPPDATA%\Dako\apps.ini`. Stats reads native GPU counters and process working sets only while visible; unavailable counters show a dash. The Today information card currently shows real battery/power status; weather/calendar accounts and richer media artwork remain pending. See [effects and verification](docs/DESKTOP_EFFECTS.md).

See [the design and implementation plan](docs/PROJECT.md), [reference observations](docs/reference/README.md), and [performance notes](docs/PERFORMANCE.md).
