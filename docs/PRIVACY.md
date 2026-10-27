# Privacy

Dako has no telemetry, advertising, core account requirement or cloud synchronization. Its core dock, launcher,
controls and monitoring use local Windows APIs. No paid service has been added.

## Requests

Weather is off until a city is entered. Open-Meteo's geocoding host receives that city; its forecast host receives
the resolved coordinates. A successful forecast caches in the local settings file for 30 minutes. Calendar
subscriptions contact only the HTTPS URL explicitly entered by the user. No location permission or OAuth is used.

Features → Capabilities has an Offline switch. It blocks new Dako web requests and invalidates existing request
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
Unpinned records expire after ten minutes; all records, including pins, clear on lock, sleep or module removal.
Memory is the default. An explicit encrypted-history setting writes `%LOCALAPPDATA%\Dako\clipboard-history.dpapi`
using CurrentUser DPAPI. It retains opted-in records across a normal Dako exit; expiry includes the closed interval.
While Dako runs, lock/sleep clearing also erases its encrypted snapshot. Removing the feature erases saved history.
Storage failures disable persistence for the session and report any encrypted file that could not be erased.
If settings are also unwritable, Dako reports that failure; it cannot guarantee file erasure on an unwritable drive.
The local settings export contains the persistence preference, without the separate history snapshot.
Copies made by Dako request exclusion from clipboard history and cloud sync.
Preview and render fixtures do not read or write the real clipboard. [Implementation details](CLIPBOARD_DECISION.md).

Snippet copying and result copying happen only on explicit action. Users paste into their destination themselves;
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
