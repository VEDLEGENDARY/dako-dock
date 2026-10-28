# Dako V4 foundation (verified pure logic only)

Two header-only pieces plus tests. They contain no Win32 and are **not yet wired into Dako**.

| File | Drop into | Purpose |
|---|---|---|
| `src/Anchor.h` | `src/` | `Edge::Top/Bottom`, polygon mirroring, wheel direction, flyout up/down rule |
| `src/IslandState.h` | `src/` | Dock → Peek → Expanded state machine with dwell/leave/cooldown/media debounce |
| `tests/IslandTests.cpp` | `tests/` | 14 tests incl. exact notch mirror against your real `dock::Notch` |

Verified with `g++ -std=c++20 -Wall -Wextra -Werror` against the repo's `DockMotion.h` on Linux (all pass).
**Not verified:** MSVC `/W4 /WX`, any Windows behavior. Add a `scripts/test-island.ps1` that compiles the test with `cl.exe`.

Wiring steps are in DAKO_V4_PLAN.md, milestones M1 and M2.

## V4 integration — 2026-10-28

The supplied headers are now integrated into the production state/geometry paths and extended with runtime
configuration and stable media-loss handling. MSVC pure tests, event/state tests, native fixtures, settings IPC
and GUI tests are available. See the current [architecture](../docs/ARCHITECTURE.md),
[settings](../docs/SETTINGS.md) and [verification](../docs/VERIFICATION.md). The original text above records the
foundation's status when supplied; it is not the current runtime status.
