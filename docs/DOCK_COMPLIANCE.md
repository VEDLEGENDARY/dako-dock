# Dock brief compliance — current 3.12 development build

Updated October 27, 2026 against the [verbatim attachment](DOCK_REQUIREMENTS.md), current source and matching
release/test artifacts. That brief takes precedence over the former taskbar design in V3.md. V3 features belong on
the floating dock and its shared island.

**Implementation is ready for review; full V3/manual acceptance remains open.** Implemented below describes code
and the stated evidence, not certification of every physical display, driver or application.

| Attachment requirement | Result | Evidence / remaining validation |
|---|---|---|
| Audit before implementation; rename first | Recorded | DOCK_RECONCILE.md retains the pre-implementation audit. Historical generated rename/build artifacts were removed during cleanup. |
| One centered, content-fit, rounded 56-DIP pill, 8 DIP above the edge; about 90% maximum width | Implemented | DockShell.h geometry, DockView.h paper/ink silhouette and bounded lower-rim shadow treatment; 105 dock PNG alpha checks. |
| Invisible 72-DIP AppBar strip, scaled by DPI/text size | Implemented | DockShell.cpp uses DirectComposition and NEW/QUERYPOS/SETPOS/REMOVE. Only desktop opt-in permits registration. |
| Outside-pill click-through | Native routing verified | HTTRANSPARENT plus the capsule window region; NativeDockTests routes a margin to a real foreign-process window. Physical browser clicks remain manual. |
| Mark opens Apps; search opens the command palette | Implemented | DesktopIntegration.h, PaletteView.h and native palette filtering/execution/focus checks. Ctrl+Alt+K uses the same palette. |
| Pinned/running apps merged by AUMID, running dots and unpinned divider; reorder/new instance | Implemented | DockShell.h grouping/alias handling, FoundationTests and native drag/keyboard/middle-click callbacks. |
| About 40-DIP app icons, elastic magnification, tooltips, DWM previews and flash | Implemented | Existing window tracker/preview/flash plumbing retained; finite spring timer, fixture matrices and real native thumbnail registration checks. |
| Reduced motion and high contrast | Implemented | Magnification disabled with reduced motion; solid strong-outline surface without shadow in high contrast; dedicated DPI/text fixture matrices. |
| Island opens upward from the clicked dock zone | Implemented | Pill bounds/anchors in DockShell and DesktopIntegration; native placement checks. Palette/status transitions also stop the previous view's sampler. |
| User-chosen status widgets, clock as the only status text; status overflow popover | Implemented | Five-bit widget mask, responsive status layout and shared island popover. This uses native status controls, not an Explorer tray-icon mirror. |
| Remove full-width painting, detached center pill, dock app labels/per-window buttons, Start tiles/recommendations and separate clock/tray strips | Implemented | DockView paints one capsule; installed-app names and Running rows remain inside the Apps island, as required by V3. |
| Show desktop/task view in palette; optional dock icons off by default | Implemented | Palette actions and DockOptions defaults; status popover choices. |
| Dock/notch preference, migrate bar, one-time desktop choice, safe ordinary launch | Implemented | Settings merge/migration, ConfirmDesktopChoice and DockEnabled gating. Without consent, normal launch uses the notch without reservation/suppression. Preview is non-reserving. |
| Preserve per-monitor filtering, fullscreen hide, recovery and work-area ownership | Implemented; physical acceptance pending | Existing services retained. Native tests cover enumerated monitors, fullscreen callbacks, exact exit/forced-kill restoration and simulated Explorer restart handlers. Actual hotplug, games and Explorer restart remain manual. Dock mode does not force full-monitor work areas. |
| Restore Dako-owned settings on exit/crash | Implemented; hardware acceptance pending | First-value journal, fail-closed writes, idempotent retry and real guard/mock-backend tests. Explorer/work areas recover before hardware replay. Real theme/audio/brightness/radio/wallpaper restoration remains manual. Pairing/connections open Windows Settings. |
| Native C++ stack, no AI/voice/microphone, no new perpetual application timers | Preserved | Native build and service design unchanged; motion is finite, workers request-only, sampling view-scoped. Existing visible clock and opt-in guard reconciliation timers are documented. |
| Bounds, accessibility, keyboard navigation; five DPI by four text tiers | Automated checks pass | 1,052 current fixtures, MSAA checks, native fields/focus, full fifty-cycle lifecycle and dock resource tests, plus final targeted view-transition checks. Full screen-reader acceptance remains manual. |
| Idle CPU over 60 seconds no worse than before | Measured preview comparison passes | Matched resting previews: 0.0071% normalized CPU before, 0.0024% after. Expanded Controls was measured separately. This does not certify normal desktop-mode CPU on every driver. |
| Dock fixture beside an actual Explorer taskbar screenshot | Open | The capture tool did not expose Explorer's taskbar. The historical comparison used two Dako fixtures and did not establish Explorer parity. The requested Explorer comparison is still manual. |
| Maximized browser content clear of dock | Native window clearance verified; browser acceptance open | Real maximized test HWND ends at the reservation boundary. An actual browser acceptance run remains manual. |
| Optional auto-hide | Deferred, optional | No overlap-based hide mode was introduced. |
| Current build and documentation | Consolidated | Per owner instruction, build/Dako.exe and one dist ZIP replace old milestone outputs. README, PROJECT and VERIFICATION describe the current build. |

## Remaining original V3 items

- Public recent/frequent documents and the native status popover are implemented. Third-party custom Jump List tasks
  and badge/progress observation remain deferred; they are not represented as working features.
- Notification access reports Allowed on this PC, but event subscription fails with 0x80070490. No permission was
  requested and no contents were read. Event-driven notifications remain deferred.
- Win+number registration returned 0/9 on this host. Dock keyboard navigation works; Win+number is not certified.
- Physical mixed-DPI/hotplug, browser/game/elevated-window behavior, real setting restoration, recent-document launches,
  the Explorer comparison/restart and a screen-reader journey remain acceptance work.

See [VERIFICATION.md](VERIFICATION.md) for measured results and limitations. The portable package includes this
checklist and the original brief. V3_1.md remains an unfinished roadmap.
