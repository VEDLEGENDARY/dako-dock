#include "Island.h"

void Island::RefreshControls() {
    if (renderMode || fixtureGeometry || !expanded || tab != Tab::Controls)
        return;
    HMONITOR monitor = panelMonitor ? panelMonitor : MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (controlState.monitor != monitor)
        controlState.brightnessSupported = false;
    if (!nativeControls)
        nativeControls = std::make_unique<controls::Service>(window);
    nativeControls->Refresh(monitor);
}
void Island::SetBrightnessAt(int x, bool commit) {
    auto rect = BrightnessRect();
    controlState.brightness =
        static_cast<int>(std::lround(std::clamp((x * 96.0f / dpi - PanelX()) / textScale - rect.left - 12, 0.0f,
                                                std::max(1.0f, rect.right - rect.left - 24)) /
                                     std::max(1.0f, rect.right - rect.left - 24) * 100));
    if (commit && controls::MayWrite(renderMode, fixtureGeometry, previewMode) && nativeControls &&
        PrepareSystemChange())
        nativeControls->SetBrightness(controlState.monitor, controlState.brightness);
    InvalidateRect(window, nullptr, FALSE);
}
void Island::ControlAction(int id) {
    if (id == OutputPrevious) {
        if (outputPage)
            --outputPage;
        return;
    }
    if (id == OutputNext) {
        ++outputPage;
        return;
    }
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        if (id == Dark)
            controlState.dark = !controlState.dark;
        if (id == Airplane && controlState.radiosAvailable)
            controlState.radiosOff = !controlState.radiosOff;
        note = L"Preview: Windows settings remain unchanged.";
        return;
    }
    if ((id == Dark || id == Airplane) && !PrepareSystemChange())
        return;
    if (id == Dark) {
        if (!nativeControls)
            RefreshControls();
        if (nativeControls)
            nativeControls->SetDark(!controlState.dark);
    } else if (id == Airplane) {
        if (!nativeControls || !controlState.radiosAvailable)
            return;
        try {
            auto consent = winrt::Windows::Devices::Radios::Radio::RequestAccessAsync();
            nativeControls->SetAirplane(!controlState.radiosOff, std::move(consent));
        } catch (const winrt::hresult_error &) {
            note = L"Radio control is unavailable. Open Airplane mode settings.";
        }
    } else if (id == OutputSettings)
        Open(L"ms-settings:sound");
    else if (id == AirplaneSettings)
        Open(L"ms-settings:network-airplanemode");
    else if (id == ScreenSnip)
        Open(L"ms-screenclip:");
    else if (id == ProjectDisplay) {
        wchar_t systemFolder[MAX_PATH]{};
        GetSystemDirectoryW(systemFolder, MAX_PATH);
        Open((std::wstring(systemFolder) + L"\\DisplaySwitch.exe").c_str());
    }
}
D2D1_RECT_F Island::BrightnessRect() const {
    auto b = Body();
    float y = b.top + (Layout().narrow ? 364.0f : 300.0f);
    return {b.left, y, b.right, y + 36};
}
void Island::OutputDevices() {
    auto b = Body();
    auto header = Layout().header;
    if (Layout().emergency)
        CompactHeader(true);
    else {
        SmallButton(FlyoutBack, {header.left, 8, header.left + 40, 44}, L"\xE72B", L"Back to controls");
        Text(L"Audio outputs", {header.left + 52, 8, header.right, 44}, type::Section);
    }
    Text(L"Change the default output in Sound settings.", {b.left, b.top, b.right, b.top + 32}, type::Secondary,
         theme::Muted);
    size_t slots = static_cast<size_t>(std::max(1.0f, std::floor((b.bottom - b.top - 40) / 56)));
    size_t count = controlState.outputs.size();
    if (outputPage * slots >= count)
        outputPage = 0;
    for (size_t slot = 0; slot < slots && outputPage * slots + slot < count; ++slot) {
        const auto &output = controlState.outputs[outputPage * slots + slot];
        float y = b.top + 40 + static_cast<float>(slot) * 56;
        D2D1_RECT_F rect{b.left, y, b.right, y + 48};
        Fill(rect, output.isDefault ? theme::AccentSoft : theme::Card, 12);
        AddHit(2100 + static_cast<int>(slot), rect,
               output.name + (output.isDefault ? L", default output" : L", available output"), false);
        Text(output.name, {rect.left + 12, y, rect.right - 12, y + 26}, type::Control,
             highContrast && output.isDefault ? theme::SelectedText : theme::Text);
        Text(output.isDefault ? L"Default output" : L"Available", {rect.left + 12, y + 24, rect.right - 12, y + 46},
             type::Caption, highContrast && output.isDefault ? theme::SelectedText : theme::Muted);
    }
    if (!count)
        Paragraph(nativeControls ? L"No active audio outputs. Open Sound settings to review devices."
                                 : L"No active audio outputs.",
                  {b.left + 12, b.top + 48, b.right - 12, b.bottom - 12});
    if (Layout().emergency)
        return;
    auto footer = Footer();
    TextButton(OutputSettings, {footer.left, footer.top, footer.right - 96, footer.bottom}, L"Open sound settings ↗");
    SmallButton(OutputPrevious, {footer.right - 84, footer.top, footer.right - 48, footer.bottom}, L"\xE76B",
                L"Previous outputs", outputPage > 0);
    SmallButton(OutputNext, {footer.right - 36, footer.top, footer.right, footer.bottom}, L"\xE76C", L"Next outputs",
                (outputPage + 1) * slots < count);
}
