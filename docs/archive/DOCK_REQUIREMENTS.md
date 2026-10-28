> Historical record. The V4 foundation plan supersedes conflicting product instructions.

Reconcile task: turn Dako's taskbar-style bar into a dock. Read this whole message before editing.

Context
V3.md milestones are done. During that run the bottom bar was built like a Windows taskbar clone: a full-width AppBar with left, center and right clusters, a Start-style Apps button, and a row of tray-like icons. That is the wrong product. Dako must be one floating, aesthetic, all-in-one dock that absorbs the taskbar's functions. It must not look or behave like a taskbar. The island, palette and HUDs grow out of the dock as one object.

Step 1: Audit (no edits yet)
Inspect the repo and write docs/DOCK_RECONCILE.md listing:
- Every place the bar is drawn, laid out, hit-tested or documented (BarShell.h or equivalent, layoutMode, AppBar code, bar fixtures, README, docs/PROJECT.md, V3.md).
- Which parts already match the dock spec below and which must change.
- Any behavior that must be preserved (running-app tracking, previews, flash, fullscreen hide, per-monitor logic, recovery guard, work-area restore).
Then proceed. Do not rewrite working plumbing for style.

Step 2: Target design
- One surface: a single floating pill, centered, about 56 DIP tall, fully rounded ends, paper surface with an ink hairline and soft shadow, about 8 DIP above the screen edge. Content-fit width. Never edge to edge. If content exceeds about 90% of the monitor width, the app zone scrolls with an overflow chevron.
- Keep the AppBar only as invisible reservation: SHAppBarMessage (ABM_NEW, ABM_QUERYPOS, ABM_SETPOS, ABE_BOTTOM), reserved height about 72 DIP scaled by DPI and text scale. The window spans the strip, is transparent through DirectComposition, draws nothing outside the pill, and returns HTTRANSPARENT from WM_NCHITTEST outside the pill so clicks reach windows beneath.
- Three zones inside the one pill, separated by thin dividers:
  1. Launcher: Dako mark (opens island Apps view) and a search icon (opens the command palette).
  2. Apps: pinned and running apps merged. A pinned app that is running is one icon with a running dot, not two. Group windows by AppUserModelID. Running unpinned apps follow after a divider. Drag to reorder. Middle-click opens a new instance.
  3. Status: weather glyph, Wi-Fi, volume, battery, compact clock. Icons only, the clock is the only text. At most five glyphs, user-chosen. Clicking one opens the island at the matching view.
- Icons: about 40 DIP, hover magnification with elastic neighbor spacing, tooltip, preview strip above the pill (DwmRegisterThumbnail), flash pulse. Reduced motion: no magnification, instant state changes. High contrast: solid surface, strong outline, no shadow. Use the existing motion system and constants.
- The island opens upward from the pill, anchored to the clicked zone, same panel and spring motion. Pill and island read as one object.
- Remove: text labels on apps, per-window buttons, any Start-style button or menu layout (tiles, recommended section), any separate clock or tray strip, any row of tiny tray icons, any second bar. Tray access becomes an overflow popover opened from the status zone. Show desktop and task view become palette actions, plus optional dock icons that are off by default.
- Settings: layoutMode is `dock` (default) or `notch`. Rename any `bar` value and migrate saved settings. The two modes stay mutually exclusive and switching restores the other mode's state first.
- Replacing the taskbar needs desktop mode (Explorer taskbar suppression, opt-in, existing recovery guard process). First run asks once: "Use Dako as my taskbar". Without it, a normal launch does not touch Explorer, registers no AppBar and shows the notch.
- Work area: do not force SPI_SETWORKAREA to the full monitor in dock mode. Notch mode keeps its current behavior.
- Fullscreen: hide the dock via ABN_FULLSCREENAPP and SHQueryUserNotificationState. Optional auto-hide slides the pill off the edge and releases the reservation with ABM_SETAUTOHIDEBAR. No "hide only on overlap" mode.

Step 3: Implement
- Rename bar code and docs to dock (for example BarShell.h to DockShell.h, bar mode to dock mode). Keep git history readable: rename first, then change behavior.
- Rebuild the layout as the three-zone pill. Reuse existing services (window list, previews, flash, fullscreen logic, per-monitor handling). One dock per monitor, windows filtered per monitor.
- Hit-test bounds, accessibility names (IslandAccessible), keyboard traversal across all three zones, and fixture renders at the existing 5 DPI by 4 text-size matrix for the new pill, including overflow, reduced motion and high contrast.
- Update README, docs/PROJECT.md and V3.md wording so they describe a dock, not a bar. Add a short "Design principle: a dock, not a taskbar" section to docs/PROJECT.md with the removal list above.

Step 4: Constraints (unchanged)
- C++20, Win32, Direct2D, DirectWrite, DirectComposition. No Electron, WebView, JS runtime or web server. No AI, voice or microphone features.
- Keep MSVC /W4 /WX. Keep the 127 fixture render verification, motion tests, async tests and lifecycle harness passing, extended for the new pill.
- No new perpetual timers. Prefer events. Normal launch must not alter Explorer.
- Anything that changes system state restores on exit and on crash. Work area must be restored after exit and after a forced kill (extend scripts/test-shell.ps1).
- Code style: concise, lines at most about 120 characters, short comments, no end-of-function comments.
- Unit tests: AppBar rectangle math, pill hit-testing (outside the pill is click-through), DPI conversion, window eligibility filter.

Step 5: Acceptance
- A fixture render of the dock, beside a screenshot of Explorer's taskbar, is not mistakable for it. Record the review in docs/VERIFICATION.md.
- Maximized browser content is not covered. Clicks beside the pill reach the windows beneath.
- Work area restored after exit and forced kill. Explorer restart recovery works.
- All existing verification passes. Idle CPU over 60 s is no worse than before this task.
- Do not claim results you did not measure on a real run. List anything you could not verify in docs/VERIFICATION.md.

When done, give me a short summary of what changed, what was removed, and anything that needs manual hardware testing.