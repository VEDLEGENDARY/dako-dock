#include "../src/CommandModel.h"
#include "../src/ClipboardModel.h"
#include "../src/SnippetModel.h"
#include "../src/CommandCatalog.h"
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Value(const wchar_t *input, double expected) {
    auto value = command::Calculator::Evaluate(input);
    Require(value && std::abs(*value - expected) < 1e-9, "calculator result");
}
} // namespace
int main() {
    try {
        command::Catalog catalog;
        int invocations = 0;
        Require(catalog.Add({L"core.a", L"Alpha"}, [&] { ++invocations; }) &&
                    catalog.Add({L"core.b", L"Beta Alpha"}, [&] { ++invocations; }),
                "all sources share one command catalog");
        Require(!catalog.Add({L"core.a", L"Duplicate"}, [] {}), "duplicate action ID rejected");
        Require(catalog.Invoke(L"core.b") && catalog.At(catalog.Search(L"").front()).id == L"core.b",
                "local learned usage ranks empty-query results");
        Require(catalog.At(catalog.Search(L"Alpha").front()).id == L"core.a",
                "exact match outranks frequent substring");
        command::Catalog restored;
        restored.Restore(catalog.Save());
        restored.Add({L"core.a", L"Alpha"}, [] {});
        restored.Add({L"core.b", L"Beta Alpha"}, [] {});
        Require(restored.At(restored.Search(L"").front()).id == L"core.b", "learned usage persists without query text");
        restored.Add({L"private.result-1", L"Private query", L"", false}, [] {});
        auto beforeRank = restored.Save();
        Require(restored.Invoke(L"private.result-1") && restored.Save() == beforeRank,
                "ephemeral private results are never learned");
        restored.Add({L"core.rebuild", L"Rebuild"}, [&] { restored.Clear(); });
        Require(restored.Invoke(L"core.rebuild") && restored.Size() == 0 && !restored.Invoke(L"core.a"),
                "reentrant catalog rebuild cannot retain a stale handler");
        auto snippet = snippets::Parse(L" Greeting | First\\nSecond\\t\\\\end", 7);
        Require(snippet && snippet->text == L"First\nSecond\t\\end", "snippet insert syntax preserves whitespace");
        auto decoded = snippets::Decode(snippets::Encode({*snippet}));
        Require(decoded && decoded->size() == 1 && decoded->front().title == L"Greeting" &&
                    decoded->front().text == snippet->text,
                "snippet storage round-trip");
        Require(!snippets::Decode(L"1;1:0000:0061") && !snippets::Decode(L"1;abc:0061:0062") &&
                    !snippets::Decode(L"1;1:0061:0062;1:0063:0064") && !snippets::Parse(L"No body | ", 1),
                "malformed, duplicate and empty snippets rejected");
        Value(L"2 + 3 * 4", 14);
        Value(L"(2 + 3) * 4", 20);
        Value(L"2^3^2", 512);
        Value(L"-2^2", -4);
        Value(L"2^-2", .25);
        Value(L"= 1e3 / 4", 250);
        for (auto invalid : {L"", L"1/0", L"2+", L"1) + 2", L"nan", L"inf", L"1e999", L"sqrt(4)", L"2 + launch.exe",
                             L"(2+3", L"2^0.5^", L"0^-1", L"1e999^0", L"(1e308*1e308)^0"})
            Require(!command::Calculator::Evaluate(invalid), "calculator rejects invalid or non-finite expressions");
        Require(!command::Calculator::Evaluate(std::wstring(60, L'(') + L"1" + std::wstring(60, L')')),
                "calculator recursion is bounded");
        auto length = command::Convert(L"1 in to cm"), temperature = command::Convert(L"32 F in C");
        Require(length && std::abs(length->value - 2.54) < 1e-9 && temperature && std::abs(temperature->value) < 1e-9,
                "length and affine temperature conversion");
        auto bytes = command::Convert(L"1 MiB in KB");
        Require(bytes && std::abs(bytes->value - 1048.576) < 1e-9, "SI and IEC bytes stay distinct");
        Require(!command::Convert(L"1 m in kg") && !command::Convert(L"-1 K in C") &&
                    !command::Convert(L"1 m in ft extra"),
                "unit mismatches and invalid temperature rejected");
        auto clean = command::CleanUrl(L"https://fixture.invalid/p?utm_source=x&id=7&fbclid=y#section");
        Require(clean.text == L"https://fixture.invalid/p?id=7#section",
                "tracking stripped; functional query retained");
        Require(command::CleanUrl(L"https://fixture.invalid/?u%74m_source=x").text == L"https://fixture.invalid/",
                "encoded tracking key recognized");
        auto signedUrl = command::CleanUrl(L"https://fixture.invalid/?utm_source=x&X-Amz-Signature=abc");
        Require(signedUrl.signedUrl && signedUrl.text.find(L"utm_source") != std::wstring::npos,
                "signed URLs are left unchanged");
        for (auto unchanged : {L"plain text", L"https://fixture.invalid/#route?utm_source=x", L"javascript:x?utm_x=y",
                               L"https://fixture.invalid/?ref=important&source=feed", L"https://?utm_source=x"})
            Require(command::CleanUrl(unchanged).text == unchanged, "non-tracking data is unchanged");

        using clipboard::HistoryFlag;
        Require(
            !clipboard::MayRetain(true, HistoryFlag::Allowed) && !clipboard::MayRetain(false, HistoryFlag::Denied) &&
                !clipboard::MayRetain(false, HistoryFlag::Invalid) && clipboard::MayRetain(false, HistoryFlag::Missing),
            "clipboard exclusions fail closed");
        clipboard::History history;
        auto oldGeneration = history.ClearGeneration();
        history.Clear();
        clipboard::Item delayed;
        delayed.text = L"Delayed source";
        Require(!history.AddIfUncleared(delayed, false, HistoryFlag::Allowed, 1, oldGeneration) &&
                    history.Items().empty(),
                "a delayed transfer cannot resurrect data after clear");
        clipboard::Item item;
        item.text = L"first";
        Require(history.Add(item, false, HistoryFlag::Missing, 1), "clipboard item accepted");
        auto id = history.Items().front().id;
        Require(history.Pin(id), "clipboard item can be pinned");
        for (int i = 0; i < 50; ++i) {
            item.text = L"Entry " + std::to_wstring(i);
            Require(history.Add(item, false, HistoryFlag::Allowed, static_cast<uint64_t>(i + 2)), "bounded add");
        }
        Require(history.Items().size() == 25 && history.Find(id) && history.Find(id)->pinned,
                "oldest unpinned entries evicted without losing pins");
        auto newest = history.Items().front().id;
        Require(history.Add(item, false, HistoryFlag::Allowed, 100) && history.Items().front().id == newest,
                "duplicate content retains stable identity");
        history.Expire(1000, 500);
        Require(history.Items().size() == 1 && history.Find(id), "expiry retains explicitly pinned content");
        history.Clear();
        Require(history.Items().empty() && history.Bytes() == 0,
                "clear releases all history allocations including pins");
        item.text = L"after clear";
        history.Add(item, false, HistoryFlag::Missing, 1001);
        Require(history.Items().front().id != id, "cleared item IDs cannot address later content");
        item.bytes.resize(clipboard::History::ItemLimit + 1);
        Require(!history.Add(item, false, HistoryFlag::Allowed, 1002), "oversized clipboard data rejected");
        std::puts("PASS bounded calculator/units, non-destructive URL cleaning and private bounded clipboard model");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
