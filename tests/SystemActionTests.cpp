#include "../src/SystemActionModel.h"
#include "../src/SystemActionNative.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
    clipboard::SetExtraHandler([](clipboard::TransferOp operation, clipboard::Payload &payload, DWORD &error) {
        // This test executable has no path to a native removal request, including its private helper entry.
        if (operation != clipboard::TransferOp::QueryRecycleBin && operation != clipboard::TransferOp::QueryDrives) {
            error = ERROR_ACCESS_DENIED;
            return clipboard::TransferStatus::Unsupported;
        }
        return systemactions::HandleTransfer(operation, payload, error);
    });
    constexpr std::wstring_view helper = L"--clipboard-transfer=";
    if (argc == 2 && std::wstring_view(argv[1]).starts_with(helper))
        return clipboard::TransferHelper(argv[1] + helper.size());
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) {
        HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!cancel)
            return 2;
        int code = 0;
        for (auto operation : {clipboard::TransferOp::QueryRecycleBin, clipboard::TransferOp::QueryDrives}) {
            auto reply = clipboard::Transfer(operation, {}, cancel, 6000, L"diagnostics-system-metadata");
            bool valid = false;
            if (operation == clipboard::TransferOp::QueryRecycleBin)
                valid = systemactions::DecodeTotals(reply.payload).has_value();
            else
                valid = systemactions::DecodeDrives(reply.payload).has_value();
            std::printf("operation=%u status=%u error=0x%08lx valid_metadata=%u child_exited=%u\n",
                        static_cast<unsigned>(operation), static_cast<unsigned>(reply.status), reply.error, valid,
                        reply.childExited);
            if (reply.status != clipboard::TransferStatus::Ok || !valid || !reply.childExited)
                code = 1;
        }
        CloseHandle(cancel);
        std::puts("file_names_printed=0 device_identifiers_printed=0 deletion_calls=0 ejection_calls=0");
        return code;
    }
    try {
        using namespace systemactions;
        auto totals = DecodeTotals(Encode(BinTotals{3, 98304}));
        Require(totals && totals->items == 3 && totals->bytes == 98304,
                "bin totals transport preserves bounded metadata");
        Require(!DecodeTotals(Encode(BinTotals{UINT64_MAX, 1})),
                "negative native totals cannot pass unsigned decoding");
        auto badTotals = Encode(BinTotals{1, 2});
        badTotals.bytes.pop_back();
        Require(!DecodeTotals(badTotals), "truncated native totals rejected");
        Drive fixture{L"DAKO\\FIXTURE\\DEVICE", L"Synthetic removable drive", L"Y:\\ Z:\\ ", 123456789};
        auto encoded = Encode(std::vector<Drive>{fixture});
        Require(encoded.has_value(), "validated removable metadata serializes");
        auto decoded = DecodeDrives(*encoded);
        Require(decoded && decoded->size() == 1 && SameTarget(fixture, decoded->front()),
                "removable identity and arrival survive IPC");
        auto changed = fixture;
        changed.arrived++;
        Require(!SameTarget(fixture, changed), "a reattached device is not the previously confirmed target");
        changed = fixture;
        changed.roots = L"X:\\ ";
        Require(!SameTarget(fixture, changed), "changed drive assignments reject stale selection");
        changed = fixture;
        changed.id = L"DAKO\\FIXTURE\\OTHER";
        Require(!SameTarget(fixture, changed), "another device cannot reuse the displayed drive letters");
        changed = fixture;
        changed.label = L"Renamed device";
        Require(SameTarget(fixture, changed), "a display label is not device identity");
        Require(!Roots(L"C:\\ C:\\ ") && !Roots(L"\\\\server\\share") && !Roots(L"C:\\:stream") && !Roots(L"c:\\ "),
                "device roots have a bounded unambiguous canonical format");
        changed = fixture;
        changed.arrived = 0;
        Require(!Valid(changed), "unverifiable arrival identity is ineligible");
        changed = fixture;
        changed.id += L'\n';
        Require(!Valid(changed), "control characters in device metadata rejected");
        Require(!Encode(std::vector<Drive>{fixture, fixture}), "duplicate targets cannot create ambiguous actions");
        auto corrupt = *encoded;
        corrupt.bytes[8] = std::byte{255};
        Require(!DecodeDrives(corrupt), "untrusted counts bounded before allocation");
        corrupt = *encoded;
        corrupt.bytes.pop_back();
        Require(!DecodeDrives(corrupt), "truncated arrival metadata rejected");
        std::puts("PASS system metadata bounds, immutable target identity, reattachment/drive reassignment and "
                  "malformed IPC; no native deletion/ejection");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
