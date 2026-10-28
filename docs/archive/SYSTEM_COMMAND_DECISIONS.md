> Historical record. The V4 foundation plan supersedes conflicting product instructions.

# S1 system-command decisions

## Current routes (3.9)

Sleep, restart and dark mode reuse the existing core handlers. Sleep/restart retain their explicit native
confirmation; theme changes retain the original session recovery journal. Preview/fixtures refuse power
operations before any dialog or setter. Tests do not sleep/restart this PC or change its theme.

The Settings module has named page navigation and a bounded typed page route. Typed links show the
complete URI for confirmation: some Windows routes initiate setup/update flows. Other schemes, unsupported
parameters, fragments, malformed escapes and control characters are rejected. Dako delegates the confirmed URI to Windows; page
availability and subsequent Windows behavior are not Dako-managed preference changes. The reference is
Microsoft's [Settings URI documentation](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-settings).

The 3.12 route supports these documented context parameters, one per link: defaultapps with registeredAppUser,
registeredAppMachine or registeredAUMID; appsfeatures-app with its bare package family name; camera with cameraId;
sound-properties with endpointId or interfaceId. Values are decoded once as strict UTF-8 and re-encoded as URI
components. Empty values, control/NUL characters, malformed UTF-8, additional query delimiters, unsupported keys,
decoded values over 512 UTF-16 units and whole URIs over 1,024 characters are rejected. Package family syntax is
checked with Microsoft's VerifyPackageFamilyName; this does not check whether a package is installed.

Canonical spelling is used for confirmation and palette matching; equivalent escaping keeps the same transient
target ID. Changing a target invalidates its prior ID. Device/app values stay in memory, are cleared with query
state and never enter ranking/settings/audit text. The palette edit accepts more than the model limit so an oversized
paste is rejected by validation instead of being silently shortened into another valid target. Normal app search
retains its original 120-character limit. This adds a Windows SDK import library, without a new external dependency.
No settings page or hardware operation is launched by automated checks; physical page availability remains manual.

Windows version/app/device support determines navigation. Default-app context links need a sufficiently updated
Windows 11 version; a device URI does not imply that Dako opens that device or reads its camera/audio stream.
Sources: [Default Apps context links](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings)
and [package-family syntax validation](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-verifypackagefamilyname).

## Desktop icon visibility spike (2026-10-27)

The standalone DesktopIconsProbe.exe reads only the desktop view's flags/options through IShellWindows,
IShellBrowser, IShellView and IFolderView2/IFolderViewOptions. It runs in an owned bounded helper. It has no
setter and does not enumerate desktop items, retrieve their names/positions, or access clipboard content.

The sandbox returned E_ACCESSDENIED. An approved run outside it returned success, a clean helper exit,
icons_hidden=0, custom_position_flag=0 and options_hresult=S_OK. This proves the public read path on this host.
It does not prove a reversible write path. Microsoft's SetCurrentFolderFlags documentation states that
Windows 7 and later require FVO_CUSTOMPOSITION; that option was absent in the read result.

Decision: do not force a layout-option change, change undocumented registry preferences, send a private
Explorer command or claim the setter works. Icon toggling remains pending a verified, reversible backend
with original-state capture and normal/crash/module-removal restoration. Windows' desktop View menu remains
the manual route. The other S1 commands can proceed independently.

References: [desktop view lookup](https://devblogs.microsoft.com/oldnewthing/20130318-00/?p=4933/),
[GetCurrentFolderFlags](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-getcurrentfolderflags)
and [SetCurrentFolderFlags](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-setcurrentfolderflags).

An additional 3.11 read-only spike queried the documented SHGetSetSettings/SSF_HIDEICONS field twice with
opposite initial bits. Both getters returned hidden=0, matching the live desktop-view getter, and both owned
helpers exited cleanly. It called no setter and enumerated no desktop items. This supplies another public
read path; it does not yet validate mutation, view refresh, failure detection or crash restoration. The void
Shell-state API would need an independent observer and a journal before any write experiment is permitted.
References: [SHGetSetSettings](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetsetsettings),
[SSF_HIDEICONS](https://learn.microsoft.com/en-us/windows/win32/shell/ssf-constants) and
[SHELLSTATE](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-shellstatea).

The standalone DesktopStateSpike now journals the original Boolean before mutation, starts an independent
parent-handle restorer in a bounded job (without kill-on-close), and validates both getter paths before arming.
It passes 50 normal and five forced-parent restoration cycles using private synthetic state, returning process
handles to baseline. Default execution never changes Windows preferences. Recovery completion preserves the
original metadata, including when a locked journal prevents completion from being saved.

An explicitly approved native run on this host did not verify a visible change from the public Shell setter
plus public IShellView::Refresh. It returned public_setter_observed=0, guard_restored=1 and
original_visible_state_verified=1. The original state was restored and confirmed through the independent
getters. No desktop item was enumerated and no clipboard content was accessed.

Decision after the write spike: keep desktop-icon toggling unavailable. This backend did not pass functional
acceptance here. Do not substitute private Explorer commands or force view-position options. The unused restoration prototype was removed during cleanup; future production reuse needs startup replay and failure
reporting review in addition to backend validation. The manual Windows desktop View menu remains the route.

## Recycle Bin and removable disks (3.10)

These are explicit user-data/device actions, not preferences for which a fake undo can be offered. Both are
separate optional modules and start with no scan, timer or helper at rest. Queries use bounded owned helpers.
Preview/diagnostic/render fixtures cannot delete/eject, even if an internal caller supplies a confirmed flag.

The Recycle Bin module reads item/byte totals without enumerating filenames. Emptying is offered after totals
are available. The action registry requires a full-target confirmation explaining permanent deletion for this
account on all drives and that totals are a snapshot. Only then does the module call SHEmptyRecycleBinW;
Dako supplies the confirmation and Windows supplies its operation/progress UI. The ledger records the logical
target/outcome, without filenames. This is not an immutable per-file preview: concurrent changes in Windows
are included in an explicit empty-all operation. Actual emptying remains manual acceptance work.

The drive module reads mounted disk numbers and present disk-interface metadata. Eligibility requires a
removable disk node, a verified arrival timestamp and mounted drive letters. Dock nodes and the Windows/Dako
disks are excluded. It does not walk upward to a hub/controller or use raw volume dismount/eject IOCTLs.
At invocation it re-enumerates and compares instance ID, arrival timestamp and drive assignments, rechecks
the devnode immediately before calling CM_Request_Device_EjectW, and lets Windows perform normal safe-removal
and veto handling. No privileges are enabled and no request is retried automatically. Unsupported devices,
missing identity/permissions and vetoes remain under Windows control, with a Windows review shortcut.

Instance identifiers and arrival data stay in memory. The ledger records the safe-removal request and drive
letters without device identifiers or private veto text; ephemeral target IDs are not learned. A committed
removal can complete after Dako closes and cannot be undone by Dako. Not all removable media/controllers
qualify as removable mounted disk nodes; actual hardware removal/interoperability remains manual acceptance.

The standalone read-only system probe succeeded on this host for both query APIs, with clean helper exits.
It prints only status/validation results and refuses mutation operations, including in its helper entry point.
It did not print file/device names, empty a bin or eject a device. Synthetic tests cover bounds, immutable
identity/arrival/assignment changes, confirmation metadata and fixture guards. Automated verification must
never empty the user's bin or eject a real drive.

References: [SHQueryRecycleBinW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shqueryrecyclebinw),
[SHEmptyRecycleBinW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shemptyrecyclebinw),
[device numbers](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_storage_get_device_number)
and [PnP safe removal](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_request_device_ejectw).
