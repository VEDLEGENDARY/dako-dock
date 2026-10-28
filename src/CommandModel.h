#pragma once
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace command {
inline std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t item) { return static_cast<wchar_t>(std::towlower(item)); });
    return value;
}
class Calculator {
    std::wstring input_;
    size_t at_ = 0;
    unsigned depth_ = 0;
    bool valid_ = true;
    void Space() {
        while (at_ < input_.size() && std::iswspace(input_[at_]))
            ++at_;
    }
    bool Take(wchar_t value) {
        Space();
        if (at_ < input_.size() && input_[at_] == value) {
            ++at_;
            return true;
        }
        return false;
    }
    double Atom() {
        if (++depth_ > 32) {
            valid_ = false;
            --depth_;
            return 0;
        }
        double result = 0;
        Space();
        if (Take(L'(')) {
            result = Sum();
            if (!Take(L')'))
                valid_ = false;
        } else {
            auto start = input_.c_str() + at_;
            wchar_t *end = nullptr;
            result = std::wcstod(start, &end);
            if (end == start)
                valid_ = false;
            else
                at_ += static_cast<size_t>(end - start);
        }
        --depth_;
        if (!std::isfinite(result))
            valid_ = false;
        return result;
    }
    double Power() {
        auto left = Atom();
        if (Take(L'^')) {
            if (++depth_ > 32) {
                valid_ = false;
                --depth_;
                return 0;
            }
            left = std::pow(left, Unary());
            if (!std::isfinite(left))
                valid_ = false;
            --depth_;
        }
        return left;
    }
    double Unary() {
        if (Take(L'+') || Take(L'-')) {
            wchar_t sign = input_[at_ - 1];
            if (++depth_ > 32) {
                valid_ = false;
                --depth_;
                return 0;
            }
            auto value = Unary();
            --depth_;
            return sign == L'-' ? -value : value;
        }
        return Power();
    }
    double Product() {
        auto result = Unary();
        for (;;) {
            if (Take(L'*'))
                result *= Unary();
            else if (Take(L'/')) {
                auto denominator = Unary();
                if (denominator == 0)
                    valid_ = false;
                else
                    result /= denominator;
            } else
                break;
            if (!std::isfinite(result))
                valid_ = false;
        }
        return result;
    }
    double Sum() {
        auto result = Product();
        for (;;) {
            if (Take(L'+'))
                result += Product();
            else if (Take(L'-'))
                result -= Product();
            else
                break;
            if (!std::isfinite(result))
                valid_ = false;
        }
        return result;
    }

  public:
    static std::optional<double> Evaluate(std::wstring input) {
        if (input.empty() || input.size() > 256 || input.find(L'\0') != std::wstring::npos)
            return {};
        Calculator parser;
        parser.input_ = std::move(input);
        if (parser.input_.front() == L'=')
            parser.at_ = 1;
        double value = parser.Sum();
        parser.Space();
        if (!parser.valid_ || parser.at_ != parser.input_.size() || !std::isfinite(value))
            return {};
        return value;
    }
};
struct Conversion {
    double value = 0;
    std::wstring unit;
};
inline std::optional<Conversion> Convert(const std::wstring &query) {
    if (query.size() > 128 || query.find(L'\0') != std::wstring::npos)
        return {};
    std::wistringstream stream(query);
    double number = 0;
    std::wstring from, separator, to, extra;
    if (!(stream >> number >> from >> separator >> to) || (stream >> extra) || !std::isfinite(number))
        return {};
    separator = Lower(separator);
    if (separator != L"in" && separator != L"to" && separator != L"=")
        return {};
    struct Unit {
        const wchar_t *name;
        int category;
        double factor, offset;
    };
    const Unit units[]{{L"m", 0, 1, 0},
                       {L"cm", 0, .01, 0},
                       {L"mm", 0, .001, 0},
                       {L"km", 0, 1000, 0},
                       {L"in", 0, .0254, 0},
                       {L"ft", 0, .3048, 0},
                       {L"yd", 0, .9144, 0},
                       {L"mi", 0, 1609.344, 0},
                       {L"kg", 1, 1, 0},
                       {L"g", 1, .001, 0},
                       {L"lb", 1, .45359237, 0},
                       {L"oz", 1, .028349523125, 0},
                       {L"s", 2, 1, 0},
                       {L"min", 2, 60, 0},
                       {L"h", 2, 3600, 0},
                       {L"day", 2, 86400, 0},
                       {L"bytes", 3, 1, 0},
                       {L"kb", 3, 1000, 0},
                       {L"mb", 3, 1e6, 0},
                       {L"gb", 3, 1e9, 0},
                       {L"tb", 3, 1e12, 0},
                       {L"kib", 3, 1024, 0},
                       {L"mib", 3, 1048576, 0},
                       {L"gib", 3, 1073741824, 0},
                       {L"tib", 3, 1099511627776, 0},
                       {L"c", 4, 1, 273.15},
                       {L"f", 4, 5.0 / 9, 459.67 * 5 / 9},
                       {L"k", 4, 1, 0}};
    const Unit *source = nullptr, *target = nullptr;
    auto sourceName = Lower(from), targetName = Lower(to);
    for (const auto &unit : units) {
        if (sourceName == unit.name)
            source = &unit;
        if (targetName == unit.name)
            target = &unit;
    }
    if (!source || !target || source->category != target->category)
        return {};
    double base = number * source->factor + source->offset;
    if (source->category == 4 && base < -1e-9)
        return {};
    double result = (base - target->offset) / target->factor;
    if (!std::isfinite(result))
        return {};
    return Conversion{result, to};
}
struct Cleaned {
    std::wstring text;
    bool signedUrl = false;
};
inline Cleaned CleanUrl(const std::wstring &input) {
    if (input.size() > 32768 || input.find_first_of(L"\r\n\t ") != std::wstring::npos ||
        input.find(L'\0') != std::wstring::npos)
        return {input};
    auto lower = Lower(input);
    size_t host = lower.starts_with(L"https://") ? 8 : lower.starts_with(L"http://") ? 7 : 0;
    if (!host || host >= input.size() || input[host] == L'/' || input[host] == L'?' || input[host] == L'#')
        return {input};
    size_t question = input.find(L'?', host), fragment = input.find(L'#', host);
    if (question == std::wstring::npos || (fragment != std::wstring::npos && fragment < question))
        return {input};
    size_t end = fragment == std::wstring::npos ? input.size() : fragment;
    std::vector<std::wstring> kept;
    bool removed = false;
    auto decodeKey = [](const std::wstring &value) {
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9')
                return c - L'0';
            c = static_cast<wchar_t>(std::towlower(c));
            return c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
        };
        std::wstring key;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == L'%' && i + 2 < value.size()) {
                int high = hex(value[i + 1]), low = hex(value[i + 2]);
                if (high >= 0 && low >= 0) {
                    key += static_cast<wchar_t>(high * 16 + low);
                    i += 2;
                    continue;
                }
            }
            key += value[i];
        }
        return Lower(std::move(key));
    };
    for (size_t at = question + 1; at <= end;) {
        size_t next = input.find(L'&', at);
        if (next == std::wstring::npos || next > end)
            next = end;
        auto pair = input.substr(at, next - at);
        auto key = decodeKey(pair.substr(0, pair.find(L'=')));
        if (key == L"x-amz-signature" || key == L"x-goog-signature" || key == L"signature" || key == L"sig")
            return {input, true};
        bool tracking = key.starts_with(L"utm_") || key == L"fbclid" || key == L"gclid" || key == L"dclid" ||
                        key == L"msclkid" || key == L"mc_cid" || key == L"mc_eid" || key == L"igshid" ||
                        key == L"_ga" || key == L"_gl";
        if (tracking)
            removed = true;
        else if (!pair.empty())
            kept.push_back(std::move(pair));
        if (next == end)
            break;
        at = next + 1;
    }
    if (!removed)
        return {input};
    std::wstring result = input.substr(0, question);
    for (size_t i = 0; i < kept.size(); ++i)
        result += (i ? L"&" : L"?") + kept[i];
    if (fragment != std::wstring::npos)
        result += input.substr(fragment);
    return {std::move(result)};
}
} // namespace command
