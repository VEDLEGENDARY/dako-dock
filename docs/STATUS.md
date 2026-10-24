# Development status — October 23, 2026

Testing is authorized again. Keep Explorer and the existing Windows shell available during development.

## Revised dock

- Native C++20 / Win32 / Direct2D / DirectWrite application without a web engine.
- A 216 × 18 DIP flared top notch attaches ordinary popups to the screen edge. There is no reserved horizontal rail or wallpaper gap. Maximized browsers use the full monitor with only the notch subtracted from their window region; original region data is retained for recovery.
- Active recognized browsers alone detach the popup at 112 DIP for toolbar clearance. Context is cached and held through interaction. Tab headers retain their screen coordinates across content size changes. Normal panels, compact media, carousel and typography follow the supplied screenshots.
- Spring expansion/retraction, velocity-preserving reversal, pane cross-fades, animated hover states and wallpaper carousel. Finite animation timers stop after settling; reduced-motion preference is respected.
- Shared near-black styling with wallpaper-derived accent colors across Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power and wireless flyouts. The color worker notices external wallpaper/file changes without repeated decoding.
- Native volume and media-session transport/metadata, lazy app discovery/search, persistent tasks, focus timer, real system/process telemetry, current clipboard text, bounded wallpaper thumbnails and explicit wallpaper application.
- Wi-Fi flyout: real network enumeration, radio state, saved/open/password-based connection requests, disconnect and connection notifications. More settings opens Windows Settings.
- Bluetooth flyout: Classic/LE enumeration, bounded discovery, connection requests, disconnect, native pairing consent and removal. Headphones/earphones are the priority; profile-specific connection behavior still requires hardware testing. See [wireless support and limits](NATIVE_WIRELESS.md).
- Keyboard navigation, MSAA metadata, native text inputs, single-instance reveal, Escape dismissal and keyboard/context-menu Exit.
- Windows tray icon; Dako always uses a tool window and stays out of Alt+Tab, including review mode.
- Explorer taskbar suppression through native show events and a one-second fallback; full-monitor Maximize/Snap. New resizable, unowned main windows maximize once; manual window sizes remain untouched. No separate fullscreen shortcut or app-frame modification.
- A native recovery helper restores previous taskbar mode and monitor work areas after exit/crash. If recovery cannot start, suppression is not enabled. Ctrl+Alt+Shift+T and the context menu restore the taskbar without exiting.

## Validation

MSVC C++20 Release build passes `/W4 /WX`. Motion tests cover settling, reversal, hover dwell, travel corridor, modal protection, dismissal grace, Escape cooldown, browser classification and top-notch orientation.

The reference-shape revision passes 24 renders, including attached tabs, browser detachment, compact media and palette variants. Native checks pass for full work areas, notch-only clipping/restoration, maximize/manual resize and recovery of a surviving foreign-process window after abrupt exit. A real layered-window circular animation test passed without changing the wallpaper. The earlier full-width rail was removed at the user's request. See [desktop effects](DESKTOP_EFFECTS.md).

The final read-only runtime check received two hardware snapshots, four process memory entries and a sampled current-wallpaper accent. GPU utilization did not return a usable sample in this check; the UI shows a dash rather than inventing a percentage. The layer/region check also covers excluding app windows from the circular reveal.

Live inspection showed real music metadata and the Wi-Fi network list with browser chrome visible above the panel. Automated wireless captures use fixtures. No connection/disconnection, pairing/removal, radio changes, wallpaper application or power actions were triggered by the agent. Actual Bluetooth audio reconnection and secured Wi-Fi connection remain user hardware tests.

The cold resting process measured 15.79 MiB working set. After loading media and wireless support, the inspection process measured 132.38 MiB working set and 97.25 MiB private memory. Both short samples recorded 0.0071% normalized CPU. See [measurement context](PERFORMANCE.md); GPU/DWM and animation frame time remain unmeasured. The post-use footprint exceeds the initial private-memory goal.

## Remaining work

- User review of hover behavior, animation feel, DPI and multiple monitors; live headphone/audio and secured-network validation.
- Browser padding is a conservative 112 DIP allowance, not per-toolbar text detection. The cutout clips drawing and does not reflow individual browser tabs. Intentional open popups temporarily cover content below browser chrome.
- Broader Bluetooth profile support, audio-output picker and native display controls. Audio/display/color tiles currently open Settings.
- Rich media artwork/app icons, richer clipboard and weather/calendar integration. GPU and top-process telemetry now sample native data while Stats is visible; real power status remains in Today.
- Multi-monitor wallpaper validation. Center-out circular reveal is implemented, with app-window exclusions and finite resource lifetime.
- Notifications, Start/running-app integration and opt-in startup. Explorer still initializes on this Windows edition; taskbar suppression begins when Dako launches and may race briefly with Explorer/system transitions. The tray icon is registered, but the native notification area is hidden with the taskbar.
- Post-use memory reduction, GPU/frame-time/power measurements, sleep/resume, device-change and recovery test matrix.
