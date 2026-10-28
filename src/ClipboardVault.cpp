#include "ClipboardVault.h"
#include "PrivacyRuntime.h"
#include <wincrypt.h>
#include <atomic>
#include <cstring>

namespace clipboard {
namespace {
constexpr size_t PlainLimit = History::Limit + 4096, CipherLimit = PlainLimit + 65536;
constexpr uint32_t Magic = 0x444b4831, Version = 1;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct Wipe {
    std::vector<std::byte> &bytes;
    ~Wipe() {
        if (!bytes.empty())
            SecureZeroMemory(bytes.data(), bytes.size());
    }
};
struct Blob {
    DATA_BLOB value{};
    ~Blob() {
        if (value.pbData) {
            SecureZeroMemory(value.pbData, value.cbData);
            LocalFree(value.pbData);
        }
    }
};
void Put(std::vector<std::byte> &bytes, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 255));
}
struct Reader {
    const std::vector<std::byte> &bytes;
    size_t at = 0;
    bool valid = true;
    uint64_t Get(unsigned width) {
        if (!valid || at > bytes.size() || width > bytes.size() - at) {
            valid = false;
            return 0;
        }
        uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= static_cast<uint64_t>(bytes[at++]) << (i * 8);
        return value;
    }
};
} // namespace
uint64_t WallMilliseconds() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10000;
}
std::optional<std::vector<std::byte>> Protect(const std::vector<std::byte> &plain) {
    if (plain.empty() || plain.size() > PlainLimit)
        return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<std::byte *>(plain.data()))};
    Blob output;
    if (!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value))
        return {};
    std::vector<std::byte> result;
    if (output.value.cbData && output.value.cbData <= CipherLimit)
        result.assign(reinterpret_cast<std::byte *>(output.value.pbData),
                      reinterpret_cast<std::byte *>(output.value.pbData) + output.value.cbData);
    return result.empty() ? std::nullopt : std::make_optional(std::move(result));
}
std::optional<std::vector<std::byte>> Unprotect(const std::vector<std::byte> &encrypted) {
    if (encrypted.empty() || encrypted.size() > CipherLimit)
        return {};
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE *>(const_cast<std::byte *>(encrypted.data()))};
    Blob output;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value))
        return {};
    std::vector<std::byte> result;
    if (output.value.cbData && output.value.cbData <= PlainLimit)
        result.assign(reinterpret_cast<std::byte *>(output.value.pbData),
                      reinterpret_cast<std::byte *>(output.value.pbData) + output.value.cbData);
    return result.empty() ? std::nullopt : std::make_optional(std::move(result));
}
std::optional<std::vector<std::byte>> Serialize(const std::vector<Item> &items, uint64_t tick, uint64_t wall,
                                                uint64_t retention) {
    if (items.size() > History::Capacity)
        return {};
    std::vector<std::byte> bytes;
    Put(bytes, Magic, 4);
    Put(bytes, Version, 4);
    Put(bytes, wall, 8);
    Put(bytes, items.size(), 4);
    for (const auto &item : items) {
        auto payload = FromItem(item);
        auto validated = ToItem(payload);
        if (!validated || validated->kind != item.kind || item.captured > tick || item.Bytes() > History::ItemLimit ||
            payload.bytes.size() > TransferLimit || bytes.size() + payload.bytes.size() + 20 > PlainLimit)
            return {};
        Put(bytes, payload.format, 4);
        Put(bytes, item.pinned ? 1 : 0, 4);
        Put(bytes, std::min(retention, tick - item.captured), 8);
        Put(bytes, payload.bytes.size(), 4);
        bytes.insert(bytes.end(), payload.bytes.begin(), payload.bytes.end());
    }
    return bytes;
}
std::optional<std::vector<RestoredItem>> Deserialize(const std::vector<std::byte> &bytes, uint64_t wall,
                                                     uint64_t retention) {
    if (bytes.size() > PlainLimit)
        return {};
    Reader reader{bytes};
    if (reader.Get(4) != Magic || reader.Get(4) != Version)
        return {};
    uint64_t saved = reader.Get(8), count = reader.Get(4);
    if (!reader.valid || saved > wall || count > History::Capacity)
        return {};
    uint64_t elapsed = std::min(MaxRetentionMs, wall - saved);
    std::vector<RestoredItem> result;
    size_t total = 0;
    for (uint64_t index = 0; index < count; ++index) {
        Payload payload;
        payload.format = static_cast<UINT>(reader.Get(4));
        uint64_t pinned = reader.Get(4), age = reader.Get(8), size = reader.Get(4);
        if (!reader.valid || pinned > 1 || age > MaxRetentionMs || size > TransferLimit || reader.at > bytes.size() ||
            size > bytes.size() - reader.at)
            return {};
        payload.bytes.assign(bytes.begin() + static_cast<ptrdiff_t>(reader.at),
                             bytes.begin() + static_cast<ptrdiff_t>(reader.at + size));
        reader.at += static_cast<size_t>(size);
        auto item = ToItem(std::move(payload));
        if (!item || item->Bytes() > History::ItemLimit)
            return {};
        total += item->Bytes();
        if (total > History::Limit)
            return {};
        age = std::min(MaxRetentionMs, age + elapsed);
        if (!pinned && age >= retention)
            continue;
        item->pinned = pinned != 0;
        result.push_back({std::move(*item), age});
    }
    if (reader.at != bytes.size())
        return {};
    return result;
}
bool SaveVault(const std::filesystem::path &path, const std::vector<Item> &items, uint64_t tick, uint64_t retention) {
    privacy::Write audit(L"clipboard", L"file-write", path.wstring(), L"encrypted-history-snapshot");
    auto plain = Serialize(items, tick, WallMilliseconds(), retention);
    if (!plain)
        return false;
    Wipe wipe{*plain};
    auto encrypted = Protect(*plain);
    if (!encrypted)
        return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    static std::atomic<uint64_t> sequence = 0;
    auto temporary = path.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence);
    bool written = false;
    {
        privacy::Write staging(L"clipboard", L"file-write", temporary, L"stage-encrypted-history");
        Handle file(
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
            return false;
        DWORD size = static_cast<DWORD>(encrypted->size()), actual = 0;
        written = WriteFile(file.value, encrypted->data(), size, &actual, nullptr) && actual == size &&
                  FlushFileBuffers(file.value);
        staging.Complete(written);
    }
    bool committed = false;
    if (written)
        for (DWORD attempt = 0; attempt < 4 && !committed; ++attempt) {
            committed = MoveFileExW(temporary.c_str(), path.c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!committed)
                Sleep(15 * (attempt + 1));
        }
    if (!committed) {
        privacy::Write cleanup(L"clipboard", L"file-write", temporary, L"remove-failed-history-stage");
        cleanup.Complete(DeleteFileW(temporary.c_str()) != FALSE);
    }
    return audit.Complete(committed);
}
std::optional<std::vector<RestoredItem>> LoadVault(const std::filesystem::path &path, uint64_t retention) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(CipherLimit))
        return {};
    std::vector<std::byte> encrypted(static_cast<size_t>(size.QuadPart));
    DWORD actual = 0;
    if (!ReadFile(file.value, encrypted.data(), static_cast<DWORD>(encrypted.size()), &actual, nullptr) ||
        actual != encrypted.size())
        return {};
    auto plain = Unprotect(encrypted);
    if (!plain)
        return {};
    Wipe wipe{*plain};
    return Deserialize(*plain, WallMilliseconds(), retention);
}
bool EraseVault(const std::filesystem::path &path) {
    auto erase = [](const std::filesystem::path &target) {
        privacy::Write audit(L"clipboard", L"file-write", target.wstring(), L"erase-encrypted-history");
        bool erased = DeleteFileW(target.c_str()) != FALSE;
        if (!erased)
            erased = GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
        return audit.Complete(erased);
    };
    bool erased = erase(path);
    // Only this vault's reserved, directly contained staging files; never recurse or follow reparse points.
    auto parent = std::filesystem::absolute(path).parent_path().lexically_normal();
    auto prefix = path.filename().wstring() + L".tmp-";
    std::error_code error;
    std::filesystem::directory_iterator entries(parent, error);
    if (error)
        return erased && (error == std::errc::no_such_file_or_directory);
    for (auto end = std::filesystem::directory_iterator(); entries != end; entries.increment(error)) {
        if (error)
            return false;
        const auto &candidate = entries->path();
        if (!candidate.filename().wstring().starts_with(prefix) ||
            std::filesystem::absolute(candidate).parent_path().lexically_normal() != parent)
            continue;
        DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
            continue;
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            erased = false;
            continue;
        }
        erased = erase(candidate) && erased;
    }
    return erased && !error;
}
} // namespace clipboard
