#define UNICODE
#define _UNICODE
#include "../src/ClipboardPaste.h"
#include <cassert>
#include <iostream>
static WNDPROC original = nullptr;
static unsigned pasteMessages = 0;
LRESULT CALLBACK Capture(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PASTE) {
        ++pasteMessages;
        return 0;
    }
    return CallWindowProcW(original, window, message, wparam, lparam);
}
int main() {
    auto parent = CreateWindowExW(0, L"STATIC", L"Owned paste test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 240, 160,
                                  nullptr, nullptr, nullptr, nullptr);
    auto edit =
        CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 24, parent, nullptr, nullptr, nullptr);
    auto password = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_PASSWORD, 0, 30, 100, 24, parent,
                                    nullptr, nullptr, nullptr);
    auto readonly = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_READONLY, 0, 60, 100, 24, parent,
                                    nullptr, nullptr, nullptr);
    assert(parent && edit && password && readonly);
    assert(clipboard::PasteTarget(edit, GetCurrentProcessId()));
    assert(!clipboard::PasteTarget(edit, GetCurrentProcessId() + 1));
    assert(!clipboard::PasteTarget(password));
    assert(!clipboard::PasteTarget(readonly));
    assert(!clipboard::PasteTarget(parent));
    assert(!clipboard::PasteFocused(edit, GetCurrentProcessId(), 0));
    original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Capture)));
    SetForegroundWindow(parent);
    SetFocus(edit);
    DWORD sequence = GetClipboardSequenceNumber();
    // This owned subclass intercepts WM_PASTE, so no clipboard payload is accessed.
    if (sequence && GetForegroundWindow() == parent) {
        assert(clipboard::CapturePasteTarget() == edit);
        assert(!clipboard::PasteFocused(edit, GetCurrentProcessId(), sequence + 1));
        assert(clipboard::PasteFocused(edit, GetCurrentProcessId(), sequence));
        assert(pasteMessages == 1);
    }
    EnableWindow(edit, FALSE);
    assert(!clipboard::PasteTarget(edit));
    DestroyWindow(parent);
    assert(!clipboard::PasteTarget(edit));
    std::cout << "PASS targeted paste: identity, standard edit, protected/read-only/disabled/stale target rejection; "
                 "no clipboard IO\n";
}
