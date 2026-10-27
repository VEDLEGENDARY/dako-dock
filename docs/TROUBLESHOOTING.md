# Troubleshooting

## Optional commands are missing

Open Features from the footer Menu or Ctrl+Alt+K. Minimal disables optional modules. Enable Windows shortcuts
to add its three Settings links and its shared island page. Removing a module removes its actions immediately.
The optional module preferences do not replace the existing desktop/taskbar consent choice.

## Weather or calendar does not refresh

Check Features → Capabilities → Offline. A previously issued request can take a few seconds to drain after
Offline is enabled. Weather still requires an entered city, and a calendar feed requires an entered HTTPS URL.
Redirects are not followed: supply the final HTTPS feed URL. Reopen Today after changing the source or Offline
state. Do not paste private feed URLs into bug reports. Provider/network failures preserve local calendar events.

## Settings export or import fails

Use a writable location. The file must be a Dako export with schema version 3; malformed, oversized, duplicate-key
and unsupported-schema files are rejected. Import retains a backup and preserves desktop consent and an active
Offline preference. Restart Dako to apply imported module/view preferences. Brief atomic-rename sharing conflicts
are retried; a persistent write error is shown and the previous committed file is retained.

If settings.ini itself is corrupt, close Dako and keep a separate copy before moving that file aside. Dako can
then create a fresh store, after which a known-good export can be imported. Never discard the original file or
recovery journals while investigating a failed save or restore.

## A Windows preference was not restored

Reconnect the relevant hardware and inspect `%LOCALAPPDATA%\Dako\Recovery`. The existing `--restore-session="path"`
mode retries a retained journal after its owning process has exited. Do not edit a recovery record or copy it
outside that directory. Disconnected devices and driver failures can prevent restoration.

## Verification and cost figures

Run scripts/test-modules.ps1 for lifecycle, schema, consent, Offline and import/export checks. Run
`scripts/measure.ps1 -ModuleBudgets -Seconds 60` for module allocation/CPU/hook budgets. The dashboard reports
explicitly owned allocations and attributed module CPU, excluding shared services, allocator overhead and OS
memory. These values are not the total process working set. Ordinary scripts/measure.ps1 measures the process.

Use build/Features.cmd for a non-reserving preview. Preview does not save imported/exported configuration
or enable desktop suppression. Native metadata tests do not replace a full screen-reader or hardware check.

## Clipboard history is empty

Enable it in Features and accept its capability explanations. Preview/render modes cannot capture the clipboard.
History starts with new copies, and honors source exclusion/no-history flags. Unsupported or larger-than-2-MiB
payloads are skipped. If session state cannot be verified, capture waits for a verified unlock notification.
Another app may own Ctrl+Alt+V; the palette can still open history. Copies made by Dako are excluded from capture.

Encrypted persistence is opt-in. If its file is unreadable/unwritable or erasure fails, Dako switches to memory
for the session and reports any remaining encrypted file. Release any backup/editor lock and use Clear history
to retry. The file is clipboard-history.dpapi beside settings.ini; its owned encrypted staging files use the
same filename followed by .tmp-. A settings-write failure is also reported instead of claiming the policy saved.

## Keep awake does not start

Preview cannot hold execution requests. In a normal instance, enable the feature and choose a duration. It prevents
idle sleep only; Windows still handles manual sleep and lid actions. Stop or remove the feature to release its request.
For rules, choose AC power, an external display and/or executable names, then choose Start keep-awake rule monitoring.
Every chosen condition must match; any one selected running app satisfies the app condition. Stop before editing.
Unknown power/display state, lock and sleep release the request. Monitoring must be started again after launching
Dako. Protected app identities, virtual/unknown display connectors and later windowless app starts can be unavailable.
Use exact file names such as editor.exe, separated by semicolons, without paths, arguments or wildcards.

## Indexed files has no matches

Enable Indexed files in Features. Type `files ` and at least two characters from the beginning of a filename,
or use its island search. Windows Search must be running and its index must include that folder. This source
does not scan the drive or search file contents. Results can arrive asynchronously; changing/clearing the query
cancels the previous request. An unavailable or slow provider produces an error with a native file-picker fallback.
Only local drive paths are accepted. Script, installer and shortcut launches ask for confirmation.

## A shelf drop is unavailable

Open Shelf in a normal instance and keep its page open. Preview uses synthetic data and rejects real drops.
Only Unicode text/links and local file lists are supported; virtual files/streamed attachments, remote paths,
oversized items and a full shelf are rejected. Original files remain untouched. The source app must remain
available while its explicit drop is rendered. Slow rendering times out; retry from the source or use Park text
or a link. Preview a parked item to copy it, or hold and drag the Copy button. Back/Escape leaves the shelf.

## A Settings page is unavailable

Enable Windows shortcuts for named page commands. Type `ms-settings:page` in the palette, or use Open a Windows
Settings link to enter a page ID. Typed links show a confirmation. Page IDs use letters, digits and hyphens.
Documented Default apps, App features, Camera and Sound properties links accept one context parameter; values
must be URI-escaped UTF-8. Other parameters/schemes, malformed escapes and oversized links are rejected.
Default-app context links require a sufficiently updated Windows 11 version. A page can be absent or redirect on
another Windows version or when its hardware is unavailable. Settings runs under Windows' own behavior.

## Recycle Bin or removable drives is unavailable

Enable the corresponding feature and use Refresh. There is no idle scan. Recycle Bin emptying appears only
after totals are available and requires confirmation; review its contents in Windows first. Preview cannot delete.
Removable drives lists only eligible mounted removable disk nodes with verifiable arrival identity. The Windows
and Dako disks, docks/controllers and unverified devices are excluded. Windows can veto a busy device or require
different permissions. Close its files, refresh and retry manually, or use Windows' own eject control. Dako never
forces a dismount or elevates automatically. Preview cannot eject; a completed removal cannot be undone by Dako.
