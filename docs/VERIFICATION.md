# Dako 2.0 verification

The Windows x64 release is built from current source with MSVC C++20, release optimization, a static C++ runtime and warnings as errors (`/W4 /WX`). Version resources identify the application as 2.0.0.0. Releases are unsigned.

## Reproduce

```powershell
.\scripts\build.ps1
.\scripts\test-motion.ps1
.\scripts\verify.ps1 -RenderDirectory build/release-verification
.\scripts\test-live.ps1 -OutputDirectory build/release-live
.\scripts\package.ps1 -Version 2.0.0
```

- Motion/layout tests cover spring reversal, finite feedback, proximity, 295 monitor/DPI/text geometry cases, pagination and transformed hits.
- Frame tests cover readiness, one-frame backpressure, generation rejection, cancellation and 50-cycle cleanup.
- Async tests cover success, deadlines, shutdown cancellation, late completion and 50-cycle event cleanup.
- Render verification creates 127 fixture PNGs across five DPI tiers and four text-size tiers, with geometry, identity, catalog, MSAA, native input and persistence assertions.
- The native lifecycle harness uses inert interaction data and the production renderer. It checks rapid navigation, controls and text fields during motion, contrast, wallpaper hit geometry, 50 open/close cycles, large-text input, resource bounds, 60-second rest and read-only service shutdown.

Generated images, timing logs, test binaries and packages stay under ignored `build/`, `docs/renders/` or `dist/`. Packaging includes only the executable, README, the 18 catalogued wallpaper photos, catalog and photo source notes. SHA-256 checksums cover the EXE and ZIP.

## Scope and limits

Normal launch preserves Explorer, monitor work areas and foreign browser regions. Taskbar suppression and new-window maximization are opt-in. `scripts/test-shell.ps1` intentionally exercises suppression/crash recovery and should run only in a deliberate desktop validation session.

Physical wireless pairing/reconnection, power operations, wallpaper application, mixed-monitor changes, an audible screen-reader journey and full GPU/DWM traces remain separate acceptance work. Fixtures do not establish successful hardware connections or screen-reader usability. DXGI subset statistics and CPU paint timings are observations on a particular host, not complete latency traces or universal performance guarantees. See [the current design record](DESIGN_REVAMP.md) for dated measurements.
