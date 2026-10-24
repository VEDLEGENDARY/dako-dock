# Project direction

## Accepted requirements

- Work in `C:\Users\vedpa\codingadventure\dako`.
- Match the desktop UI, motion, proportions, and graphics visible in Hitodama's public website demonstrations as closely as possible.
- Do not reproduce the marketing website as the desktop application.
- Exclude voice AI, microphones, dictation, API keys, branding, logos, and the soul mascot.
- Eventually replace the visible Windows taskbar, Start, notifications, Wi-Fi/Bluetooth flyouts, and related desktop surfaces.
- Native Windows implementation with low RAM/CPU usage. Electron and web wrappers are excluded.
- The agent selects tools and implementation details; review visual milestones with the user as development proceeds.
- Latest instruction: testing is authorized again ("i can test now", October 23, 2026). The preview may be opened for review.

## Foundation

Current screenshots supersede the rail experiment: normal popups attach to the screen edge with curved shoulders; active recognized browsers alone detach the popup at 112 DIP. Browser windows fill the monitor with only the notch excluded from the native region, without a horizontal wallpaper gap. Popup context is held during interaction and headers stay anchored as tabs change. Wallpapers reveal in an expanding center-out circle and automatically supply the UI accent palette.

The earlier bottom-center and fixed-rail experiments are superseded. Dako remains a tray/tool window and suppresses the Explorer taskbar while active. Normal Maximize uses the full monitor; no separate fullscreen shortcut is desired. New main app windows maximize once, while later manual resizing stays native. Recovery restores taskbar/work areas and any browser window region after exit/crash.

The latest review requests a proximity-revealed dock that leaves browser tabs unobstructed, consistent styling, smooth motion based on `reference/example.mp4`, and in-app Wi-Fi/Bluetooth controls with secondary Windows Settings links. Headphones/earphones are the Bluetooth priority, while retaining support for other device types. The revised dock implements these interaction foundations; actual wireless profile behavior needs hardware validation.

C++20 + Win32 for windows, input, and desktop integration. Direct2D + DirectWrite for the retained resources and custom-painted interface. C++/WinRT for media sessions. Windows Core Audio for endpoint volume. The installed MSVC x64 compiler and Windows SDK are sufficient; no new runtime stack is needed.

The application blocks in GetMessage when idle. Only finite transition timers and a low-frequency Stats timer while that tab is visible are allowed. System updates should be delivered by native notifications, and background work must not block the UI thread. Performance is measured in Release builds; budget figures are goals, not claims.

## Visual specification from the site

The desktop section at https://hitodama.in/#island shows an animated simulation on a landscape desktop:

- Top-center, top-edge-attached black island with rounded bottom corners.
- Compact media state with square artwork, title/artist, progress, and transport actions.
- Expansion changes width and height around a fixed center anchor.
- Matte near-black panel, darker inset cards, small gray labels, white main text.
- Warm muted gold for active Controls, sliders, and active filter pills.
- Seven tabs: Controls, Today, Stats, Clipboard, Apps, Wallpaper, Power.
- Controls: three tiles (Wi-Fi, Bluetooth, Audio), two wider tiles (Night light, Dark mode), a broad volume slider, then a media card.
- Clipboard: search and type filters, a left item list, a right content preview and metadata.
- Wallpaper: filters and a horizontal perspective-like photo carousel.

The public page reveals only some states. Unknown dimensions, timings, and behavior must be identified as estimates until verified against more frames or user-provided reference material.

## Milestones

1. Native island: compact/expanded states, reference Controls layout, keyboard escape, volume/media integration, native resource measurements.
2. Reference fidelity: capture and compare each demonstrated tab, implement native task storage, app discovery, wallpaper previews and selection; tune transitions and DPI behavior.
3. Native replacements: custom WLAN selection, Bluetooth enumeration/pairing, audio-device picker, media artwork, brightness/display integration, opt-in clipboard history.
4. Desktop integration: taskbar/AppBar, running apps, Start launcher, notification access and grouping, tray compatibility, multi-monitor placement, sleep/resume and Explorer restart recovery.
5. Daily use: portable settings, opt-in startup, crash/recovery watchdog, reversible taskbar suppression. A restore path is required before hiding native navigation.

Full shell replacement is a later deployment decision. This machine reports EditionID `Core` (Home); Microsoft's Shell Launcher is documented for Enterprise, Education, and IoT Enterprise. Secure desktop, sign-in, and UAC remain Windows-owned.

## Sources

- https://hitodama.in/#island — visual and interaction reference.
- https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-overview — native drawing.
- https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-animations-and-media — avoiding perpetual animation and rendering loops.
- https://learn.microsoft.com/en-us/windows/configuration/shell-launcher/ — edition support and replacement-shell limitations.
