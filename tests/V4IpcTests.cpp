#include "../src/SettingsCore.h"
#include <cassert>
#include <cstdio>
#include <functional>

static unsigned commits = 0, previews = 0, rollbacks = 0;
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == v4::SettingsChanged) {
        if (wparam == 0)
            ++commits;
        else if (wparam == 1)
            ++previews;
        else
            ++rollbacks;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
static bool PumpUntil(const std::function<bool()> &done, DWORD timeout = 3000) {
    ULONGLONG deadline = GetTickCount64() + timeout;
    while (GetTickCount64() < deadline) {
        if (done()) return true;
        MsgWaitForMultipleObjectsEx(0, nullptr, 25, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return false;
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--preview-child") {
        v4::Preview packet;
        wcscpy_s(packet.key, L"Radius");
        wcscpy_s(packet.value, L"23");
        bool sent=false;for(int i=0;i<50&&!sent;++i){sent=v4::SendPreview(packet,argv[2]);if(!sent)Sleep(20);}
        if(!sent)return 2;
        Sleep(10000);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW type{};
    type.lpfnWndProc = Procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"Dako.Test.SettingsIpc";
    RegisterClassW(&type);
    HWND window =
        CreateWindowExW(0, type.lpszClassName, L"", 0, 0, 0, 1, 1, HWND_MESSAGE, nullptr, type.hInstance, nullptr);
    assert(window);
    std::wstring channel = L".test-" + std::to_wstring(GetCurrentProcessId());
    {
        v4::SettingsWatch watch(window, channel);
        v4::Preview packet;
        wcscpy_s(packet.key, L"Radius");
        wcscpy_s(packet.value, L"28");
        assert(PumpUntil([&] { return v4::SendPreview(packet, channel); }));
        assert(PumpUntil([] { return previews == 1; }));
        auto received = watch.Consume();
        assert(received && std::wstring(received->value) == L"28");
        v4::SignalSettings(channel);
        assert(PumpUntil([] { return commits == 1; }));
        packet.command = 1;
        assert(PumpUntil([&] { return v4::SendPreview(packet, channel); }));
        assert(PumpUntil([] { return previews == 2; }));
        received = watch.Consume();
        assert(received && received->command == 1);
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto command = L"\"" + std::wstring(executable) + L"\" --preview-child " + channel;
        STARTUPINFOW start{sizeof(start)};
        PROCESS_INFORMATION child{};
        assert(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &start, &child));
        assert(PumpUntil([] { return previews == 3; }));
        assert(TerminateProcess(child.hProcess, 0));
        WaitForSingleObject(child.hProcess, 3000);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        assert(PumpUntil([] { return rollbacks == 1; }));
    }
    DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
    CoUninitialize();
    std::puts("PASS user-scoped preview pipe, event reload, rollback packets, forced settings-client exit and clean "
              "shutdown");
}
