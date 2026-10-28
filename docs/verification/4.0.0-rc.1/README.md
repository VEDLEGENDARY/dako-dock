# Dako 4.0.0-rc.1 verification snapshot

Reviewed October 28, 2026 on Windows 11 x64. The package remains a release candidate: physical game,
mixed-DPI/hotplug, real slideshow/provider and complete screen-reader acceptance are still open.

| Gate | Command | Evidence |
|---|---|---|
| Release build | `scripts/build.ps1` | [build.txt](build.txt) |
| Regression/settings/IPC/GUI | `scripts/test-v4.ps1` | [v4.txt](v4.txt) |
| Pure motion/layout/privacy/modules | `scripts/test-motion.ps1` | [motion.txt](motion.txt) |
| 1,068 fresh PNG fixtures and MSAA | `scripts/verify.ps1` | [render.txt](render.txt) |
| Full shell/recovery and native wheel routing | `scripts/test-shell.ps1` | [shell.txt](shell.txt) |
| Full lifecycle, 50 cycles and 60-second rest | `scripts/test-live.ps1 -UseProductionObjects` | [live.txt](live.txt) |
| 120-column C++ limit | `scripts/check-style.ps1` | [style.txt](style.txt) |

All listed gates passed on this snapshot. No additional test reruns followed the successful gates.
The full lifecycle harness links the freshly built production release objects after validating their
source/header timestamps and release compiler options. The harness itself is compiled separately.
The default `test-live.ps1` also supports a complete independent rebuild.

The new regressions cover launcher dwell versus app icons; bidirectional vertical/horizontal wheel routing
on both edges; playing-only and paused-media Peek; unchanged slideshow configuration; stale generations;
completion/failure guards; one-shot delivery/cancellation; and the native settings checkbox round trip.
The real dock test sends wheel messages and checks stable MSAA identities before/after scrolling.
Slideshow tests do not mutate the user's wallpaper.

An early shell exit assertion was sensitive to real foreground/QUNS state. The fixture now restores an owned
normal foreground window, waits for shell notification settling, and reports diagnostic state on timeout.
A later lifecycle assertion still described the detached V3 panel; it now checks the exact mirrored four-DIP
V4 join with one physical pixel of raster tolerance. The final full commands above passed these assertions.

## Measurements and limits

See [lifecycle-results.txt](lifecycle-results.txt) for raw counters and all completed phases. This is a native
synthetic-content harness, not a production playing/no-media comparison or a PresentMon game benchmark.

- Warm rest: 60 seconds; 0.016572% normalized CPU.
- Warm palette handler-to-paint p95: 8.3239 ms.
- Paint p95: 2.029 ms; 9911 valid presentation samples.
- Presented vblank interval p95: 8.33333 ms on this 120 Hz host.

Explorer-restart callbacks were simulated; Explorer was not killed. The native shell gate uses temporary
owned AppBars and verifies restoration. Win+number registered 0/9 on this host and is not certified.
Offline stayed enabled; weather/Cloudflare calls, actual wallpaper changes and physical device actions
were not exercised. Do not infer secure-desktop/uiAccess or exclusive-game support from these tests.

## Reproduce and identify the snapshot

Install x64 MSVC C++ tools and a Windows SDK. From the repository root, run the commands in table order.
Run the native GUI/shell gates serially in an interactive Windows session. Then run
`scripts/export-verification.ps1`; it refuses logs without complete passing markers and checks binary RC identity.

[manifest.json](manifest.json) records the base commit, each source/test/script hash, binary SHA-256 values,
prerelease flags and commands. Source text is decoded as UTF-8 with BOM removed and CRLF normalized to LF.
The aggregate hash is SHA-256 of UTF-8 `path + NUL + sha256 + newline` in the listed `sources` order.
Logs replace the local repository prefix with `<repo>`. Generated PNGs, binaries, compiler objects and
private local preferences remain outside this checked-in evidence.

Source snapshot SHA-256: `cee09aa58e17569679115b556685a5bcf005951d995e62db7a691fdee9daadfa`.
