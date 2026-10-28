# Verification — Dako 4.0.0

Updated 2026-10-28. Results below describe this Windows 11 x64 host. Synthetic fixtures do not establish
physical hardware, every editor, every game or a complete screen-reader journey.

## Automated gates

| Gate | Evidence |
|---|---|
| MSVC C++20 `/W4 /WX /permissive-`, both executables and shared core | `build/v4-release-build.log` |
| Supplied 14 anchor/state tests and expanded event/state matrix | `build/v4-final-motion.log`, `build/v4-final-native-settings.log` |
| Settings defaults, bounds, atomic invalid-value rejection, export/reset/import, corrupt backup | `build/v4-final-native-settings.log` |
| User-scoped preview pipe/event, rollback, forced settings-client exit, clean shutdown | `build/v4-final-native-settings.log` |
| Independent GUI controls, MSAA names, Apply, slider release and isolated settings | `build/v4-final-native-settings.log` |
| Targeted paste identity and standard/protected/read-only/disabled/stale control checks | `build/v4-final-native-settings.log` |
| Pure motion/layout, privacy/Offline, modules, frame-clock/async cancellation, settings/ICS, telemetry | `build/v4-final-motion.log` |
| Clipboard helper IPC/quota/timeout/cancellation, DPAPI expiry/tamper/atomic storage | `build/v4-final-clipboard-transfer.log`, `build/v4-final-clipboard-vault.log` |
| Synthetic encrypted history restart/lock/removal, failed erasure and restrictive persistence | `build/v4-final-clipboard-storage.log` |
| Native shell recovery, actual temporary AppBars, maximize clearance, click-through, forced exit | `build/v4-final-shell.log` |
| Full native composition/lifecycle: FAILED at Stats PID geometry after 50 cycles; final fix not rerun at user request | `build/v4-final-live.log` |
| 1,068 PNG fixtures, geometry/state/MSAA assertions, all V4 pages and top/bottom at 100/125/150/200% | `build/v4-final-render.log`, `build/tests/render/verification.txt` |

The storage integration was rerun outside the restricted sandbox after its locked-file scenario
returned access denied. The approved run passed. The MSVC parallel debug-information build also required
access to the compiler database manager outside the execution sandbox. No test changed user clipboard data.

Shell tests used owned test windows, real temporary reservations and DWM previews, then restored work areas.
Explorer-restart callbacks were simulated; Explorer was not terminated. Win+number registered 0/9 on this host;
that shortcut is not certified. Default Always show and explicit Hide over fullscreen callbacks were tested.
The top/bottom polygon tests reflect the same points and include native regions; PNG DPI matrices are synthetic.

## Performance and visual evidence

The old 100 ms proximity timer is removed. State tests assert no pending interaction tick at settled idle,
with and without stable media. This proves finite timer ownership, not zero operating-system wakeups.
The full lifecycle run reached 50 cycles, then failed the Stats PID geometry assertion before its 60-second rest stage. PID-based row tracking and accessible identity were restored afterward; this fix was build-checked but not test-rerun at the owner's request. A separate resting-preview measurement is recorded below. It uses
synthetic content plus production composition and read-only services; it is not a fullscreen game benchmark.

<!-- measured:start -->

- Isolated resting preview with real read-only services: **60.02 s**, **0.0532% normalized CPU**, **703.12 ms CPU**,
  **53 MiB private**, **96.98 MiB working set**, 533 handles, 18 threads. The host's media session state was not recorded.
  This was not a controlled playing/no-session comparison or a zero-wakeup measurement.
- Full synthetic native run reached 50 composition cycles. Cycle 50: 283 handles, 10 GDI objects, 13 USER objects,
  62.9961 MiB private, 96.1172 MiB working set. It stopped at the subsequent Stats PID assertion; later assertions,
  rest sampling and final native read-only checks did not execute in that run.
- Final release compiles at `/W4 /WX`; the last PID bookkeeping fix and final clipboard metadata changes were not
  rerun through the full suite. The last successful render matrix covered 1,068 fixtures before those final changes.
- Complete pure/motion, settings GUI/IPC, shell recovery and clipboard helper/vault/storage suites passed earlier
  in this implementation. Test runs were stopped when the owner requested it; no complete V4 acceptance is claimed.

<!-- measured:end -->

The supplied references are retained under `dako-v4-foundation/references`. `build/tests/render/v4-*.png`
contains each V4 page and Peek with controlled content. Colors, navigation, rounded cards, ring gauges,
week/focus layout, clipboard split view and skewed wallpaper carousel were reviewed at 1×. Dynamic text,
real provider data and supported control styling differ from the references. The exact sampled accent is
`#CFAE60`; Rubik loads privately from the bundled font without installation. High contrast uses system colors.

## Physical acceptance still open

- Actual mixed-DPI dual-monitor hotplug, sleep/resume, auto-hidden Windows taskbar and real Explorer restart.
- Chrome F11, fullscreen video, borderless/exclusive games and PresentMon 4K60/game comparisons. Higher Windows
  bands, secure desktop and exclusive presentation retain platform limits; V4 does not ship uiAccess.
- Separate 60-second production measurements with real active media and no session. Current state/fixture
  evidence does not substitute for both real-world cases.
- Real audio, brightness, radio, theme and wallpaper changes/restoration on each relevant device, including
  disconnect/crash recovery. Journals remain for failed restores.
- A complete screen-reader journey and clipboard interoperability in supported third-party editors. Targeted
  WM_PASTE is limited to standard Edit/RichEdit fields; custom/elevated/secure fields use Copy/manual paste.
- Explicit Online weather and Cloudflare tests on the user's network. Automated verification keeps requests
  blocked and does not consume the opted-in transfer sample.

Notification subscription previously failed with `0x80070490` on the unpackaged host. Portable unsigned ZIP
is the V4 packaging decision; MSIX/notifications, installer, signing and uiAccess remain later work. Section 10's
other ideas are a roadmap; per-app hiding and redacted diagnostics were selected for this release.

## Reproduce

```powershell
.\scripts\build.ps1
.\scripts\test-v4.ps1
.\scripts\test-motion.ps1
.\scripts\verify.ps1
.\scripts\test-live.ps1
.\scripts\test-shell.ps1
.\scripts\test-clipboard-transfer.ps1
.\scripts\test-clipboard-vault.ps1
.\scripts\test-clipboard-storage.ps1
.\scripts\package.ps1
```

Native tests and captures stay in `build/tests`. `test-live.ps1 -Quick` is a short development run;
`test-live.ps1` without flags is the 50-cycle/60-second gate. Packaging copies both executables, fonts/OFL,
18 wallpapers, the consolidated docs, archived decisions, V4 plan and references into one portable ZIP.
`dist/SHA256SUMS.txt` records the SHA-256. Packaging does not sign the binaries.
