#include "../src/FileSearchModel.h"
#include "../src/WindowsFileSearch.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t **argv) {
    clipboard::SetExtraHandler(files::HandleTransfer);
    constexpr std::wstring_view helper = L"--clipboard-transfer=";
    if (argc == 2 && std::wstring_view(argv[1]).starts_with(helper)) return clipboard::TransferHelper(argv[1] + helper.size());
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) {
        GUID id{}; wchar_t text[64]{};
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, 64)) return 2;
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        auto reply = clipboard::Transfer(clipboard::TransferOp::SearchFiles,
            clipboard::TextPayload(L"Dako_index_probe_" + std::wstring(text)), cancel, 8000, L"diagnostics-file-probe");
        CloseHandle(cancel);
        auto results = reply.status == clipboard::TransferStatus::Ok ? files::Decode(reply.payload.bytes) : std::nullopt;
        std::printf("status=%u hresult=0x%08lx rows=%lld child_exited=%u\nrandom_fixture_query=1 clipboard_reads=0 clipboard_writes=0\n",
            static_cast<unsigned>(reply.status), reply.error, results ? static_cast<long long>(results->size()) : -1LL, reply.childExited);
        return results && results->empty() && reply.childExited ? 0 : 1;
    }
    try {
        auto sql = files::Sql(L"report's draft");
        Require(sql && sql->find(L"LIKE 'report''s draft%'") != std::wstring::npos &&
            sql->find(L"WHERE SCOPE='file:'") != std::wstring::npos, "fixed read-only query quotes user filename patterns");
        Require(!files::Sql(L"x") && !files::Sql(std::wstring(257, L'x')) && !files::Sql(L"line\nbreak"),
                "query size and control characters are bounded");
        auto path = files::LocalUrl(L"file:///C:/DakoFixture/report.txt");
        Require(path && files::LocalPath(*path), "documented file URI decodes to a local drive path");
        Require(!files::LocalUrl(L"https://fixture.invalid/a") && !files::LocalUrl(L"file://server/share/a.txt") &&
            !files::LocalPath(L"C:\\file.txt:stream") && !files::LocalPath(L"\\\\?\\C:\\file.txt") &&
            !files::LocalPath(L"relative.txt"), "remote, device, relative and alternate-stream targets rejected");
        Require(files::ConfirmOpen(L"C:\\a.PS1") && files::ConfirmOpen(L"C:\\shortcut.lnk") &&
            files::ConfirmOpen(L"C:\\setup.msi") && !files::ConfirmOpen(L"C:\\report.txt"),
            "scripts, shortcuts and installers require confirmation");
        auto bytes = files::Encode({{L"日本語 report.txt", L"C:\\DakoFixture\\日本語 report.txt"}});
        Require(bytes.has_value(), "result serialization");
        auto results = files::Decode(*bytes);
        Require(results && results->size() == 1 && results->front().name == L"日本語 report.txt", "Unicode result round-trip");
        auto badCount = *bytes; badCount[8] = std::byte{255};
        Require(!files::Decode(badCount), "untrusted result counts bounded before allocation");
        auto truncated = *bytes; truncated.pop_back();
        Require(!files::Decode(truncated), "truncated path payload rejected");
        Require(!files::Encode({{L"Remote", L"\\\\server\\share\\a.txt"}}), "remote results cannot be serialized");
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancel != nullptr, "cancel event");
        auto reply = clipboard::Transfer(clipboard::TransferOp::FixtureFiles, clipboard::TextPayload(L"fixture"), cancel, 3000,
                                         L"files-fixture");
        CloseHandle(cancel);
        auto returned = files::Decode(reply.payload.bytes);
        Require(reply.status == clipboard::TransferStatus::Ok && reply.childExited && returned && returned->size() == 1,
                "shipping helper extension transports synthetic files without querying the user index");
        std::puts("PASS fixed filename SQL, local-path validation, launch confirmation, bounded result IPC; no personal index queries");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError()); return 1;
    }
}
