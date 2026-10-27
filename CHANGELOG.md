# Changelog

## Repository consolidation — 2026-10-27

- Retain one current `build/Dako.exe` and one portable archive in `dist`, with SHA-256 verification.
- Isolate compiler and test outputs; remove old milestone binaries, cumulative reports and unused experiments.
- Update build/review/package instructions and retain the unfinished V3_1 roadmap and API decision records.

## 3.12.0 development — 2026-10-27 · S1 Settings context links

- Support documented Default apps, App features, Camera and Sound properties context parameters with strict
  UTF-8/escape validation, canonical targets, bounded values and full-target confirmation. Reject unknown parameters.
- Keep typed app/device values in memory and out of ranking/audit text. Match canonical targets from equivalent
  typed spellings, and preserve the original transient target ID across equivalent escaping.
- Expand palette input beyond the URI validation limit so oversized pastes fail validation instead of being
  truncated into a valid target. Keep ordinary app search at 120 characters. Preserve unsigned 3.11; V3_1 remains unfinished.
- Update the shared native edit's accessible name when switching between command and app search.

## 3.11.0 development — 2026-10-27 · S1 keep-awake rules

- Add explicitly started keep-awake rules for AC power, external displays and selected desktop executable names,
  with optional display wake, shared island controls and transactional local settings.
- Release requests when conditions stop matching, on lock/suspend and on module removal. Own native notifications,
  process-exit waits and the minimal window-event callback on one worker; install no polling timer or input hook.
- Require updated capability consent. Keep native rule monitoring blocked in preview/fixtures, preserve unsigned
  3.10 and document protected/windowless-process limitations. Remaining S1 and S2–S8 work is unfinished; no paid addition.
- Declare UTF-8 resource encoding so punctuation and Unicode labels render correctly on ANSI-default build hosts.

## 3.10.0 development — 2026-10-27 · S1 Recycle Bin and removable drives

- Add optional request-only Recycle Bin totals/review and confirmed permanent emptying, with deletion blocked in preview/fixtures.
- Add bounded removable-device metadata queries and confirmed Windows PnP safe-removal requests. Exclude the
  Windows/Dako disks and dock nodes; recheck device identity, arrival and drive assignments without forced dismount or elevation.
- Keep metadata in memory, omit device identifiers/veto text from audit/ranking and prioritize typed Settings targets in labels.
- Preserve unsigned 3.9. Icon control, other remaining S1 work and S2–S8 are unfinished; no paid addition was made.

## 3.9.0 development — 2026-10-27 · S1 Settings and system routes

- Expand Windows shortcuts to 32 named pages and a validated typed ms-settings base-page source, with full-URI
  confirmation for typed links and no query persistence. Context-specific parameters remain unsupported.
- Add sleep, restart and dark-mode palette routes through existing handlers; block fixture power commands
  before any dialog or system action. Localize the palette cue and accessible name in resources.
- Keep transient Settings/file IDs unique across module re-enabling, reject oversized filename prefixes,
  retain copied launch arguments during shell message pumping, and report a full shelf without discarding items.
- Preserve unsigned 3.8. Remaining S1 tools and S2–S8 remain in progress; no paid addition was made.

## 3.8.0 development — 2026-10-26 · S1 shelf

- Add an optional memory-only OLE shelf inside the shared island for explicit text, link and local file-list drops.
  Keep incoming delayed rendering in the bounded helper, with cancellation and stale-clear rejection.
- Add item previews, clipboard copying and copy-only outgoing drag data. Original files are not read/moved/deleted.
  Clear parked records on lock/sleep/removal, and register the drop target only while its page is visible.
- Keep the shelf available across app focus changes for dragging. Add synthetic native receive/removal tests.
  Preserve unsigned 3.7; the complete V3_1 roadmap remains unfinished and no paid addition was made.

## 3.7.0 development — 2026-10-26 · S1 indexed files

- Add optional local Windows Search filename-prefix queries to the shared palette and island, with a
  cancellable debounce, bounded job-owned helper and no idle scan or search-history persistence.
- Validate local result paths and confirm script/installer/shortcut launches with their full paths.
  Keep a native file-picker fallback. Add synthetic result, path, stale-action and scaled UI checks.
- Preserve the unsigned 3.6 checkpoint. S1 and the broader V3_1 roadmap remain in progress; no paid additions.

## 3.6.0 development — 2026-10-26 · S1 privacy and cost accounting

- Add opt-in CurrentUser DPAPI clipboard snapshots, with memory as default, expiry across restarts,
  lock/sleep/removal erasure, restrictive failure handling and clear-during-transfer/save guards.
- Count live helper private commit and cumulative child/worker CPU on demand. Activity ceilings include the
  bounded helper process; they do not represent resident idle allocations.
- Add synthetic-only storage integration covering restart/pins, lock, removal, immediate shutdown and denied
  erasure/retry. The production executable also checks its helper IPC and teardown without clipboard IO.
- Preserve the unsigned 3.5 checkpoint. Indexed files, shelf, keep-awake rules and other roadmap work remain pending.

## 3.5.0 development — 2026-10-26 · S1 in progress

- Add optional local calculator, units and conservative Clean URL actions inside the palette/shared island.
- Add bounded memory clipboard history with source exclusion flags, text/image/file previews, pinning, search,
  expiry and lock/sleep clearing. Isolate delayed rendering in a cancellable owned helper process.
- Route the original clipboard preview through the same helper; preview/render fixtures perform no clipboard IO.
- Add searchable named snippets with transactional local settings and an explicit copy menu, plus timed keep-awake
  requests whose owning worker restores its previous execution state on stop/removal.
- Share palette sources through one catalog with bounded local usage ranking, excluding clipboard IDs and query results.
- Add data-boundary, transfer cancellation, native power restoration and scaled view checks. S1 is not complete;
  see docs/MODULES.md. No paid dependency or service has been added.

## 3.4.0 development — 2026-10-25 · S0 module foundation (partial)

- Start V3_1.md with module lifecycle, lazy factories, enabled-state action dispatch and startup cleanup.
- Add Features inside the shared island, presets, a scoped capability ledger and explicit own-cost refresh.
- Ship an optional Windows settings shortcuts module; disabled modules retain settings and unload their instances.
- Add a 50-cycle diagnostic with a real thread-local hook, worker, window and timer, plus resource baseline checks.
- Add resource-table strings and scaled/high-contrast/MSAA fixtures; preserve earlier v3 build folders.
- Track the remaining S0 requirements in docs/MODULES.md. Full privacy coverage and cost attribution remain pending.

## 3.3.0 — 2026-10-25 · Dock reconciliation and parity

- Replace the full-width bar with one transparent, content-fit rounded dock, finite hover magnification and capsule-only input.
- Require desktop consent for reservation/suppression; migrate bar preferences and keep the notch and isolated preview.
- Add a shared command palette, status choices, optional desktop/task-view icons and public recent/frequent app documents.
- Journal Dako-owned system settings before changes and restore through the existing exit/crash guard; delegate persistent pairing and connections to Windows Settings.
- Record the public badge/progress/custom-task and notification-listener limits; retain native status access.
- Extend fixtures, native recovery/input tests and preserve runnable 3.0–3.3 builds.

## 3.2.0 — 2026-10-25 · Controls, stats and wallpaper

- Add a real Windows dark-mode toggle, supported-display brightness, output-device list, radio control and native shortcuts.
- Add sixty visible seconds of CPU/RAM/network history, per-process CPU ranking with a memory toggle, fixed volumes and uptime/battery estimates.
- Replace the wallpaper filmstrip with a source-filtered grid, display targeting and a clearly global fit selector.
- Validate disconnected targets and attempt fit rollback on image failure; remove desktop capture from normal Apply.
- Keep automated control/wallpaper actions inert, add read-only native capability checks, and preserve earlier v3 builds.

## 3.1.0 — 2026-10-25 · Bar and running apps

- Default to a 40-DIP per-monitor AppBar, with upward-opening island panels and a retained notch option.
- Add grouped pinned/running apps, DWM previews, flash indicators, pin reordering, new-instance actions and keyboard/MSAA support.
- Extend crash recovery to remove AppBar reservations, with Explorer suppression remaining opt-in and no forced bar-mode work area.
- Handle fullscreen notifications, display changes and Explorer registration recovery through events.
- Add Running to Apps, live native bar status and 225 fixture renders; retain separate 3.0 and 3.1 runnable builds/packages.


## 3.0.0 — 2026-10-25 · Foundation and Today

- Split the entry point, island implementation, native services and five view implementations.
- Merge appearance, tasks, favorites and recents into a versioned atomic settings store with legacy migration.
- Remove the unused experimental rail window. Use wallpaper settings-change events and a mouse hot zone at rest.
- Share a fixed body height across five tabs, with a slim header and Power in the footer Menu.
- Rebuild Today with opt-in cached Open-Meteo weather, a month calendar and HTTPS ICS subscriptions, local events,
  twenty scrolling tasks, compact focus controls and a Windows Clock shortcut.
- Keep runnable milestone builds under `build/v3.0` and packages under `dist/v3.0`; add an isolated review launcher.
- Extend the original 127 render fixtures to 153 and add settings, weather, recurrence, eligibility and DPI tests.

## 2.0.0 — 2026-10-25

- Introduce a paper, ink and orange native interface with shared navigation across seven views.
- Add DirectComposition rendering with an opaque fallback, bounded frame scheduling and interruptible motion.
- Improve responsive layout, DPI/text scaling, native text fields, keyboard navigation and accessibility metadata.
- Keep normal startup independent of desktop replacement; taskbar suppression remains explicitly opt-in.
- Expand geometry, motion, async, fixture and native lifecycle verification.
- Release graphics resources when collapse completes after a stalled frame or with reduced motion.
- Add portable Windows x64 packaging with the offline wallpaper collection and SHA-256 checksums.

This is an early unsigned Windows build. Physical Wi-Fi/Bluetooth pairing and reconnection, mixed-monitor changes, a full screen-reader journey and complete GPU/DWM tracing remain acceptance work. The fixture suite does not certify those paths. See `docs/VERIFICATION.md` for current evidence and limits.
