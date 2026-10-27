# Memory-only shelf (3.8 development)

The optional shelf lives in the existing island, with no additional bar or floating window. Open it from
Features/the command palette. It remains visible across app focus changes so a file can be selected in
Explorer and dropped onto it. Escape/Back or another view leaves the shelf normally. Its OLE drop target is
registered only while that page is visible; removal revokes it. A separate message-only window receives lock
and suspend notifications, without an idle timer or worker. Preview/render fixtures are inert.

The shelf parks Unicode text, links as text, and local Unicode HDROP file lists. At most 25 records and
8 MiB are retained, with a 2 MiB item/transfer limit. A full shelf rejects new items rather than discarding
parked records. It never reads file contents, moves/deletes an original, fetches a URL or opens a dropped item.
UNC/device paths and alternate streams are rejected. Virtual files requiring streamed content are unsupported.
Clear, lock, sleep and feature removal clear records and reject late transfer results. Nothing is persisted.

Incoming data rendering uses the existing job-owned helper, with a two-second operation bound, 64 MiB helper
commit ceiling and cancellation on clear/removal. The UI marshals the explicitly dropped IDataObject into a
local, bounded packet and holds a TABLESTRONG reference until completion. Only this locally generated packet
is passed through the private inherited mapping. No marshal data is loaded from user files or URLs. Marshal
references are released in the originating apartment, detached before COM calls to tolerate reentrant Stop.
State remains alive across COM message pumping; a stale external drop-target reference cannot revive a removed
module. This isolates GetData rendering; it is not a security sandbox for a native COM provider or for Windows.

Preview an item to copy it or hold and drag its Copy button. Small pointer movement retains ordinary click
behavior; dragging starts after the Windows drag threshold, with no blocking DragDetect loop. Outgoing OLE
data advertises COPY only, including a preferred-copy effect. It provides text, HTTP(S) URL or local HDROP data
plus no-history/no-sync metadata. A clear, removal or lock cancels an ongoing source on its next OLE callback.
The destination application's handling of supplied data is outside Dako's control. Keyboard text/link input
and explicit clipboard copying provide an alternative to dragging. Clipboard copies use the existing isolated
writer and its exclusion flags. Shelf IDs/content are excluded from learned ranking and audit text.

The 88 MiB activity budget includes the helper ceiling, retained metadata and conservative transfer/drag
buffers; the helper is absent at rest. Transfer/child CPU, incoming drop-handler work and temporary outgoing
buffers are attributed. Windows/OLE shared allocations remain outside explicit allocation attribution.

Automated checks use owned synthetic text/links and nonexistent fixture file paths. They cover real COM
marshaling/helper rendering, padded Unicode HGLOBALs, native registration, asynchronous completion, clear/lock/
hidden gates, move rejection, cancellation/removal and stale COM references. The native island harness checks
focus retention and pointer-threshold routing with IO kept inert. Actual Explorer/browser/third-party drop
interoperability and a complete physical screen-reader journey remain manual acceptance work.

References: Microsoft [OLE drop registration](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-registerdragdrop),
[marshaling flags](https://learn.microsoft.com/en-us/windows/win32/api/wtypesbase/ne-wtypesbase-mshlflags),
[marshal-data release](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coreleasemarshaldata),
and [OLE dragging](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop).
