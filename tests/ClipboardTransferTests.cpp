#include "../src/ClipboardTransfer.h"
#include "../src/ClipboardData.h"
#include "../src/ClipboardTask.h"
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <string_view>

void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
LRESULT CALLBACK CancelOnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_APP + 1) {
        SetEvent(reinterpret_cast<HANDLE>(GetWindowLongPtrW(window, GWLP_USERDATA)));
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int wmain(int argc, wchar_t **argv) {
    constexpr std::wstring_view mode = L"--clipboard-transfer=";
    if (argc == 2 && std::wstring_view(argv[1]).starts_with(mode))
        return clipboard::TransferHelper(argv[1] + mode.size());
    try {
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel != nullptr, "cancel event");
        auto value = clipboard::TextPayload(L"Fixture 日本語 text");
        auto reply = clipboard::Transfer(clipboard::TransferOp::FixtureEcho, value, cancel, 2000);
        Require(reply.status == clipboard::TransferStatus::Ok &&
                    clipboard::PayloadText(reply.payload) == L"Fixture 日本語 text",
                "private inherited mapping preserves payload");
        auto timed = clipboard::Transfer(clipboard::TransferOp::FixtureHang, {}, cancel, 100);
        Require(timed.status == clipboard::TransferStatus::TimedOut && timed.childExited,
                "unresponsive owned child is terminated on timeout");
        clipboard::TransferActivity activity;
        clipboard::Reply tracked;
        std::thread observed([&] {
            tracked = clipboard::Transfer(clipboard::TransferOp::FixtureHang, {}, cancel, 2000, L"fixture", &activity);
        });
        ULONGLONG until = GetTickCount64() + 1500;
        size_t committed = 0;
        while (GetTickCount64() < until && !committed) {
            committed = activity.CommittedBytes();
            if (!committed)
                Sleep(1);
        }
        SetEvent(cancel);
        observed.join();
        ResetEvent(cancel);
        Require(committed > 0 && committed <= clipboard::HelperCommitLimit && activity.CommittedBytes() == 0 &&
                    tracked.childExited,
                "helper commit is attributed on demand and disappears after cancellation");
        std::thread interrupt([&] {
            Sleep(50);
            SetEvent(cancel);
        });
        auto cancelled = clipboard::Transfer(clipboard::TransferOp::FixtureHang, {}, cancel, 2000);
        interrupt.join();
        Require(cancelled.status == clipboard::TransferStatus::Cancelled && cancelled.childExited,
                "module cancellation terminates its owned child");
        ResetEvent(cancel);
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"Dako.Transfer.Fixture";
        cls.lpfnWndProc = CancelOnMessage;
        Require(RegisterClassW(&cls) != 0, "fixture message class");
        HWND window =
            CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
        Require(window != nullptr, "fixture notification window");
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cancel));
        PostMessageW(window, WM_APP + 1, 0, 0);
        auto pumped = clipboard::Transfer(clipboard::TransferOp::FixtureHang, {}, cancel, 2000);
        Require(pumped.status == clipboard::TransferStatus::Cancelled && pumped.childExited,
                "lock/suspend notification messages can cancel an active transfer");
        DestroyWindow(window);
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        ResetEvent(cancel);
        DWORD before = 0, after = 0;
        GetProcessHandleCount(GetCurrentProcess(), &before);
        for (int i = 0; i < 20; ++i) {
            auto repeated = clipboard::Transfer(clipboard::TransferOp::FixtureEcho, value, cancel, 2000);
            Require(repeated.status == clipboard::TransferStatus::Ok, "repeated transfer");
        }
        GetProcessHandleCount(GetCurrentProcess(), &after);
        Require(before == after, "mapping/job/process/thread handles return to baseline");
        CloseHandle(cancel);
        clipboard::Payload dib{CF_DIB};
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = 2;
        header.biHeight = 2;
        header.biPlanes = 1;
        header.biBitCount = 24;
        header.biCompression = BI_RGB;
        dib.bytes.resize(sizeof(header) + 16);
        std::memcpy(dib.bytes.data(), &header, sizeof(header));
        auto image = clipboard::DecodeImage(dib);
        Require(image && image->width == 2 && image->height == 2 && image->bgra.size() == 16,
                "bounded DIB decoding handles padded bottom-up rows");
        auto malformed = dib;
        malformed.bytes.pop_back();
        Require(!clipboard::DecodeImage(malformed), "truncated image rejected");
        header.biWidth = LONG_MAX;
        std::memcpy(malformed.bytes.data(), &header, sizeof(header));
        Require(!clipboard::DecodeImage(malformed), "oversized image dimensions rejected before allocation");
        clipboard::Payload files{CF_HDROP};
        DROPFILES drop{};
        drop.pFiles = sizeof(drop);
        drop.fWide = TRUE;
        const wchar_t paths[] = L"C:\\fixture\\a.txt\0D:\\fixture\\b.txt\0";
        files.bytes.resize(sizeof(drop) + sizeof(paths));
        std::memcpy(files.bytes.data(), &drop, sizeof(drop));
        std::memcpy(files.bytes.data() + sizeof(drop), paths, sizeof(paths));
        auto list = clipboard::FileList(files);
        Require(list && list->size() == 2 && clipboard::ToItem(files)->kind == clipboard::Kind::Files,
                "file-list payload decoded without filesystem access");
        drop.pFiles = MAXDWORD;
        std::memcpy(files.bytes.data(), &drop, sizeof(drop));
        Require(!clipboard::FileList(files), "invalid file-list offset rejected");
        clipboard::Task task;
        Require(task.Start(clipboard::TransferOp::FixtureHang, {}, nullptr, L"fixture"), "background transfer starts");
        Require(!task.Start(clipboard::TransferOp::FixtureEcho, value, nullptr, L"fixture"),
                "only one helper per module at a time");
        ULONGLONG stop = GetTickCount64();
        task.Stop();
        Require(!task.Busy() && task.Bytes() == 0 && GetTickCount64() - stop < 3500,
                "module removal cancels and joins its worker with no retained payload");
        Require(clipboard::TransferHelper(L"0") != 0 && clipboard::TransferHelper(L"123x") != 0,
                "invalid helper arguments rejected");
        std::puts(
            "PASS private clipboard transfer IPC, bounded timeout/cancellation and handle cleanup; no clipboard IO");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
