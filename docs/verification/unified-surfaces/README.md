# Unified surfaces verification

Implementation and acceptance evidence for `DAKO_UNIFIED_SURFACES.md`, November 1, 2026.
Baseline: local `6d72f1c`, compared with the brief's `083f8c1`; see [local diff](local-diff.txt)
and [baseline captures](baseline/). The referenced `DAKO_UPSCALE_AND_TASKBAR.md` was absent;
the available briefs supplied the requirements implemented here.

## Implementation

| Phase | Result |
|---|---|
| 0 | Audited the local implementation and preserved the 1,087-image baseline. |
| 1 | `Shape.h` owns Surface 28, Card 16, Control 12, Chip 8 and Fillet 28. Continuous corners, pill/inset helpers, shared eight-entry LRU and enforced radius lint replace per-surface radii. Legacy Radius settings are ignored. |
| 2 | One cached union outline paints the pill and extension and supplies clipping, hit testing, flattened window/backdrop regions and the GPU shadow. Both edges share an exact join, with no overlap offset. |
| 3 | A monitor-stable envelope and `SurfaceMotion` preserve graphics and velocity through retargets. Content snapshots crossfade at their original scale and remain anchored to the presented edge. Width classes are 360/480/800 DIP; tall content scrolls within the 80% height limit. |
| 4 | Dock controls route through Action/Peek/Panel requests. Status clicks deep-link into the common Quick Panel; saved/open Wi-Fi networks connect directly from its list. Volume wheel/middle-click use an in-pill HUD. Full tabs remain available through expand. |
| 5 | App context menus, overflow, preview cards and delayed label chips render inside the shared surface. The separate native preview, HMENU and tooltip paths are removed. Up to three DWM thumbnails target the composition envelope itself. |
| 6 | Expanded image fixtures, raster joins/radii, motion, production navigation, native composition and lifecycle/resource checks provide the evidence below. |

The dock remains a floating overlay eight DIP from the edge and does not reserve browser/work-area
space. Bottom-dock surface navigation stays at the bottom. Native dock input remains available
under the composited pill; its separate paint is suppressed while the joined surface is visible.

## Verification scope

The content fixture matrix covers all 19 surface/control scenes at 100%, 125%, 150% and 200%,
both edges, and Midnight, Paper and high contrast. The full suite also retains the earlier page,
text-scale, accessibility and settings-link cases. Content captures and silhouette captures are
separate: the former show page layout; the latter exercise the actual shared geometry.

Raster checks cover 216 joins (four DPIs, two edges, three materials, three width classes and
three anchors), with 216 enlarged join crops. Samples cross five rows around each join and reject
alpha gaps, internal hairlines and fill-color steps. Sixteen corner audit images sample the four
radius tokens through the production rounded-fill helper. Geometry tests additionally check
reflection, containment and simple outlines across 36 union/anchor combinations.

The native navigation harness links production objects and uses its own inert windows. It checks
both dock edges, direct Wi-Fi routing, active-button toggle, Escape, Alt+F4, panel focus, stable HWND
bounds/graphics, wheel/middle-click actions, stale hover rejection, rapid animated retargets and
completed snapshot cleanup. It enumerates its process for forbidden native menu/tooltip/dialog/
preview classes. Windows Graphics Capture samples only the harness's own HWND: join pixels must
match on either side, and real DWM thumbnails must reproduce its magenta source window above the
DirectComposition swapchain with `WS_EX_NOREDIRECTIONBITMAP`.

Motion tests cover eleven routes, opening under 320 ms and retargeting under 240 ms at default
speed, velocity preservation, content-before-shrink close ordering, reduced motion, hover
generations, delayed label fade and no frame clock at rest. Native lifecycle tests exercise
50 open/close cycles, interruption, GPU cleanup, work-area preservation and a 60-second rest.

These are deterministic raster checks and native software integration checks. Hardware writes,
personal-window capture and clipboard mutation are disabled in the harnesses. They do not claim
manual screen-reader sign-off or a perceptual comparison against unavailable earlier chat images.

## Reproduction and retained evidence

Run these from the repository root in PowerShell with the Visual Studio C++ toolchain installed:

```powershell
./scripts/build.ps1
./scripts/check-style.ps1
./scripts/test-motion.ps1
./scripts/verify.ps1 -RenderDirectory build/tests/unified-final
./scripts/test-unified-navigation.ps1
./scripts/test-live.ps1 -UseProductionObjects -OutputDirectory build/tests/unified-lifecycle-final
./scripts/test-v4.ps1
```

Build before the native navigation/lifecycle harnesses; do not rebuild while they are running.
Their production objects were checked newer than every shared header and their source files.
The final implementation is commit `b8834d0` (following the shape migration commits).

- [Build](final/build.txt), [style and radius lint](final/style.txt), [motion/geometry/raster suite](final/motion.txt).
- [1,543-fixture acceptance](final/fixtures.txt), [production navigation and own-HWND pixel samples](final/navigation.txt).
- [Settings, IPC and independent GUI checks](final/settings-v4.txt) from the earlier acceptance run;
  the settings implementation did not change after that run.
- [Representative content, join crops and corner audits](final/).
- [Source/binary SHA-256](final/source-binary-sha256.txt), [all fixture hashes](final/fixture-sha256.txt),
  [all 448 raster/crop hashes](final/raster-sha256.txt). Full generated image sets remain in ignored `build/tests`.

## Final resource acceptance

The timestamp-validated release objects completed 50 native composition cycles and a 60-second
warm rest interval. The previous polish measurement and this run used the same machine, but these
are individual observations rather than a statistical benchmark series.

| Rest measurement | Previous polish | Unified surfaces |
|---|---:|---:|
| Private memory | 39.2188 MiB | 29.6094 MiB |
| Working set | 67.8672 MiB | 58.4727 MiB |
| Normalized CPU | 0.0307765% | 0.0272254% |

Final rest resources: 273 handles, 12 GDI objects, 14 USER objects. The frame clock and animation
timer were stopped and composition resources released. Warm Palette hotkey-to-paint p95 was
8.3183 ms. CPU paint p95 was 1.2324 ms; this is not a GPU duration. Reported presented-vblank
intervals were p50/p95 8.3333 ms, p99 16.6667 ms on the 120 Hz display; invalid/disjoint statistics
are retained in the raw report rather than counted as valid presentation samples.

See [native lifecycle results](final/lifecycle-results.txt), [timing counters](final/timing.txt),
[read-only controls](final/read-only-controls.txt) and [complete lifecycle command log](final/lifecycle.txt).
The work-area preservation checks passed for dock/notch switching and the floating overlay.
