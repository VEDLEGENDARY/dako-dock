#include "../src/ClipboardVault.h"
#include <cstdio>
#include <stdexcept>
#include <fstream>

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2)
        return 2;
    try {
        auto item = clipboard::ToItem(clipboard::TextPayload(L"Synthetic DPAPI 日本語 fixture"));
        Require(item.has_value(), "synthetic text item");
        item->captured = 1000;
        auto plain = clipboard::Serialize({*item}, 2000, 100000);
        Require(plain.has_value(), "bounded history serialization");
        auto encrypted = clipboard::Protect(*plain);
        Require(encrypted && *encrypted != *plain, "CurrentUser DPAPI encryption");
        auto decrypted = clipboard::Unprotect(*encrypted);
        Require(decrypted && *decrypted == *plain, "CurrentUser DPAPI round trip");
        auto restored = clipboard::Deserialize(*decrypted, 105000);
        Require(restored && restored->size() == 1 && restored->front().age == 6000 &&
                    restored->front().item.text == item->text,
                "restored item includes elapsed wall time");
        auto corrupt = *encrypted;
        corrupt[corrupt.size() / 2] ^= std::byte{1};
        Require(!clipboard::Unprotect(corrupt), "tampered ciphertext rejected by DPAPI");
        Require(!clipboard::Deserialize(*plain, 99999), "wall-clock rollback fails closed");
        Require(clipboard::Deserialize(*plain, 100000 + clipboard::RetentionMs)->empty(),
                "expired unpinned records discarded");
        item->pinned = true;
        auto pinned = clipboard::Serialize({*item}, 2000, 100000);
        Require(pinned && clipboard::Deserialize(*pinned, 100000 + clipboard::RetentionMs)->size() == 1,
                "pinned history survives normal TTL expiry");
        auto trailing = *plain;
        trailing.push_back(std::byte{0});
        Require(!clipboard::Deserialize(trailing, 100000), "trailing data rejected");
        auto truncated = *plain;
        truncated.pop_back();
        Require(!clipboard::Deserialize(truncated, 100000), "truncated payload rejected");
        auto file = std::filesystem::path(argv[1]) / L"synthetic-history.dpapi";
        item->captured = clipboard::RetentionMs;
        Require(clipboard::SaveVault(file, {*item}, clipboard::RetentionMs), "atomic encrypted file write");
        auto fromFile = clipboard::LoadVault(file);
        Require(fromFile && fromFile->size() == 1 && fromFile->front().item.text == item->text,
                "encrypted file round trip");
        HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "lock only generated vault fixture");
        bool refused = !clipboard::SaveVault(file, {}, clipboard::RetentionMs) && !clipboard::EraseVault(file);
        CloseHandle(locked);
        Require(refused && clipboard::LoadVault(file)->size() == 1,
                "failed replacement/erasure preserves old encrypted file and reports failure");
        auto orphan = file.wstring() + L".tmp-owned-fixture";
        {
            std::ofstream output(orphan, std::ios::binary);
            output.write(reinterpret_cast<const char *>(encrypted->data()),
                         static_cast<std::streamsize>(encrypted->size()));
        }
        Require(clipboard::EraseVault(file) && !std::filesystem::exists(file),
                "explicit erasure removes generated vault");
        Require(!std::filesystem::exists(orphan), "clear also removes an interrupted owned encrypted staging file");
        std::puts("PASS bounded history serialization, CurrentUser DPAPI, tamper/expiry checks and atomic encrypted "
                  "storage; no clipboard IO");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
