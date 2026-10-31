#include "Island.h"

void Island::Power() {
    const int actions[]{Lock, SleepNow, RestartNow, ShutdownNow};
    const wchar_t *labels[]{L"Lock", L"Sleep", L"Restart", L"Shut down"};
    const wchar_t *hints[]{L"Lock this PC", L"Resume where you left off", L"Restart Windows", L"Turn off this PC"};
    const wchar_t *icons[]{L"\xE72E", L"\xE708", L"\xE777", L"\xE7E8"};
    auto rows = layout::Rows(layout::Canvas(Layout()), {1, 1});
    for (int i = 0; i < 4; ++i) {
        PaintGroup reveal(*this, i);
        auto r = layout::Columns(rows[i / 2], {1, 1})[i % 2];
        auto rect = Tactile(actions[i], R(r));
        AddHit(actions[i], R(r), std::wstring(labels[i]) + L". " + hints[i]);
        Card(rect, 1, HoverAmount(actions[i]));
        unsigned color = i >= 2 ? dock::Mix(theme::Text, theme::Danger, HoverAmount(actions[i])) : theme::Text;
        Text(icons[i], {rect.left, rect.top + 16, rect.right, rect.top + 56}, type::LargeIcon, color,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(labels[i], {rect.left + 12, rect.top + 64, rect.right - 12, rect.top + 88}, type::Title, color,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(hints[i], {rect.left + 12, rect.top + 92, rect.right - 12, rect.bottom - 12}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(actions[i], R(r), shape::Card);
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
