# Phase 8 — remaining pages and final acceptance

Updated 2026-10-31. Before images are preserved in
[`design-baseline`](../../design-baseline/verification.txt). Representative after images in this directory
cover Controls, Today, Stats, Power, Wallpaper, Apps, Clipboard, dock, high contrast, 200% text, error state,
confirmation, editor and menu. The full generated matrix remains in `build/tests/render` (1,087 PNGs).
The owner's subsequent pixel-accuracy requirements and page-by-page comparisons are recorded in the
[reference audit](reference-audit.md), including preferred dimensions and intentional data/consent differences.

## Implemented

- Shared list rows, empty states, input wells, search/clear controls, switch sizes, scroll fades and
  finite scrollbar deadlines across remaining island pages. Native IDs, privacy and system-write gates remain.
- Owner reference proportions replace the earlier common-size pass. Bottom tabs/Menu share one row;
  Controls, Today, Stats, Clipboard, Wallpaper and Peek restore the reference compositions. The taskbar
  floats 8 DIP from the usable edge and does not reserve application work area. The obsolete browser-clearance
  menu option is removed. Secondary commands remain
  in Menu. CPU base GHz is read once alongside the existing processor name; inaccessible data is not invented.
- Standalone settings uses shared tokens, Rubik typography and spring responses, a 200 DIP sidebar,
  560 DIP content maximum and responsive forms. Native accessible fields/check states and Apply semantics
  remain; the in-window discard sheet defaults to Cancel. Its manifest enables Common Controls 6.
- Minimum hit targets and shoulder-envelope bounds are asserted in the render matrix. Deferred focus
  transfers to native EDIT only from the island's own focused parent once reveal makes the field visible.
  Stats process identity and the module empty-state readout keep their existing accessible IDs.
- Ambient shadows remain visible on opaque panels. A cached layered bitmap follows the animated bounds
  without adding another GPU presentation queue. Capacity covers the target size and row copies stretch
  the cached mask. Hidden shadows release full resources, retaining only a small source mask;
  resize/collapse resource cleanup is asserted. The last retired GPU device flushes deferred destruction
  and trims driver scratch buffers without adding idle polling.
- The catalog retains its original 320x200 mip, already below the requested 720px ceiling. Unneeded
  opacity layers and collapsed text/device caches are released to preserve the idle memory budget.
- Final side-by-side inspection corrected centered Controls labels, compact tab pills, 64 DIP wallpaper
  strips with 12 DIP gutters, thin focus/gauge rings, compact Clipboard commands and Peek's extra card/fill.

## Validation

- Phase gate (`build.ps1`, `check-style.ps1`, `verify.ps1`, `test-motion.ps1`):
  release build `/W4 /WX /permissive-`, 120-column style gate,
  all 1,087 renders, geometry/identity/MSAA and motion/privacy/module/foundation assertions.
- `scripts/test-v4.ps1`: behavior, targeted paste, state, settings schema/export/reset/import, IPC rollback
  and independent native settings GUI; see [`settings-checks.txt`](settings-checks.txt).
- Final native lifecycle test compiled `tests/NativeLifecycleTests.cpp` at `/O2 /MT /W4 /WX /permissive-`
  and linked the current release objects and standard libraries from `scripts/test-live.ps1`, then ran
  `build/tests/live/NativePolishFinal.exe build/tests/live/polish-reference-final` without `--quick`.
  This exercises 50 composition cycles, native EDIT/caret, both edges, DPI/text tiers, modal focus/cancel,
  MSAA menu states, inert commands, GPU release and a 60-second warm rest.
- Read-only service controls were checked separately, with settings unchanged. No hardware actions,
  clipboard payloads or live network samples were required.

## Measurement scope

| Same-host native protocol | Phase 0 (`119f215`) | Final polish |
|---|---:|---:|
| Idle private memory | 33.6016 MiB | 39.2188 MiB |
| Idle working set | 62.9805 MiB | 67.8672 MiB |
| Normalized idle CPU | 0.053267% | 0.0307765% |
| Paint p95 | 2.5367 ms | 0.6652 ms |
| Presented interval p50 / p95 | 8.3333 / 8.3333 ms | 8.3333 / 8.3333 ms |

The idle-memory guard is **not met**: private memory increased by 5.6172 MiB (16.7%).
Functional acceptance passes, and CPU, paint time and presentation pacing improve or hold,
but this remaining regression prevents unconditional performance sign-off. Font registration and
GDI/heap-cleanup probes did not eliminate it; no speculative global heap trimming is shipped.
Raw final measurements are in [native-results.txt](native-results.txt); the separate CPU/message timing
report is [native-timing.txt](native-timing.txt). [Source hashes](source-hashes.txt),
[binary hashes](binary-hashes.txt) and [object checksum validation](polish-object-check.txt) identify the build.

The original phase-0 revision `119f215` was rebuilt and run on this host for comparison; its raw
[`baseline-live-results.txt`](baseline-live-results.txt) is preserved. The older RC's historical
28.14 MiB / 0.016572% values were recorded on a different run and are retained in `docs/VERIFICATION.md`.
The comparison uses the same 50-cycle / 60-second lifecycle protocol with inert fixture data.
It does not establish zero OS wakeups or a production media/no-session power measurement.

## Manual limits

An isolated live preview showed a clean opaque edge and the in-island menu on this host; Escape restored
the invoking focus. Settings Midnight was also inspected. Physical mixed-DPI monitor transitions and
backdrop edges, Paper on a live unobscured desktop, a complete screen-reader journey and hardware
restoration are not signed off. Automated DPI/text/high-contrast and native GUI checks are distinct
from those physical checks. No personal desktop captures are included in this evidence.
