#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace command {
struct CatalogAction {
    std::wstring id, label, search;
    bool learn = true;
    size_t source = 0;
};
class Catalog {
    struct Entry { CatalogAction action; std::function<void()> invoke; };
    struct Rank { uint32_t uses = 0; uint64_t last = 0; };
    std::vector<Entry> entries_;
    std::map<std::wstring, size_t> index_;
    std::map<std::wstring, Rank> ranks_;
    uint64_t tick_ = 0;
    bool dirty_ = false;
    static std::wstring Fold(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return value;
    }
public:
    void Clear() { entries_.clear(); index_.clear(); }
    size_t Size() const { return entries_.size(); }
    bool Add(CatalogAction action, std::function<void()> invoke) {
        if (entries_.size() >= 8192 || action.id.empty() || action.id.size() > 4096 ||
            action.label.empty() || !invoke || index_.contains(action.id)) return false;
        index_[action.id] = entries_.size();
        entries_.push_back({std::move(action), std::move(invoke)});
        return true;
    }
    const CatalogAction &At(size_t index) const { return entries_.at(index).action; }
    using Matcher = std::function<int(const CatalogAction &, const std::wstring &)>;
    std::vector<size_t> Search(const std::wstring &query, const Matcher &matcher = {}) const {
        std::vector<size_t> result;
        auto folded = Fold(query);
        std::vector<int> scores(entries_.size());
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto &action = entries_[i].action;
            int score = -1;
            if (matcher) score = matcher(action, query);
            else {
                auto label = Fold(action.label), text = label + L" " + Fold(action.search);
                if (query.empty() || text.find(folded) != std::wstring::npos)
                    score = query.empty() ? 0 : label == folded ? 3 : label.starts_with(folded) ? 2 : 1;
            }
            if (score >= 0) {
                result.push_back(i);
                scores[i] = score;
            }
        }
        auto rank = [&](size_t i) { auto found = ranks_.find(entries_[i].action.id); return found == ranks_.end() ? Rank{} : found->second; };
        std::stable_sort(result.begin(), result.end(), [&](size_t left, size_t right) {
            if (scores[left] != scores[right]) return scores[left] > scores[right];
            auto a = rank(left), b = rank(right);
            return a.uses != b.uses ? a.uses > b.uses : a.last > b.last;
        });
        return result;
    }
    bool Invoke(const std::wstring &id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;
        // The handler may rebuild or clear the catalog while a modal dialog is open.
        auto invoke = entries_[found->second].invoke;
        bool learn = entries_[found->second].action.learn;
        auto key = entries_[found->second].action.id;
        invoke();
        if (learn) {
            if (!ranks_.contains(key) && ranks_.size() >= 128) {
                auto oldest = std::min_element(ranks_.begin(), ranks_.end(), [](const auto &a, const auto &b) {
                    return a.second.last < b.second.last;
                });
                ranks_.erase(oldest);
            }
            auto &rank = ranks_[key];
            rank.uses = std::min(uint32_t{65535}, rank.uses + 1); rank.last = ++tick_; dirty_ = true;
        }
        return true;
    }
    using Saved = std::map<std::wstring, std::wstring>;
    Saved Save() const {
        Saved saved;
        const wchar_t *digits = L"0123456789abcdef";
        for (const auto &[id, rank] : ranks_) {
            std::wstring key = L"r";
            for (wchar_t c : id) for (int shift = 12; shift >= 0; shift -= 4) key += digits[(c >> shift) & 15];
            saved[key] = std::to_wstring(rank.uses);
        }
        return saved;
    }
    void Restore(const Saved &saved) {
        ranks_.clear(); tick_ = 0; dirty_ = false;
        for (const auto &[key, count] : saved) {
            if (ranks_.size() >= 128) break;
            if (key.size() < 5 || key.size() > 16385 || key.front() != L'r' || (key.size() - 1) % 4 ||
                count.empty() || count.size() > 5 || count.find_first_not_of(L"0123456789") != std::wstring::npos) continue;
            std::wstring id;
            bool valid = true;
            for (size_t at = 1; at < key.size() && valid; at += 4) {
                unsigned code = 0;
                for (size_t i = 0; i < 4; ++i) {
                    wchar_t c = key[at + i];
                    int value = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
                    if (value < 0) { valid = false; break; }
                    code = code * 16 + static_cast<unsigned>(value);
                }
                if (!code) valid = false;
                id += static_cast<wchar_t>(code);
            }
            auto uses = std::stoul(count);
            if (valid && uses && uses <= 65535) ranks_[id] = {static_cast<uint32_t>(uses), ++tick_};
        }
    }
    bool Dirty() const { return dirty_; }
    void SavedSuccessfully() { dirty_ = false; }
};
}
