#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <vector>

inline std::optional<std::wstring> PromptText(HWND owner, const std::wstring& title,
    const std::wstring& help, const std::wstring& initial, size_t limit = 2048) {
    struct State { std::wstring help, value; size_t limit; bool accepted = false; } state{help, initial, limit};
    std::vector<WORD> words;
    auto word = [&](WORD value) { words.push_back(value); };
    auto dword = [&](DWORD value) { word(LOWORD(value)); word(HIWORD(value)); };
    auto string = [&](const std::wstring& value) { for (wchar_t c : value) word(c); word(0); };
    dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    dword(0); word(4); word(0); word(0); word(300); word(130);
    word(0); word(0); string(title); word(9); string(L"Segoe UI");
    auto item = [&](DWORD style, short x, short y, short w, short h, WORD id, WORD type,
        const std::wstring& text) {
        if (words.size() % 2) word(0);
        dword(style | WS_CHILD | WS_VISIBLE); dword(0);
        word(x); word(y); word(w); word(h); word(id); word(0xFFFF); word(type); string(text); word(0);
    };
    item(SS_LEFT, 12, 10, 276, 46, 10, 0x82, help);
    item(WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 12, 62, 276, 18, 11, 0x81, initial);
    item(WS_TABSTOP | BS_DEFPUSHBUTTON, 158, 96, 62, 22, IDOK, 0x80, L"Save");
    item(WS_TABSTOP, 226, 96, 62, 22, IDCANCEL, 0x80, L"Cancel");
    auto procedure = [](HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) -> INT_PTR {
        auto* data = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (message == WM_INITDIALOG) {
            data = reinterpret_cast<State*>(lparam);
            SetWindowLongPtrW(dialog, DWLP_USER, lparam);
            SendDlgItemMessageW(dialog, 11, EM_SETLIMITTEXT, data->limit, 0);
            SetFocus(GetDlgItem(dialog, 11)); SendDlgItemMessageW(dialog, 11, EM_SETSEL, 0, -1);
            return FALSE;
        }
        if (message == WM_COMMAND && data) {
            if (LOWORD(wparam) == IDOK) {
                std::vector<wchar_t> value(data->limit + 1);
                GetDlgItemTextW(dialog, 11, value.data(), static_cast<int>(value.size()));
                data->value = value.data(); data->accepted = true; EndDialog(dialog, IDOK); return TRUE;
            }
            if (LOWORD(wparam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        }
        return FALSE;
    };
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), reinterpret_cast<DLGTEMPLATE*>(words.data()),
        owner, procedure, reinterpret_cast<LPARAM>(&state));
    return state.accepted ? std::optional(state.value) : std::nullopt;
}
