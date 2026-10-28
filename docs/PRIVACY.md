# Privacy

Dako has no telemetry, advertising, core account requirement or cloud synchronization. Its core dock, launcher,
controls and monitoring use local Windows APIs. No paid service has been added.

## Requests

Offline defaults On in V4, including migration. Weather is off until a city is entered and Online is enabled. Open-Meteo's geocoding host receives that city; its forecast host receives
the resolved coordinates. A successful forecast caches in the local settings file for 30 minutes. Calendar
subscriptions contact only the HTTPS URL explicitly entered by the user. No location permission or OAuth is used.

DakoSettings → Privacy and Features → Capabilities expose the Offline switch. It blocks new Dako web requests and invalidates existing request
generations. An operation already inside synchronous WinHTTP drains within the existing bounded timeouts. The UI
shows that draining state; toggling Online does not revive the invalidated request. Automatic redirects are
disabled, so a calendar subscription must use its final HTTPS URL. Offline does not disable Windows networking,
other applications, or the network behavior of an application opened from Dako.

The ledger keeps the last 128 operations in memory and reports how many older entries have been omitted. It
records requested HTTP hosts and reasons, file/registry locations and outcomes, and active hook subscriptions.
It never records request URL paths, query strings, response bodies, calendar titles, task values or setting
values. Windows may use its configured DNS and proxy infrastructure. No ledger contents are uploaded.

## Local files

- `%LOCALAPPDATA%\Dako\settings.ini`: appearance, tasks, pins, recents, weather cache, calendar configuration,
  module preferences/consent, Offline preference, snippets and bounded command-use counts. Private ICS URLs
  and snippets are stored here in plain text. Rankings contain stable command/app IDs, not search queries,
  clipboard text or calculated result text. Clipboard and temporary result IDs are excluded from learning.
- `%LOCALAPPDATA%\Dako\Recovery`: recovery journals for Dako-owned preference changes. These can contain original
  wallpaper paths, display/device identifiers and the original preference values needed for restoration.
- Original-wallpaper backup metadata and a fallback image can be retained under Dako's local data directory.
- An explicit settings export contains configuration, including private feed URLs, in plain text. The export
  dialog explains this. Import keeps a backup beside settings.ini before replacing preferences.
- Explicit diagnostics and verification modes write reports/images beside the executable or to the requested
  output directory. Preview startup writes a local startup trace. These writes are recorded by the audit runtime.

Files remain accessible to the user and other software running with that user's permissions. Keep exported
settings private. Removing an optional module preserves its preferences and releases its active resources.

Optional clipboard history stores up to 25 text/image/file records in memory, with 8 MiB total and 2 MiB per
item/transfer limits. It does not capture existing content at enablement. It honors source exclusion/no-history
flags before reading payloads. Dako cannot identify a password when the source does not mark its copy private.
Unpinned records expire after the configured 1–1440 minutes (default ten); all records, including pins, clear on lock, sleep or module removal.
Memory is the default. An explicit encrypted-history setting writes `%LOCALAPPDATA%\Dako\clipboard-history.dpapi`
using CurrentUser DPAPI. It retains opted-in records across a normal Dako exit; expiry includes the closed interval.
While Dako runs, lock/sleep clearing also erases its encrypted snapshot. Removing the feature erases saved history.
Storage failures disable persistence for the session and report any encrypted file that could not be erased.
If settings are also unwritable, Dako reports that failure; it cannot guarantee file erasure on an unwritable drive.
The local settings export contains the persistence preference, without the separate history snapshot.
Copies made by Dako request exclusion from clipboard history and cloud sync.
Preview and render fixtures do not read or write the real clipboard. [Implementation details](archive/CLIPBOARD_DECISION.md).

Snippet copying and result copying happen only on explicit action. Clipboard Enter/Paste supports the previously focused standard Windows Edit/RichEdit control. It rejects password, read-only, disabled, other-user/session, higher-integrity, stale or unfocused targets. It sends targeted WM_PASTE through the bounded helper, without global key injection. Unsupported editors use Copy and manual paste;
there is no automatic expansion, key capture or background URL rewriting. Recording and input remapping remain
later roadmap work.

Keep-awake rule preferences store up to eight executable file names locally and include them in settings exports.
Monitoring starts explicitly. It reads current-session selected process identities, AC state and active display
connector metadata; no command lines, window titles, document contents or input are captured. App identity results
remain in memory. The ledger records logical execution requests without app names or IDs. No periodic scan runs;
monitoring owns a window lifecycle hook only when an app condition is configured. Stop/removal releases it all.

Indexed files is optional and queries the local Windows Search filename index only after input, with no content
search, recursive disk scan or idle polling. Queries and up to 50 returned paths stay in memory. Private results
are excluded from usage ranking and audit text; leaving the palette cancels its pending query. The helper has
a bounded lifetime and commit quota. No HTTP request is made by this source. Opening a chosen file delegates
to its Windows handler; that application has its own behavior and permissions. Scripts, installers and shortcuts
ask for confirmation. The native picker is available when indexing is unavailable; remote/device paths are rejected.

The optional Shelf holds up to 25 dropped text/link/file-list records in memory, with an 8 MiB total and
2 MiB item/transfer bound. It keeps file paths, without reading, moving or deleting their contents. Explicit
incoming OLE rendering runs in the bounded helper. URLs stay as text until the user copies or drags them out;
Dako does not fetch or open them. The shelf is not saved/exported and its result IDs are excluded from ranking.
Lock, sleep, Clear shelf and module removal clear its records and reject pending old data. Receiving works
only while the shelf page is open; it stays visible when another app receives focus for dragging. Preview/render
fixtures use synthetic records and do not receive real drops. Copying an item explicitly replaces the clipboard
through the same exclusion-marked copy helper; dragging provides it to the destination application as COPY only.

Windows Settings links are explicit launches of the native Settings application. Typed base-page links show
their URI for confirmation, because some Windows URI routes can initiate setup or update flows. Documented
app/device parameters for Default apps, App features, Camera and Sound properties are supported; unsupported
parameters are rejected. Parameter values and typed links stay in memory and transient result IDs are not learned.
No app/device value is written to the audit log or read from a registry/device list for these routes. Windows determines whether
a page exists and controls its subsequent behavior, including its own networking. The Dako Offline switch
controls Dako web requests; it does not control a separately launched Windows Settings application.

Recycle Bin totals and removable-device metadata refresh only after an explicit action and are not saved.
Dako does not enumerate Recycle Bin filenames. Emptying this account's bin on all drives requires confirmation
showing the last-refreshed totals; contents can change in Windows before the operation executes. Native Windows
deletion is permanent and Dako cannot undo it. Preview/fixtures never call the deletion API.

Drive metadata can include hardware instance identifiers, arrival timestamps, device descriptions and drive
assignments, held only in memory. Transient device action IDs are not learned. The ledger records the logical
safe-removal request and drive letters without the device identifier or Windows' private veto text. Eligible
removable disk nodes are checked again at invocation, including arrival identity and drive assignments; the
Windows and Dako disks are excluded. Windows' PnP safe-removal protocol decides whether removal is allowed.
There is no forced dismount, automatic elevation or automatic retry. Preview/fixtures cannot eject a device.
An already-started removal can finish if Dako closes; Dako cannot reconnect it. Native Windows APIs manage
their own underlying filesystem/device activity; these are explicit user-data actions, not restorable preferences.

## Capability model and permissions


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

## Security and reporting


Dako is native C++20/Win32 code with no embedded web engine, script runtime, server or external binary plugin
loader. Network responses and imported settings are untrusted input. HTTPS certificate checks remain enabled;
automatic redirects are disabled, responses/imports are bounded to 2 MiB, and settings imports validate their
format before committing. Private request URLs and setting values are excluded from the audit ledger.

Optional capabilities require module consent. Imported consent is discarded. Desktop suppression has its own
consent and recovery guard; importing preferences cannot grant that consent. Managed preference writes are
journaled before application. Unknown future settings schemas are not overwritten. Hook installation/removal
and native resource baselines are exercised by --module-check.

No paid signing service or certificate has been purchased. Development executables remain unsigned. Trusted
release signing, signed update/rollback, installer distribution and their security review remain release work;
an unsigned development ZIP must not be described as a trusted signed release.

## Reporting

A private security-reporting address has not yet been configured by the maintainer. A public release needs an
approved private reporting contact. Do not publish secrets, private ICS URLs, recovery contents or exploit
details in public issues. Provide the affected version and a minimal reproduction through the eventual private
contact. No license or pricing decision is inferred from the instruction to avoid paid services.

## V4 speed test and settings diagnostics

Speed testing is off by default and requires Online plus a separate opt-in. Only clicking Run starts a test.
Cloudflare's public endpoint receives an 8 MiB download request and a 1 MiB zero-filled upload, with normal
HTTPS/IP/DNS metadata. No clipboard, tasks, files or personal payload is sent. The result is a bounded
throughput estimate, not a full network benchmark; it varies with route, server and test length. Requests
use finite timeouts, reject redirects and are cancelled/invalidated on Offline or teardown. No paid account
or API key is needed. This feature does not run in render fixtures or isolated preview.

The standalone settings window uses local user-scoped IPC only. No settings values are sent over the network.
Diagnostics export includes non-text preferences and runtime version information, omitting private paths,
URLs, city, app lists and other text settings. Settings export deliberately includes the full preferences,
so use Diagnostics for bug reports. The start-with-Windows preference manages this user's Dako Run value.