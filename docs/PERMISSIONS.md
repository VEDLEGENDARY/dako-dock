# Permissions and capabilities

Windows does not display a permission popup for every native API. Dako therefore declares optional module
capabilities and explains them before enabling a module that needs them. Consent is tied to the exact declared
capability list, with one explanation screen per capability. An import clears imported module consent;
a changed capability list needs fresh consent.
Removing a module releases its live resources. First-run presets do not grant Windows taskbar consent.

| Capability | Meaning |
|---|---|
| `keyboard-hook`, `mouse-hook` | Observe input while the enabled feature needs it. No S0 module installs either. |
| `clipboard-listener` | Observe newly copied text, supported images and file lists while history is enabled. |
| `clipboard-write` | Replace clipboard contents after an explicit copy action. Dako adds exclusion/no-sync flags. |
| `clipboard-read` | Read the current item for the original explicit preview through the bounded helper. |
| `file-index-read` | Query local indexed filenames after input; query text and result paths are not saved. |
| `ole-drag-drop` | Receive explicit text/link/file-metadata drops on the shelf page and drag selected items out. No source move or file-content read. |
| `session-events` | Listen for lock/sleep events to clear parked shelf records. No polling or input hook. |
| `recycle-bin` | Read totals on request and permanently empty this account's bin only after full-target confirmation. No undo is promised. |
| `screen-capture` | Read pixels for an explicitly requested capture. Capture is not part of S0. |
| `network:<host>` | Contact a declared host for an opted-in feature; Offline blocks web requests. |
| `file-write:<path>` | Write at the listed location; the ledger shows locations without file contents. |
| `registry-write:<key>` | Write a Windows preference; managed session changes retain their recovery records. |
| `needs-admin` | An operation needs administrator rights; ordinary Dako startup does not elevate. |
| `power-state` | Observe power conditions or hold a temporary keep-awake request. |
| `device-control` | Read or control a device through Windows. Unsupported controls remain unavailable. |
| `window-event-hook` | Observe window lifecycle/focus events. This does not install a low-level input hook. |
| `process-query` | Read selected executable identities in the current session and wait for process exit while rules run. |

The existing dock/notch and desktop recovery code use window-event and shell subscriptions. Their active
subscriptions now appear in the live ledger. The optional Windows shortcuts module installs no subscription,
uses no network and writes no Windows preference; it opens the native Settings application on explicit action.

The versioned settings store, recovery journals, wallpaper backup and diagnostic writers record their write
locations. Dark-mode writes target both `AppsUseLightTheme` and `SystemUsesLightTheme` under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`. Dako-owned theme, audio, radio, brightness
and wallpaper changes retain the existing restoration guard. Pairing and network credentials remain managed
in Windows Settings. Taskbar suppression requires its separate explicit desktop consent.

Keep awake holds a temporary request on its own worker thread. Stop, expiry or module removal restores that
thread's previous execution state. Windows releases a thread-scoped request when its owner exits. Dako does
not change lid, sleep or display preference values for this feature. See Microsoft's
[execution-state API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate).

Rule monitoring starts explicitly and adds session/power/display subscriptions, selected-process queries and
an optional window-event hook. Updated module consent covers these capabilities. Stopping monitoring closes
the worker and registrations. Saved rule settings do not start monitoring on launch. See KEEP_AWAKE_DECISION.md.

Removable drives uses `device-control` only during explicit metadata/removal requests. It excludes the Windows
and Dako disks, validates identity/arrival/drive assignments and asks Windows for normal PnP safe removal.
Windows can veto a busy/unauthorized device. No raw volume dismount, privilege enablement or automatic retry is
performed. Preview/fixtures block removal. A committed data action cannot be reversed like a preference.

This development build's ledger is bounded session history, not an operating-system security boundary. Builtin
modules are trusted native code; external binary plugins are not supported. Later modules must use the shared
network, write and capability services rather than bypassing them.
