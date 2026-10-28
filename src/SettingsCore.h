#pragma once
#include "SettingsStore.h"
#include "SettingsSchema.h"
#include <memory>
#include <optional>

namespace v4 {
inline constexpr UINT SettingsChanged = WM_APP + 84;
struct Preview {
    DWORD magic = 0x44414B34, command = 0;
    wchar_t key[64]{}, value[1025]{};
};
Values ReadValues(const std::wstring &path = {});
bool SaveValues(const Values &values, const std::wstring &path = {});
void SignalSettings(const std::wstring &channel = {});
bool SendPreview(const Preview &packet, const std::wstring &channel = {});
class SettingsWatch {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit SettingsWatch(HWND owner, std::wstring channel = {});
    ~SettingsWatch();
    std::optional<Preview> Consume();
};
} // namespace v4
