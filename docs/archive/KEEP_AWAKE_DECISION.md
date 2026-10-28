> Historical record. The V4 foundation plan supersedes conflicting product instructions.

# Keep-awake rules — 3.11 development

Status: implemented for explicit-session monitoring; physical AC/display/lock acceptance remains manual.
No dependency, service purchase, power-plan change, lid override or administrator permission is needed.

The module saves four local rule preferences: require AC power, require an active external display, keep the
display awake, and up to eight exact executable file names. Every selected condition must match; any one
selected running application satisfies the application condition. An empty condition set cannot start.
Unknown/unavailable facts cannot satisfy a condition. Monitoring starts explicitly each app session; saved
preferences never silently restart monitoring. Stop, module removal and process exit release owned resources.

Changing preferences requires stopped monitoring. The module stages changes and commits them only after the
versioned settings store succeeds; a failed save leaves the previous configuration in use. Expanded capabilities
change its consent fingerprint, so earlier power-only consent cannot silently authorize process observation.
Preview, render fixtures and the generic module diagnostic refuse native monitoring/execution requests.

A dedicated named worker owns a hidden top-level window, power/session notifications, optional out-of-context
window lifecycle hook, and at most eight selected-process handles. It sleeps in a message/handle wait without an
idle timer. GetSystemPowerStatus supplies AC state; QueryDisplayConfig supplies bounded active topology on
start and display/device/resume notifications. Only recognized external connector technologies satisfy the
display condition; embedded/unknown/virtual outputs do not. A current-session Toolhelp snapshot discovers
already-running selected apps. Window create/show events discover later desktop app starts. The callback
only filters events, posts a window handle and measures its own work. Process identity queries happen
on the worker; command lines, window titles, document contents and accessibility objects are never requested.
Process exit handles trigger a bounded rescan to find another instance of a selected app. No periodic scan,
low-level input hook, WMI event subscription or elevated trace session is installed.

Limitations: exact executable names identify app processes, not app packages or documents. Protected processes
can be unavailable. Later windowless starts are not guaranteed to be discovered until monitoring restarts or
the session resumes. The app list is OR; other chosen criteria are AND. This is documented in the input help.

On each condition transition the owning worker updates its thread-scoped SetThreadExecutionState request.
It retains the previous state and restores it when the match ends, on lock/suspend and at shutdown. Windows
also releases the request if that thread/process exits. A slow callback or failed registration/request stops
the worker. Normal manual timed requests and rules are mutually exclusive. No sleep, screen-off or lid policy
is modified, so this feature has no persistent system preference requiring a crash-recovery journal.

The live ledger shows monitoring capabilities and the actual window hook/power request. Audit text names the
logical execution request, not selected app names, paths or IDs. Rule preferences are local settings and appear
in a settings export. Worker CPU and callback maximum are measured on demand; a conservative 2 MiB active
scratch/stack allowance fits within the 4 MiB module allocation ceiling. Shared OS/RPC memory is outside this
allocation estimate. Enabling the module alone starts no worker, hook, window, timer or power request.

Validation uses exhaustive three-state condition combinations, malformed/bounded app names, synthetic facts
through a real worker, paired native registration counters and 50 warmed unload cycles. A private synthetic
desktop child checks real start/exit observation. Manual timed request/restore tests remain in place. These
checks never change power preferences or operate another application's UI. Physical connector, lock/resume,
battery transitions and assistive-technology acceptance still require hardware/user testing.

Primary API contracts:

- [Thread-scoped execution state](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate)
- [Power setting notifications](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerpowersettingnotification)
- [Active display topology](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [Display connector technologies](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ne-wingdi-displayconfig_video_output_technology)
- [Out-of-context window event hooks](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook)
- [Message and process-handle waits](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex)
