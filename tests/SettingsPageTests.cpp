#include "../src/SettingsPageModel.h"
#include <cstdio>
#include <stdexcept>

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        Require(settingspage::Uri(L" MS-SETTINGS:SOUND ") == L"ms-settings:sound",
                "base normalization remains available");
        for (const auto *value :
             {L"ms-settings:defaultapps?registeredAppMachine=Microsoft%20Edge",
              L"ms-settings:defaultapps?registeredAppUser=Editor%20%26%20Tools",
              L"ms-settings:defaultapps?registeredAUMID=Example_8wekyb3d8bbwe!App",
              L"ms-settings:appsfeatures-app?Microsoft.WindowsCalculator_8wekyb3d8bbwe",
              L"ms-settings:camera?cameraId=%5C%5C%3F%5Cusb%23fixture",
              L"ms-settings:sound-properties?endpointId={0.0.0.00000000}.{aaaaaaaa-0000-1111-2222-bbbbbbbbbbbb}",
              L"ms-settings:sound-properties?interfaceId=%5C%5C%3F%5CSWD%23MMDEVAPI%23fixture",
              L"ms-settings:defaultapps?registeredAppUser=%E6%97%A5%E6%9C%AC%E8%AA%9E"}) {
            auto uri = settingspage::Uri(value);
            Require(uri && uri->size() <= 1024 && settingspage::Uri(*uri) == uri,
                    "documented app/device parameters canonicalize idempotently");
        }
        Require(settingspage::Uri(L"ms-settings:defaultapps?registeredAUMID=Example!App") ==
                    L"ms-settings:defaultapps?registeredAUMID=Example%21App",
                "reserved characters are escaped in values");
        Require(settingspage::Uri(L"ms-settings:defaultapps?registeredAppUser=A%2526B") ==
                    L"ms-settings:defaultapps?registeredAppUser=A%2526B",
                "literal percent escapes are decoded once rather than repeatedly");
        for (const auto *bad : {L"https://example.com",
                                L"shell:AppsFolder",
                                L"javascript:alert",
                                L"ms-settings:sound#fragment",
                                L"ms-settings:display?monitorId=1",
                                L"ms-settings:camera?unknown=fixture",
                                L"ms-settings:defaultapps?registeredappuser=Editor",
                                L"ms-settings:defaultapps?registeredAppUser=",
                                L"ms-settings:defaultapps?registeredAppUser=Editor&registeredAppMachine=Other",
                                L"ms-settings:defaultapps?registeredAppUser=Editor#fragment",
                                L"ms-settings:defaultapps?registeredAppUser=Editor=Other",
                                L"ms-settings:defaultapps?registeredAppUser=Raw Space",
                                L"ms-settings:defaultapps?registeredAppUser=日本語",
                                L"ms-settings:defaultapps?registeredAppUser=%",
                                L"ms-settings:defaultapps?registeredAppUser=%0",
                                L"ms-settings:defaultapps?registeredAppUser=%GG",
                                L"ms-settings:defaultapps?registeredAppUser=%00",
                                L"ms-settings:defaultapps?registeredAppUser=%0A",
                                L"ms-settings:defaultapps?registeredAppUser=%7F",
                                L"ms-settings:defaultapps?registeredAppUser=%C0%AF",
                                L"ms-settings:defaultapps?registeredAppUser=%ED%A0%80",
                                L"ms-settings:appsfeatures-app?not-a-package",
                                L"ms-settings:appsfeatures-app?package=Example",
                                L"ms-settings:sound-properties?endpointId=fixture?other=1",
                                L"ms-settings:camera?cameraId=%FF"})
            Require(!settingspage::Uri(bad), "malformed/unsupported parameter routes fail closed");
        Require(!settingspage::Uri(L"ms-settings:camera?cameraId=" + std::wstring(1000, L'a')),
                "whole URI bound enforced");
        Require(!settingspage::Uri(L"ms-settings:camera?cameraId=" + std::wstring(513, L'a')),
                "decoded value bound enforced");
        std::wstring nul = L"ms-settings:camera?cameraId=a";
        nul += L'\0';
        nul += L'b';
        Require(!settingspage::Uri(nul), "embedded NUL rejected before activation");
        for (int first = 0; first < 256; ++first) {
            constexpr wchar_t hex[] = L"0123456789ABCDEF";
            std::wstring candidate = L"ms-settings:camera?cameraId=%";
            candidate += hex[first >> 4];
            candidate += hex[first & 15];
            auto result = settingspage::Uri(candidate);
            if (result)
                Require(settingspage::Uri(*result) == result, "byte-level canonicalization remains stable");
        }
        std::puts("PASS bounded documented Settings parameter routes, UTF-8/escape validation and canonical identity; "
                  "no launches");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
