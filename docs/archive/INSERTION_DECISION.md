> Historical record. The V4 foundation plan supersedes conflicting product instructions.

# Clipboard/snippet insertion decision — 2026-10-27

Status: direct insertion and automatic trigger expansion are deferred. Explicit clipboard copying and the
user's own paste remain the supported path. This is not a claim that direct insertion has been implemented.

The standalone InputTargetSpike creates two owned, empty Win32 EDIT controls, one with ES_PASSWORD, and
queries only CurrentIsPassword through UI Automation on a separate MTA worker while the owning UI thread
serves messages. The bounded probe passed with S_OK, plain_is_password=0 and protected_is_password=1.
It does not inspect the global focused element, another application's controls, text values or clipboard
content; it sends no keys. This verifies recognition for these standard controls, not for all applications.

Microsoft documents IsPassword as a property describing protected content. UI Automation clients must use
appropriate threading to avoid hanging their own UI. SendInput injects into the input stream, is subject to
UIPI, and can be affected by already-pressed keys; its call does not accept a target HWND.

Inference: a password-property read followed by a separate focus/foreground transition and asynchronous
clipboard operation does not establish a universal, atomic guarantee that a subsequent paste reaches the
same safe field. This spike also provides no acceptance evidence for custom secure editors, password
managers, elevated windows or antivirus heuristics. Replacing an entire UI Automation Value property would
not provide general insertion-at-caret semantics either.

Decision: keep clipboard history and snippets on their explicit copy path. Plain-text copying supplies text
for manual pasting; it does not activate or type into another app. Keep automatic expansion/input delivery
out of this checkpoint. A future S7 implementation must validate target identity, secure/custom fields,
privilege boundaries, modifier state, cancellation and the panic/unload path before enabling delivery.
No low-level input hook, input broker, elevated helper or paid dependency was added for this spike.

Sources: [UI Automation password property](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationclient/nf-uiautomationclient-iuiautomationelement-get_currentispassword),
[UI Automation threading](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-threading) and
[SendInput contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).
