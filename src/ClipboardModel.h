#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clipboard {
enum class HistoryFlag { Missing, Allowed, Denied, Invalid };
inline bool MayRetain(bool excluded, HistoryFlag history) {
    return !excluded && history != HistoryFlag::Denied && history != HistoryFlag::Invalid;
}
enum class Kind : uint32_t { Text, Image, Files };
struct Item {
    uint64_t id = 0, captured = 0;
    Kind kind = Kind::Text;
    uint32_t format = 0;
    std::wstring text;
    std::vector<std::byte> bytes;
    bool pinned = false;
    size_t Bytes() const { return text.capacity() * sizeof(wchar_t) + bytes.capacity(); }
    bool Same(const Item &other) const {
        return kind == other.kind && format == other.format && text == other.text && bytes == other.bytes;
    }
};
class History {
    std::vector<Item> items_;
    size_t capacity_ = 25;
    uint64_t generation_ = 0;
    uint64_t clearGeneration_ = 0;
    inline static std::atomic<uint64_t> sequence_ = 0;

  public:
    static constexpr size_t Capacity = 25, Limit = 8 * 1024 * 1024, ItemLimit = 2 * 1024 * 1024;
    void CapacityLimit(size_t limit) { capacity_ = std::clamp(limit, size_t{1}, Capacity); }
    const std::vector<Item> &Items() const { return items_; }
    uint64_t Generation() const { return generation_; }
    uint64_t ClearGeneration() const { return clearGeneration_; }
    size_t Bytes() const {
        size_t bytes = items_.capacity() * sizeof(Item);
        for (const auto &item : items_)
            bytes += item.Bytes();
        return bytes;
    }
    const Item *Find(uint64_t id) const {
        for (const auto &item : items_)
            if (item.id == id)
                return &item;
        return nullptr;
    }
    bool Add(Item item, bool excluded, HistoryFlag permission, uint64_t now) {
        if (static_cast<uint32_t>(item.kind) > static_cast<uint32_t>(Kind::Files) || !MayRetain(excluded, permission) ||
            item.Bytes() > ItemLimit || (item.text.empty() && item.bytes.empty()))
            return false;
        for (size_t i = 0; i < items_.size(); ++i) {
            if (!items_[i].Same(item))
                continue;
            auto existing = std::move(items_[i]);
            items_.erase(items_.begin() + static_cast<ptrdiff_t>(i));
            existing.captured = now;
            items_.insert(items_.begin(), std::move(existing));
            ++generation_;
            return true;
        }
        if (items_.empty())
            items_.reserve(Capacity);
        while (items_.size() >= capacity_ || Bytes() + item.Bytes() > Limit) {
            auto oldest = std::find_if(items_.rbegin(), items_.rend(), [](const Item &entry) { return !entry.pinned; });
            if (oldest == items_.rend())
                return false;
            items_.erase(std::next(oldest).base());
        }
        item.id = ++sequence_;
        item.captured = now;
        items_.insert(items_.begin(), std::move(item));
        ++generation_;
        return true;
    }
    bool AddIfUncleared(Item item, bool excluded, HistoryFlag permission, uint64_t now, uint64_t expected) {
        return expected == clearGeneration_ && Add(std::move(item), excluded, permission, now);
    }
    bool Pin(uint64_t id) {
        for (auto &item : items_)
            if (item.id == id) {
                item.pinned = !item.pinned;
                ++generation_;
                return true;
            }
        return false;
    }
    bool Remove(uint64_t id) {
        bool removed = std::erase_if(items_, [=](const Item &item) { return item.id == id; }) != 0;
        if (removed)
            ++generation_;
        return removed;
    }
    void Expire(uint64_t now, uint64_t age) {
        if (!age)
            return;
        if (std::erase_if(items_, [&](const Item &item) {
                return !item.pinned && now >= item.captured && now - item.captured >= age;
            }))
            ++generation_;
    }
    void Clear() {
        ++generation_;
        ++clearGeneration_;
        std::vector<Item>().swap(items_);
    }
};
} // namespace clipboard
