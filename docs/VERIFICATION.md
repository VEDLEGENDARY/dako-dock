# Dako 3.12 development verification

Updated October 27, 2026. This is the current verification record; cumulative milestone copies and old generated
builds were removed at the owner's request. V3_1 remains unfinished. No paid feature, dependency or service was added.

## Reproduce

```powershell
.\scripts\build.ps1
.\scripts\test-motion.ps1
.\scripts\test-settings-pages.ps1
.\scripts\test-command-models.ps1
.\scripts\test-clipboard-transfer.ps1
.\scripts\test-clipboard-vault.ps1
.\scripts\test-clipboard-storage.ps1
.\scripts\test-file-search.ps1
.\scripts\test-shelf.ps1
.\scripts\test-system-actions.ps1
.\scripts\verify.ps1
.\scripts\test-live.ps1 -Quick
.\scripts\package.ps1
```

The single executable is `build/Dako.exe`, with bundled wallpapers and review launchers beside it. Compiler
outputs use `build/obj`; test suites use separate `build/tests` directories. Packaging creates one unsigned
portable ZIP and its SHA-256 checksum in `dist`. Generated objects, tests and captures can be deleted after validation.
`test-live.ps1` without `-Quick` runs the full 50-cycle/60-second lifecycle measurement. `test-shell.ps1` exercises
real temporary AppBar/work-area ownership and recovery; run it during a deliberate desktop acceptance session.
`test-keep-awake.ps1` exercises temporary execution requests and requires an unlocked desktop session.

## Consolidated build

The fresh single-directory release build passed compilation and 1,052 fixtures before the owner requested no
further testing. Completed regression runs also passed motion/frame/async/foundation, module/privacy, Settings,
command models, clipboard transfer/vault/storage, indexed files, OLE shelf and system-action models. The already
running native quick lifecycle/read-only-controls check completed successfully. No additional test runs were started.
Temporary compiler/test outputs and captures were removed after recording these results.

## Current 3.12 results before consolidation

- C++20 release compilation passes `/W4 /WX`. The source and binary version are 3.12.0.0.
- All 1,052 PNG/geometry/identity/MSAA fixtures pass, including Settings app/device context captures at five DPI
  and four text-size tiers. Normal and extreme DPI/text views were inspected. Full target names remain accessible.
- Settings tests cover strict UTF-8/percent decoding, canonical/idempotent encoding, supported parameter routes,
  malformed/unsupported queries, NUL/control rejection, bounds and SDK package-family validation. They launch no
  Settings page. Module tests check canonical confirmation metadata, stale IDs and non-learning transient actions.
- Native quick verification passes complete input beyond 120 characters, oversized-paste rejection without
  truncation into a valid target, canonical matching and shared-edit accessibility names. Windows preferences and
  work areas remain unchanged. Handler-to-paint p95/max was 2.725/2.815 ms. No valid presentation-statistics sample
  was available, so this does not establish on-screen latency; its 0.110-second rest is not a full CPU measurement.
- Nine modules and the diagnostic pass 50 unload cycles. Production handles/GDI/USER return to baseline
  (203/0/1), with no named module worker remaining. The bounded helper returns parent handles to 161, accesses no
  clipboard in its synthetic check and uses approximately 2.45 MiB private commit within its 64 MiB ceiling.
- Motion, frame readiness/backpressure, async cancellation, foundation, inert baseline views, privacy/settings,
  calculator/catalog, encrypted storage, file/query/IPC and OLE shelf regressions pass with synthetic data.
- The 60-second module budget check reports zero attributed idle CPU/hook work. Explicitly owned idle bytes are
  Windows 70, utilities 1,570, clipboard 2,010, snippets 1,464, keep-awake 3,898, files 1,230, shelf 1,444,
  Recycle Bin 1,186 and drives 1,388. These figures exclude shared/allocator/OS overhead.

## Prior native measurements and decisions

The full 3.11 native run passed 50 composition/lifecycle cycles and preserved Windows preferences/work areas.
Its 60-second warm rest measured 0.016572% normalized CPU, 26.33 MiB private memory and 58.77 MiB working set.
Handler-to-paint p95/max was 8.444/8.967 ms; on this 120 Hz host frame interval p95/p99 was 8.333/16.667 ms.
These are historical single-host measurements, not a new 3.12 whole-app benchmark.

Keep-awake native checks passed 50 warmed registration/unload cycles, real synthetic-child start/exit observation
and 51 manual execution-request/restore cycles. Handles returned to 152 and USER objects stayed at 2; observed
maximum callback work was 158.9 microseconds against a 1,000-microsecond limit. No power preferences changed.
A restricted sandbox denied the unlocked-session query; the approved outside run passed.

Native dock tests verified rounded-margin click-through to a foreign-process HWND, actual AppBar clearance for
a maximized native test window, DWM thumbnail registration, fullscreen callbacks and normal/forced-parent guard
restoration. Explorer-restart handlers were simulated; Explorer was not killed. Win+number registered 0/9 on
this host, so that shortcut is not certified. Keyboard navigation within the dock remains available.

Notification access reported Allowed but event subscription failed with 0x80070490. No permission request or
notification-content read occurred; notifications remain deferred. Public recent/frequent documents are supported;
third-party custom Jump List tasks and badge/progress observation remain unavailable.

The public icon-visibility setter experiment failed functional acceptance, with original visibility restored and
independently verified. A UI Automation experiment recognized owned standard plain/password EDIT controls but
did not establish safe generic insertion. Both features remain deferred; unused one-off probe sources were removed.
See SYSTEM_COMMAND_DECISIONS.md and INSERTION_DECISION.md for rationale and API references.

## Manual acceptance still open

- Actual Explorer screenshot comparison, maximized browser content/clicks and a complete screen-reader journey.
- Physical mixed-DPI/multi-monitor hotplug, fullscreen games, elevated app activation and actual Explorer restart.
- Real theme, endpoint volume/mute, laptop/external brightness, radio and wallpaper changes and restoration,
  including crash/reconnection. Failed restores retain their recovery journal; provider latency is driver-dependent.
- Actual recent-document launches, Settings device/page availability, power/display/lock behavior, real Recycle Bin
  deletion and removable-device interoperability. Automated fixtures do not delete user data or eject real devices.

Optional auto-hide, OS location, wallpaper schedules, remaining S0/S1 acceptance and S2–S8 are not completed.
License, signing and private security-contact decisions remain open; this is an unsigned development package.