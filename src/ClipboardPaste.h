#pragma once
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

namespace clipboard {
// Targeted WM_PASTE avoids global keystroke injection and never retargets after capture.
inline bool PasteTarget(HWND edit, DWORD expectedProcess = 0) {
    if (!IsWindow(edit) || !IsWindowEnabled(edit) || !IsWindowVisible(edit))
        return false;
    wchar_t name[64]{};
    GetClassNameW(edit, name, 64);
    if (_wcsicmp(name, L"Edit") && _wcsicmp(name, L"RichEdit20W") && _wcsicmp(name, L"RICHEDIT50W"))
        return false;
    auto style = GetWindowLongPtrW(edit, GWL_STYLE);
    if (style & (ES_PASSWORD | ES_READONLY))
        return false;
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(edit, &process);
    if (!thread || (expectedProcess && process != expectedProcess))
        return false;
    DWORD session = 0, ours = 0;
    if (!ProcessIdToSessionId(process, &session) || !ProcessIdToSessionId(GetCurrentProcessId(), &ours) ||
        session != ours)
        return false;
    HANDLE other = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    HANDLE token = nullptr, own = nullptr;
    bool allowed = other && OpenProcessToken(other, TOKEN_QUERY, &token) &&
                   OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own);
    if (allowed) {
        BYTE user[1024]{}, ownUser[1024]{}, level[1024]{}, ownLevel[1024]{};
        DWORD count = 0;
        allowed = GetTokenInformation(token, TokenUser, user, sizeof(user), &count) &&
                  GetTokenInformation(own, TokenUser, ownUser, sizeof(ownUser), &count) &&
                  EqualSid(reinterpret_cast<TOKEN_USER *>(user)->User.Sid,
                           reinterpret_cast<TOKEN_USER *>(ownUser)->User.Sid) &&
                  GetTokenInformation(token, TokenIntegrityLevel, level, sizeof(level), &count) &&
                  GetTokenInformation(own, TokenIntegrityLevel, ownLevel, sizeof(ownLevel), &count);
        if (allowed) {
            auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(level)->Label.Sid;
            auto ownSid = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(ownLevel)->Label.Sid;
            allowed = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1) <=
                      *GetSidSubAuthority(ownSid, *GetSidSubAuthorityCount(ownSid) - 1);
        }
    }
    if (own)
        CloseHandle(own);
    if (token)
        CloseHandle(token);
    if (other)
        CloseHandle(other);
    return allowed;
}
inline HWND CapturePasteTarget() {
    HWND foreground = GetForegroundWindow();
    GUITHREADINFO info{sizeof(info)};
    DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    return thread && GetGUIThreadInfo(thread, &info) && PasteTarget(info.hwndFocus) ? info.hwndFocus : nullptr;
}
inline bool PasteFocused(HWND edit, DWORD process, DWORD sequence) {
    if (!sequence || GetClipboardSequenceNumber() != sequence || !PasteTarget(edit, process) ||
        GetAncestor(edit, GA_ROOT) != GetForegroundWindow())
        return false;
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(edit, nullptr), &info) || info.hwndFocus != edit)
        return false;
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(edit, WM_PASTE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &result) != 0;
}
} // namespace clipboard
