#pragma once
#include <windows.h>
#include "EffectsJournal.h"
#include <memory>
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Radios.h>

namespace controls {
inline constexpr UINT Changed = WM_APP + 60;
struct Output {
    std::wstring id, name;
    bool isDefault = false;
};
struct Snapshot {
    HMONITOR monitor = nullptr;
    bool dark = false, darkKnown = false, brightnessSupported = false;
    int brightness = 50;
    bool radiosAvailable = false, radiosOff = false, radioBlocked = false;
    std::vector<Output> outputs;
    std::wstring message;
};
inline bool MayWrite(bool render, bool fixture, bool preview) {
    return !render && !fixture && !preview;
}
bool RestoreEffect(const effects::Values &values);
class Service {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Service(HWND window);
    ~Service();
    void Refresh(HMONITOR monitor);
    void SetDark(bool dark);
    void SetBrightness(HMONITOR monitor, int percent);
    void SetAirplane(
        bool off,
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Devices::Radios::RadioAccessStatus> permission,
        winrt::Windows::Devices::Radios::RadioKind kind = winrt::Windows::Devices::Radios::RadioKind::Other);
    Snapshot Read();
};
}
