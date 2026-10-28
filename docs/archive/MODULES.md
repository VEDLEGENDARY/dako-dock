> Historical record. The V4 foundation plan supersedes conflicting product instructions.

# Module foundation and S1 tools (3.12 development)

S0 and S1 work from V3_1.md lives here. The complete suite is not finished.
Open Features from the island footer Menu or search for Features with Ctrl+Alt+K.
`Dako.exe --preview --layout=dock --features` opens an isolated review. Preview changes stay in memory.

## Implemented

- A registry of descriptors, factories, versioned-store settings/defaults, capability declarations, budgets,
  actions and surface declarations. Disabled modules retain their descriptor/settings, without a live instance.
- Typed setting schemas validate booleans, bounded integers and bounded single-line text. Invalid saved values
  fall back to declared defaults. Capability consent is tied to the declared list, precedes construction and
  is saved atomically with enabled state. Import clears imported module consent.
- Explicit Start/Stop, reverse-order shutdown, startup-failure cleanup and immediate action removal.
  Invocation rechecks enabled state; a stale palette item cannot invoke an unloaded module.
- Reentrant Stop cannot destroy an in-flight action's object or reopen its view; cancelling Start prevents
  resurrection. Live declarations cannot be added while modules are running. Shared island pages provide
  accessible paged actions/readouts and search, with no additional bar.
- A Features view inside the existing island, with enable/remove, four presets, a module capability ledger
  and manually refreshed allocation, CPU and hook figures. No new strip, global input hook or periodic sampler is installed.
- Minimal disables optional modules. Taskbar enables Windows shortcuts. Power user adds clipboard history,
  snippets, shelf and keep-awake. Creator currently selects those tools; capture/color modules remain pending.
  Presets preserve existing dock preferences. First normal run opens the preset page.
- Windows shortcuts supplies 32 named Windows Settings links in the palette/shared island and a validated
  `ms-settings:page` source for other base page IDs. Typed links show the complete URI for confirmation, since
  some Windows links start setup/update flows. Page IDs are bounded ASCII. The 3.12 route also accepts documented
  app/device context parameters for defaultapps, appsfeatures-app, camera and sound-properties. Values use strict
  UTF-8 URI escaping and canonical spelling; package family syntax is validated with a Windows SDK API. Unknown
  parameters, additional query delimiters, fragments, malformed escapes and control characters are rejected.
  Whole links are bounded to 1,024 characters; decoded values to 512 UTF-16 units. Availability is determined by Windows.
  Typed results are ephemeral and excluded from learned ranking. The module has no worker, timer or hook.
  Named shortcuts navigate to known pages; subsequent Windows actions remain under Windows control.
- New user-facing strings live in ModuleStrings.rc. The view uses shared keyboard traversal, high contrast,
  reduced motion, text scaling, scrolling, MSAA names/roles/states and the compact native menu.
- `--module-check` runs every shipped module and a diagnostic 50 times. The diagnostic creates a worker,
  message-only window, timer and thread-specific WH_GETMESSAGE hook. The ledger records the live hook.
  It checks all resources disappear, module actions are inaccessible, and native process resource counts
  return to the warmed baseline. It never observes global keyboard/mouse input or changes Windows settings.
- Features → Capabilities has the web Offline switch, active core hook subscriptions and the last 128 request/
  write operations. Older omitted operations are counted. HTTP hosts and reasons are recorded, without private
  URL paths, query strings, bodies or setting values. Existing synchronous requests drain within their timeouts;
  an Offline/Online toggle never revives an invalidated request. Automatic redirects are disabled.
- The existing schema-3 store exports/imports one local file. Import validates the entire bounded file, keeps a
  backup, preserves desktop consent and cannot weaken Offline. Monitor/app overrides have stable encoded keys
  and precedence global → monitor → app → combined. Transient rename conflicts receive bounded retries.
- scripts/measure.ps1 -ModuleBudgets enforces the declared allocation, attributed CPU and callback budgets.
  The callback diagnostic performs real hook work and reports the observed maximum. Core process memory and
  shared/OS overhead are separate from explicitly owned module allocations.

The hook follows Microsoft's documented [thread-specific hook lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw).
This diagnostic is not an input-tool implementation or approval for S7.

## Limits and remaining roadmap work

- Existing dock, Today, controls and telemetry remain the original core services. The new utility catalog uses
  modules; baseline views have not been reorganized into optional modules. Their existing hook and write sites
  are now audited. Optional legacy-service migration requires preserving the shared core dependencies.
- Allocation metrics exclude allocator overhead, shared services and OS-owned memory; they are not total
  private RAM/working-set attribution. Module implementations must account for their own allocations and CPU.
  Clipboard transfer buffers, worker/child CPU and a live helper's private commit are attributed. Snapshot scratch
  is conservatively reserved during encryption. Helper metrics are queried on demand, without sampling timers.
  The transfer helper has a 64 MiB process commit ceiling; activity budgets are 72 MiB for utilities/snippets and
  80 MiB for clipboard, including that ceiling and parent buffers. These are limits, not idle allocations.
  Resource/budget tests do not replace actual mixed-DPI, screen-reader or provider hardware acceptance.
- Settings schemas and monitor/app override APIs are available. General settings editors and automatic context
  selection for every module remain module-specific work. No external binary plugin loading exists.
- The privacy ledger is bounded memory, not a persistent full-session file or OS security sandbox. Offline
  gates Dako web requests, not Windows networking, external applications or OS shell behavior.
- Shared island and palette routing are implemented. Dock glyph/HUD routing, CLI/URL dispatch and URL action
  confirmations remain later utility/automation work.
- The 3.5 work adds optional calculator/units/Clean URL, clipboard history, snippets and timed keep-awake modules.
  History defaults to bounded memory. The 3.6 work adds opt-in CurrentUser DPAPI snapshots and clear-on-lock/
  sleep/removal while Dako runs. A normal app shutdown retains opted-in history; age expiry includes time closed.
  Actual read/write/erasure failures disable persistence for the session and report any remaining encrypted file.
  Snippets persist as local settings. The snippet menu copies text for explicit
  user pasting; it installs no input hook. Keep-awake requests use one owned worker and a kernel timeout wait,
  with no polling timer. Stop restores that worker's previous execution state; no power preference is changed.
  See [the clipboard decision](CLIPBOARD_DECISION.md) for isolation, supported formats and privacy limitations.
  Core palette sources share one catalog with bounded local command-use ranking; ephemeral query/clipboard IDs
  are excluded. The optional Indexed files module adds local filename-prefix queries through the documented
  Windows Search/ADO interfaces. Type `files ` followed by at least two filename characters, or search its
  shared island page. A 200 ms cancellable debounce precedes a bounded job-owned helper; no query runs at rest.
  At most 50 local drive paths return. UNC/device paths and alternate streams are rejected. Scripts, installers
  and shortcuts require confirmation showing the full path. Queries/results stay in memory and never enter
  ranking/settings/audit text. Removing the module cancels the helper and clears results. A native file picker
  remains available if Search is unavailable. Files activity has a 72 MiB ceiling including its 64 MiB helper.
  The 3.8 shelf parks up to 25 dropped Unicode text/link or local file-list records in bounded memory. It uses
  the existing island and has no standalone bar. Incoming delayed rendering runs in the owned helper; the UI
  holds a locally generated COM marshal reference only until completion/cancellation. The shelf accepts COPY
  only, never requests a move, and never reads original file contents. Preview a record to copy it or drag the
  Copy button into another app. A keyboard text/link input and clipboard-copy alternative are provided.
  The shelf stays open across focus changes so Explorer can be used as a source, and clears on lock/sleep/removal.
  Its private session window owns no timer or worker at rest. OLE target registration exists only while its
  page is visible. Activity ceiling: 88 MiB including helper and conservative transient buffers. See SHELF_DECISION.md.
  The 3.9 palette also routes sleep, restart and dark mode through the existing confirmed/recoverable handlers.
  Preview/fixture power commands stop before a dialog or system call. Indexed-file and typed-Settings result IDs
  stay unique across re-enabling their modules; oversized file prefixes cancel instead of being truncated.
  The 3.10 work adds optional Recycle Bin and Removable drives modules. They query bounded metadata in an
  owned helper only after Refresh. Bin emptying is offered after totals are available and uses explicit
  full-target confirmation with the last-refreshed totals. Drive requests re-enumerate eligible removable
  disk nodes, exclude the Windows/Dako disks, and compare device ID, arrival time and drive assignments before
  asking Windows for safe removal. Windows can veto the request; Dako never forces a volume dismount, ejects
  a dock/controller, or enables privileges automatically. Device IDs stay in memory and are excluded from audit
  text/ranking. Data actions are blocked in preview/fixtures. They cannot offer a fake undo. See SYSTEM_COMMAND_DECISIONS.md.
  The 3.11 work adds explicitly started keep-awake rules. Choose AC power, an active external display and/or
  exact executable names; all selected conditions must match, with any selected running app satisfying the app
  condition. Optional display wake applies only while matched. Unknown state, lock/suspend or Stop releases the
  request. A worker owns event notifications and process-exit handles without an idle polling timer. Preferences
  save transactionally; monitoring never auto-starts on launch, and expanded capability consent is required.
  Native monitoring is blocked in preview/fixtures. Protected processes and later windowless starts can be
  unavailable. The module has a 4 MiB allocation ceiling with 2 MiB conservative active scratch/stack allowance.
  See KEEP_AWAKE_DECISION.md for API contracts and limitations.
  Icon visibility and direct insertion are deferred from the current S1 scope. The public icon setter failed
  the standalone functional spike with original-state restoration verified. A read-only UIA spike recognizes
  standard password controls but does not validate generic target-bound insertion. See SYSTEM_COMMAND_DECISIONS.md
  and INSERTION_DECISION.md. S2–S8, signing/update/install infrastructure,
  licensing and the listed risk decisions remain open. No paid service or dependency has been added.

The full V3_1 suite remains in progress. Do not present this development build as a completed or signed suite.

S2–S8 are future work; unused S2 model experiments were removed during repository cleanup.

## Validation

Run scripts/build.ps1, scripts/test-modules.ps1, scripts/test-motion.ps1, scripts/verify.ps1 and
scripts/test-live.ps1. Module tests use an isolated settings file under the build output. The production
diagnostic writes build/module-check.txt. Live verification invokes it before the existing harness.
scripts/test-file-search.ps1 checks synthetic path/query/IPC boundaries. Its optional -Probe performs a real
read-only random-name query; it prints only status/counts and never opens a file or reads the clipboard.
scripts/test-shelf.ps1 checks native OLE data transfer/registration, cancellation, stale clears and retained
drop-target references with synthetic sources. scripts/test-system-actions.ps1 checks metadata and device
identity boundaries; its optional -Probe reads real metadata without printing identifiers or permitting removal.
scripts/test-keep-awake.ps1 checks rule conditions, settings rollback, native subscriptions/process start/exit
and request restoration. A restricted sandbox can deny the unlocked-session query; the real process test then
requires an approved run outside it. Its synthetic resource cycles never change power preferences.
scripts/test-settings-pages.ps1 checks parameter routing, bounds, strict UTF-8 and canonical target identities without launching Settings.
Only the current build is retained; test outputs are isolated under build/tests.
See VERIFICATION.md for actual measured results.
