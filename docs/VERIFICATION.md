# Verification — Dako 4.0.0-rc.1

Reviewed 2026-10-28. The current [checked-in evidence snapshot](verification/4.0.0-rc.1/README.md) includes
passing logs, exact commands, source hashes and binary identities. Both executables are marked prerelease;
the portable archive is `Dako-4.0.0-rc.1-windows-x64.zip`.

## Current automated acceptance

- MSVC C++20 release build at `/W4 /WX /permissive-` passed for the island, settings app and shared core.
- V4 behavior, schema, export/reset/import, corrupt backup, IPC rollback/client death and native GUI passed.
- Pure state/motion, layout, privacy, module, frame-clock/async and settings/calendar tests passed.
- The fresh 1,068-image matrix passed geometry, identity, high-contrast, DPI/text tiers and expanded MSAA checks.
- The complete shell gate passed AppBar/work-area recovery, fullscreen callbacks, native icon dwell exclusion,
  mirrored bidirectional overflow wheel messages, DWM previews and forced-exit recovery.
- The complete lifecycle gate passed the Stats PID assertion, 50 composition cycles, 60-second rest, native
  fields/caret, joined panel placement, palette search, preview work-area preservation and read-only services.
- All C++ source and test files fit the enforced 120-column limit.

The earlier 4.0 development run stopped after its Stats PID failure. This RC reruns that fix and every
required acceptance command on current production code. Two outdated/environment-sensitive native assertions
were corrected during verification; the evidence README explains their contracts and final passing runs.

## Measured scope

The native fixture's 60-second warm rest measured 0.016572% normalized CPU,
28.14 MiB private memory and 58.58 MiB working set. These are single-host synthetic-content measurements.
They do not establish zero OS wakeups or separate production playing/no-session behavior. The old perpetual
proximity poll is absent; state tests verify no interaction deadline at settled idle. Slideshow uses explicit,
one-shot deadlines and completion-based rearming rather than a periodic timer.

## Manual acceptance still open

- Physical mixed-DPI dual-monitor hotplug, sleep/resume, auto-hidden Windows taskbar and actual Explorer restart.
- Chrome F11, fullscreen video, borderless/exclusive games and PresentMon comparisons. Higher Windows bands,
  secure desktop and exclusive presentation retain platform limits; uiAccess is not shipped.
- Separate production rest measurements with real playing media and without a session.
- Real slideshow, theme/audio/brightness/radio/wallpaper changes and restoration on relevant devices, including
  disconnect/crash recovery. Failed restores retain their recovery journal.
- A complete screen-reader journey and supported third-party clipboard editors. Targeted WM_PASTE supports
  standard Edit/RichEdit fields; unsupported/custom/elevated/secure fields use Copy/manual paste.
- Explicit Online Open-Meteo and Cloudflare requests on the user's network. Automated tests consume no sample.

Notification subscription previously failed with `0x80070490` on the unpackaged host. Portable unsigned ZIP
remains the packaging choice. MSIX/notifications, installer, signing and uiAccess are later work. See
[settings](SETTINGS.md), [privacy](PRIVACY.md) and the [plan/reference archive](v4/README.md).
