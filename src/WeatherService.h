#pragma once
#include "TodayNetwork.h"
#include "SettingsStore.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

struct WeatherState {
    bool available = false;
    double temperature = 0, high = 0, low = 0;
    int code = 0;
    std::wstring city, message = L"Weather is off. Choose a city to enable it.";
};
class WeatherService {
    today::Fetcher<WeatherState> fetcher_;
    std::wstring requestedCity_;
    ULONGLONG requestedAt_ = 0;

  public:
    void Invalidate() {
        requestedAt_ = 0;
        requestedCity_.clear();
    }
    WeatherState Snapshot() { return fetcher_.Snapshot(); }
    static WeatherState ParseForecast(const std::wstring &text, const std::wstring &city) {
        using namespace winrt::Windows::Data::Json;
        auto forecast = JsonObject::Parse(text);
        auto current = forecast.GetNamedObject(L"current"), daily = forecast.GetNamedObject(L"daily");
        WeatherState result;
        result.temperature = current.GetNamedNumber(L"temperature_2m");
        result.code = static_cast<int>(current.GetNamedNumber(L"weather_code"));
        result.high = daily.GetNamedArray(L"temperature_2m_max").GetNumberAt(0);
        result.low = daily.GetNamedArray(L"temperature_2m_min").GetNumberAt(0);
        result.available = true;
        result.city = city;
        result.message = L"Open-Meteo · °C";
        return result;
    }
    static std::wstring Condition(int code) {
        if (code == 0)
            return L"Clear sky";
        if (code <= 3)
            return L"Cloudy";
        if (code <= 48)
            return L"Fog";
        if (code <= 67)
            return L"Rain";
        if (code <= 77)
            return L"Snow";
        if (code <= 82)
            return L"Showers";
        if (code <= 86)
            return L"Snow showers";
        return L"Thunderstorms";
    }
    void Refresh(HWND window, std::wstring city, bool persist) {
        if (city.empty() || fetcher_.Busy())
            return;
        ULONGLONG interval = fetcher_.Available() ? 30 * 60 * 1000 : 60000;
        if (requestedCity_ == city && GetTickCount64() - requestedAt_ < interval)
            return;
        requestedCity_ = city;
        requestedAt_ = GetTickCount64();
        fetcher_.Request(window, [city = std::move(city), persist](const std::atomic<bool> &stop) {
            using namespace winrt::Windows::Data::Json;
            WeatherState result;
            result.city = city;
            result.message = L"Weather unavailable. Reopen Today to retry.";
            FILETIME file{};
            GetSystemTimeAsFileTime(&file);
            ULARGE_INTEGER now{file.dwLowDateTime, file.dwHighDateTime};
            auto cached = SettingsStore().Get(L"WeatherCache");
            try {
                auto timestamp = std::stoull(cached[L"Time"]);
                if (cached[L"City"] == city && now.QuadPart >= timestamp && now.QuadPart - timestamp < 18000000000ULL) {
                    result.temperature = std::stod(cached[L"Temperature"]);
                    result.high = std::stod(cached[L"High"]);
                    result.low = std::stod(cached[L"Low"]);
                    result.code = std::stoi(cached[L"Code"]);
                    result.available = true;
                    result.message = L"Cached · Open-Meteo";
                    return result;
                }
            } catch (...) {
            }
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try {
                auto location =
                    JsonObject::Parse(today::Get(L"https://geocoding-api.open-meteo.com/v1/search?name=" +
                                                     today::Escape(city) + L"&count=1&language=en&format=json",
                                                 stop, L"weather-city-search"));
                auto places = location.GetNamedArray(L"results");
                if (places.Size()) {
                    auto place = places.GetObjectAt(0);
                    auto url = L"https://api.open-meteo.com/v1/forecast?latitude=" +
                               std::to_wstring(place.GetNamedNumber(L"latitude")) + L"&longitude=" +
                               std::to_wstring(place.GetNamedNumber(L"longitude")) +
                               L"&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min" +
                               L"&forecast_days=1&timezone=auto";
                    auto resolvedCity = std::wstring(place.GetNamedString(L"name")) + L", " +
                                        std::wstring(place.GetNamedString(L"country_code", L""));
                    result = ParseForecast(today::Get(url, stop, L"weather-forecast"), resolvedCity);
                    if (persist && !stop)
                        SettingsStore().Update(L"WeatherCache", {{L"City", city},
                                                                 {L"Time", std::to_wstring(now.QuadPart)},
                                                                 {L"Temperature", std::to_wstring(result.temperature)},
                                                                 {L"High", std::to_wstring(result.high)},
                                                                 {L"Low", std::to_wstring(result.low)},
                                                                 {L"Code", std::to_wstring(result.code)}});
                }
            } catch (...) {
            }
            winrt::uninit_apartment();
            return result;
        });
    }
};
