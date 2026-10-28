#include "../src/SettingsCore.h"
#include <commctrl.h>
#include <oleacc.h>
#include <cassert>
#include <cstdio>

static HWND Find(DWORD pid) {
    struct Search {
        DWORD pid;
        HWND result = nullptr;
    } search{pid};
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            auto &value = *reinterpret_cast<Search *>(context);
            DWORD current = 0;
            GetWindowThreadProcessId(window, &current);
            wchar_t name[64]{};
            GetClassNameW(window, name, 64);
            if (current == value.pid && !wcscmp(name, L"Dako.Settings")) {
                value.result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.result;
}
int wmain(int argc, wchar_t **argv) {
    assert(argc == 3);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto directory = std::filesystem::path(argv[2]);
    std::filesystem::create_directories(directory);
    auto path = (directory / L"ui-settings.ini").wstring();
    auto command = L"\"" + std::wstring(argv[1]) + L"\" --settings-file=\"" + path + L"\"";
    STARTUPINFOW start{sizeof(start)};
    start.dwFlags = STARTF_USESHOWWINDOW;
    start.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &start, &child));
    WaitForInputIdle(child.hProcess, 5000);
    HWND window = nullptr;
    for (int i = 0; i < 100 && !window; ++i) {
        Sleep(50);
        window = Find(child.dwProcessId);
    }
    assert(window);
    HWND nav = GetDlgItem(window, 2);
    assert(nav);
    SendMessageW(nav, LB_SETCURSEL, 4, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(2, LBN_SELCHANGE), reinterpret_cast<LPARAM>(nav));
    HWND theme = GetDlgItem(window, 100);
    assert(theme);
    SendMessageW(theme, CB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(100, CBN_SELCHANGE), reinterpret_cast<LPARAM>(theme));
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(10, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window, 10)));
    assert(v4::ReadValues(path).Text(L"Theme") == L"Paper");
    IAccessible *accessible = nullptr;
    assert(SUCCEEDED(AccessibleObjectFromWindow(theme, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
                                                reinterpret_cast<void **>(&accessible))));
    VARIANT self{};
    self.vt = VT_I4;
    self.lVal = CHILDID_SELF;
    BSTR name = nullptr;
    assert(SUCCEEDED(accessible->get_accName(self, &name)));
    assert(name && std::wstring(name) == L"Theme");
    SysFreeString(name);
    accessible->Release();
    SendMessageW(nav, LB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(2, LBN_SELCHANGE), reinterpret_cast<LPARAM>(nav));
    HWND scale = GetDlgItem(window, 102);
    assert(scale);
    SendMessageW(scale, TBM_SETPOS, TRUE, 120);
    SendMessageW(window, WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 120), reinterpret_cast<LPARAM>(scale));
    assert(v4::ReadValues(path).Int(L"Scale") == 120);
    SendMessageW(nav, LB_SETCURSEL, 6, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(2, LBN_SELCHANGE), reinterpret_cast<LPARAM>(nav));
    HWND playingOnly = GetDlgItem(window, 102);
    assert(playingOnly && SendMessageW(playingOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
    SendMessageW(playingOnly, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), reinterpret_cast<LPARAM>(playingOnly));
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(10, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window, 10)));
    assert(!v4::ReadValues(path).Bool(L"PeekPlayingOnly"));
    SendMessageW(window, WM_CLOSE, 0, 0);
    assert(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    GetExitCodeProcess(child.hProcess, &exit);
    assert(exit == 0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    CoUninitialize();
    std::puts(
        "PASS independent settings GUI, native controls, accessible labels, Apply, slider commit and isolated store");
}
