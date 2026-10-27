# Clipboard implementation decision — 2026-10-26

The S1 clipboard implementation uses documented Win32 clipboard, session and suspend APIs. It adds no
dependency, service subscription, telemetry or global input hook.

## Transfer isolation

`GetClipboardData` can invoke delayed rendering in another application. Dako therefore reads and writes
clipboard payloads in a short-lived copy of its own executable, outside the UI process's message handler.
A private unnamed inherited mapping carries at most 2 MiB. Only that mapping handle is inherited. An owned
job limits the child to one process and 64 MiB committed process memory and terminates it on timeout,
cancellation or parent closure. No copied text, paths or pixels enter command-line arguments or audit events.
The parent waits at most one second for a transfer, then terminates its own job and waits for teardown.
Process startup and Windows scheduling are outside that transfer deadline; the UI does not wait for transfer
completion. Module removal cancels and joins its worker before releasing resources.

The clipboard listener has a dedicated message loop. During a transfer its wait dispatches messages, so
lock and sleep notifications can cancel delayed rendering. The module never captures the clipboard that
existed before enablement. It verifies an unlocked session before capture and again before accepting data.
Unknown session state fails closed. A session lock, logoff, disconnect or suspend clears all records,
including pinned ones. Ten-minute expiry uses a timer only while unpinned records need expiry.

## Privacy and data bounds

Before requesting copied content, the helper checks `ExcludeClipboardContentFromMonitorProcessing`,
`CanIncludeInClipboardHistory` and Dako's own-copy marker. Excluded or denied copies are never retained.
These are source-supplied flags; Dako cannot identify a password when its source does not mark it private.
Copies made by Dako set exclusion, no-history and no-cloud flags before publishing the primary format.

History defaults to memory: at most 25 records, 8 MiB total, 2 MiB per item/transfer.
Supported image previews are bounded uncompressed 24/32-bit DIBs with standard RGB masks. Malformed,
compressed or oversized images and invalid file lists are rejected before rendering. File lists are parsed
without opening their paths. Text and file previews use bounded read-only accessible rows. There is no
automatic paste or background URL rewriting. An opt-in setting saves CurrentUser DPAPI snapshots in a separate
local file. Normal app exit retains opted-in history; expiry includes the closed interval. While Dako runs,
lock/sleep clearing erases the snapshot. Feature removal also erases it. IO failures clear memory, disable
persistence for the session and report any encrypted file that remains. A restrictive off policy is saved when
the settings file is writable. Source generations prevent delayed transfers/saves from undoing a clear request.

The original current-clipboard text preview uses the same helper and exclusion checks. Preview/render
fixtures perform no real clipboard reads or writes. New module tests use synthetic in-memory payloads.

## Measured probes and limits

- The native probe successfully registered/unregistered clipboard, current-session and suspend notifications.
- The sandbox denied the current-session state query (Win32 error 5). An approved read-only probe outside
  it returned the unlocked state. Both runs recorded zero clipboard-content reads and writes.
- Fixture IPC tests passed Unicode round-trip, a hung child timeout, cancellation, message-driven cancellation,
  single-transfer ownership and repeated handle cleanup. Malformed image/file payload tests also pass.
- Initial session/power registration bursts changed the process's shared resource baseline from 125 handles/
  4 threads to 127/5. After 50 warmup cycles and STA message servicing, the subsequent 50 cycles for each
  builtin returned to exactly 127 handles, zero GDI objects, one shared OLE USER object and five threads.
  Later runs showed total process thread counts can still vary independently. The diagnostic now reports
  those raw totals separately and requires zero named Dako module workers, exact handle/GDI/USER baselines,
  and paired successful removal of the clipboard class/window/listener/session/suspend/timer registrations.
- Real password-manager flags, clipboard interoperability, lock/suspend under delayed rendering and a physical
  screen reader still need manual acceptance. Fixture results do not claim those hardware/user-app results.

CurrentUser DPAPI serialization/storage primitives have separate synthetic tests, including tamper rejection,
expiry, atomic replacement and a failed-erasure case. Integration tests use a synthetic data source and real
storage in an explicitly isolated build folder. They cover pin/restart, simulated lock, module removal,
immediate off/clear followed by shutdown and erasure failure/retry. This mode installs no real clipboard
listener or hotkey and rejects clipboard writes; ordinary render fixtures still perform no storage IO.

The 3.6 own-cost page includes live helper private commit, queried only on demand, plus worker/helper CPU and
owned buffers. Its 72/80 MiB transfer ceilings include the existing 64 MiB child quota and parent/snapshot
buffers. Those are ceilings, not idle usage. The shipping-executable synthetic helper probe observed 2,404,352
bytes of private commit, cancellation/exit and an unchanged 146-handle parent baseline, with zero clipboard IO.

Microsoft references: [clipboard formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats),
[clipboard ownership](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations),
[format listener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener),
[session notifications](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsregistersessionnotification),
[session state](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ns-wtsapi32-wtsinfoex_level1_w).
