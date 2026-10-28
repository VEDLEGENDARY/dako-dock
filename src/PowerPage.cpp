#include "Island.h"

void Island::Power() {
    const int actions[]{Lock, SleepNow, RestartNow, ShutdownNow};
    const wchar_t *labels[]{L"Lock", L"Sleep", L"Restart", L"Shut down"};
    const wchar_t *icons[]{L"\xE72E", L"\xE708", L"\xE777", L"\xE7E8"};
    auto b = Layout().body;
    int cols = Layout().narrow ? 2 : 4;
    for (int i = 0; i < 4; ++i) {
        auto r = layout::Column(b, i % cols, cols);
        r.top = b.top + (i / cols) * 100.0f;
        r.bottom = r.top + 88;
        auto rect = R(r);
        AddHit(actions[i], rect, labels[i]);
        Fill(Tactile(actions[i], rect), dock::Mix(theme::Card, theme::CardHover, HoverAmount(actions[i])), 14);
        Text(icons[i], {r.left, r.top + 8, r.right, r.top + 44}, type::Icon, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(labels[i], {r.left, r.top + 48, r.right, r.bottom - 8}, type::Control, theme::Text,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(actions[i], rect);
    }
}

void Island::PowerAction(int id) {
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        note = modules::String(IDS_POWER_INERT);
        return;
    }
    ModalGuard modal(modalOpen);
    const wchar_t *prompt = id == SleepNow     ? L"Put this PC to sleep?"
                            : id == RestartNow ? L"Restart Windows? Save your work before continuing."
                                               : L"Shut down Windows? Save your work before continuing.";
    if (MessageBoxW(window, prompt, L"Dako", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    if (id == SleepNow) {
        if (!SetSuspendState(FALSE, FALSE, FALSE))
            note = L"Windows could not put this PC to sleep.";
        return;
    }
    HANDLE token = nullptr;
    bool enabled = false;
    TOKEN_PRIVILEGES previous{};
    DWORD previousSize = sizeof(previous);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            SetLastError(ERROR_SUCCESS);
            enabled = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &previousSize) &&
                      GetLastError() == ERROR_SUCCESS;
        }
    }
    bool succeeded =
        enabled && ExitWindowsEx(id == RestartNow ? EWX_REBOOT : EWX_POWEROFF,
                                 SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
    if (token) {
        if (enabled)
            AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        CloseHandle(token);
    }
    if (!succeeded)
        note = L"Windows could not complete this power action.";
}
