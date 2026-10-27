#pragma once
#include "ClipboardData.h"
#include <filesystem>

namespace clipboard {
inline constexpr uint64_t RetentionMs = 10 * 60 * 1000;
struct RestoredItem { Item item; uint64_t age = 0; };
uint64_t WallMilliseconds();
std::optional<std::vector<std::byte>> Protect(const std::vector<std::byte> &plain);
std::optional<std::vector<std::byte>> Unprotect(const std::vector<std::byte> &encrypted);
std::optional<std::vector<std::byte>> Serialize(const std::vector<Item> &, uint64_t tick, uint64_t wall);
std::optional<std::vector<RestoredItem>> Deserialize(const std::vector<std::byte> &, uint64_t wall);
bool SaveVault(const std::filesystem::path &, const std::vector<Item> &, uint64_t tick);
std::optional<std::vector<RestoredItem>> LoadVault(const std::filesystem::path &);
bool EraseVault(const std::filesystem::path &);
}
