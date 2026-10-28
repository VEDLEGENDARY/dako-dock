#include "Island.h"

void Island::OpenFlyout(Flyout next) {
    previousPane.Reset();
    paneBlend.Snap(1);
    flyout = next;
    connectionPage = 0;
    selectedWifi = selectedBluetooth = -1;
    focused = hovered = -1;
    if (wifiPassword)
        SetWindowTextW(wifiPassword, L"");
    if (!renderMode && !fixtureGeometry) {
        if (!connectivity)
            connectivity = std::make_unique<Connectivity>(window);
        if (next == Flyout::Wifi)
            connectivity->RefreshWifi(true);
        else if (next == Flyout::Bluetooth)
            connectivity->RefreshBluetooth();
    }
    SetExpanded(true);
}

void Island::EnsurePassword() {
    if (wifiPassword || renderMode || capturingPane || layoutOnly)
        return;
    wifiPassword = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 0, 0, 1, 1,
                                   window, reinterpret_cast<HMENU>(402), nullptr, nullptr);
    LabelInput(wifiPassword, L"Network security key");
    EnsureEditResources();
    SendMessageW(wifiPassword, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), TRUE);
    SendMessageW(wifiPassword, EM_SETLIMITTEXT, 64, 0);
    SendMessageW(wifiPassword, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Network security key"));
    PositionInputs();
}

size_t Island::ConnectionSlots() const {
    bool selected = flyout == Flyout::Wifi ? selectedWifi >= 0 : selectedBluetooth >= 0;
    float available = std::max(Layout().body.Height(), Layout().contentHeight);
    return static_cast<size_t>(std::clamp(std::floor((available - 76 - (selected ? 156.0f : 0.0f)) / 56), 1.0f, 5.0f));
}

void Island::ConnectionView() {
    auto l = Layout();
    float w = l.panel.Width();
    bool wifi = flyout == Flyout::Wifi, busy = wifi ? connections.wifiBusy : connections.bluetoothBusy,
         enabled = wifi ? connections.wifiOn : connections.bluetoothOn,
         available = wifi ? connections.wifiAvailable : connections.bluetoothAvailable;
    if (l.emergency)
        CompactHeader(true);
    else {
        SmallButton(FlyoutBack, {20, 8, 56, 44}, L"\xE72B", L"Back to controls");
        Text(wifi ? L"Wi-Fi" : L"Bluetooth", {68, 8, w - 132, 44}, type::Control);
        SmallButton(FlyoutRefresh, {w - 116, 8, w - 80, 44}, L"\xE72C", wifi ? L"Refresh networks" : L"Find devices",
                    !busy);
        D2D1_RECT_F toggle{w - 68, 14, w - 20, 38};
        AddHit(FlyoutToggle, toggle, enabled ? L"Turn radio off" : L"Turn radio on", available && !busy);
        Fill(toggle, enabled ? theme::Accent : theme::CardHover, 12);
        togglePosition.goal = enabled ? 1.0f : 0.0f;
        if (reducedMotion || renderMode)
            togglePosition.Snap(togglePosition.goal);
        if (!layoutOnly) {
            brush->SetColor(Color(enabled ? theme::AccentInk : theme::Muted));
            target->FillEllipse(D2D1::Ellipse({w - 56 + 24 * togglePosition.value, 26}, 8, 8), brush.Get());
        }
        FocusRing(FlyoutToggle, toggle, 12);
    }
    auto b = Body();
    D2D1::Matrix3x2F saved;
    if (!layoutOnly) {
        target->GetTransform(&saved);
        target->PushAxisAlignedClip(b, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        auto layer = D2D1::LayerParameters();
        layer.opacity = std::clamp(paneBlend.value, 0.0f, 1.0f);
        target->PushLayer(layer, nullptr);
        target->SetTransform(D2D1::Matrix3x2F::Translation(bodyDirection * 12 * (1 - paneBlend.value), -scroll) *
                             saved);
    }
    buildingBody = true;
    const auto &message = wifi ? connections.wifiMessage : connections.bluetoothMessage;
    AddHit(2040, {b.left, b.top, b.right, b.top + 44},
           message.empty() ? (busy   ? L"Searching"
                              : wifi ? L"Available networks"
                                     : L"Your devices")
                           : message,
           false);
    Paragraph(message.empty() ? (busy   ? L"Searching…"
                                 : wifi ? L"Available networks"
                                        : L"Your devices")
                              : message,
              {b.left, b.top, b.right, b.top + 44});
    size_t count = wifi ? connections.networks.size() : connections.devices.size(), slots = ConnectionSlots();
    for (size_t slot = 0; slot < slots && connectionPage * slots + slot < count; ++slot) {
        size_t index = connectionPage * slots + slot;
        int id = (wifi ? 1000 : 1200) + static_cast<int>(index);
        float y = RowY(StableKey(id), b.top + 52 + static_cast<float>(slot) * 56);
        bool selected = wifi ? static_cast<int>(index) == selectedWifi : static_cast<int>(index) == selectedBluetooth;
        bool connected = wifi ? connections.networks[index].connected : connections.devices[index].connected;
        std::wstring name = wifi ? connections.networks[index].name : connections.devices[index].name,
                     status = connected ? L"Connected"
                              : wifi    ? (connections.networks[index].secure ? L"Secured" : L"Open network")
                              : connections.devices[index].paired ? L"Paired"
                                                                  : L"Ready to pair";
        D2D1_RECT_F row{b.left, y, b.right, y + 48};
        AddHit(id, row, name + L". " + status);
        Fill(Tactile(id, row), selected ? theme::AccentSoft : theme::Card, 12);
        Text(wifi ? L"\xE701" : L"\xE702", {b.left + 12, y + 8, b.left + 40, y + 40}, type::Icon,
             highContrast && selected ? theme::SelectedText
             : connected              ? theme::Accent
                                      : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(name, {b.left + 52, y + 2, b.right - 12, y + 26}, type::Body,
             highContrast && selected ? theme::SelectedText : theme::Text);
        Text(status, {b.left + 52, y + 26, b.right - 12, y + 46}, type::Secondary,
             highContrast && selected ? theme::SelectedText : theme::Muted);
        FocusRing(id, row);
    }
    float detail = b.top + 52 + static_cast<float>(slots) * 56 + 12;
    if (!count)
        Paragraph(busy         ? L"Searching…"
                  : !available ? L"No adapter available. Use More settings."
                  : !enabled   ? L"Radio is off."
                               : L"No results. Try Refresh.",
                  {b.left + 12, b.top + 76, b.right - 12, b.top + 160});
    if (wifi && selectedWifi >= 0 && selectedWifi < static_cast<int>(connections.networks.size())) {
        const auto &network = connections.networks[selectedWifi];
        Fill({b.left, detail, b.right, detail + 120}, theme::Inset, 12);
        bool needs = (renderMode || fixtureGeometry) && network.secure && network.profile.empty() && !network.connected;
        if (needs) {
            D2D1_RECT_F input{b.left + 12, detail + 12, b.right - 12,
                              detail + 12 + std::min(32.0f, Layout().body.Height())};
            Fill(input, theme::CardHover, 8);
            AddHit(402, input, L"Network security key, 8 to 63 characters or 64 hexadecimal digits");
            EnsurePassword();
            if (renderMode)
                Text(L"Network security key", input, type::Secondary, theme::Muted);
        } else
            Paragraph(network.connected ? L"Connected to " + network.name : L"Windows saved profile or open network.",
                      {b.left + 12, detail + 8, b.right - 12, detail + 52});
        bool can = network.connected || !needs || (wifiPassword && GetWindowTextLengthW(wifiPassword) > 0);
        TextButton(network.connected ? FlyoutDisconnect : FlyoutConnect,
                   {b.right - 164, detail + 64, b.right - 12, detail + 104}, L"Wi-Fi settings ↗", !network.connected,
                   can && !connections.wifiConnecting && !busy);
    }
    if (!wifi && selectedBluetooth >= 0 && selectedBluetooth < static_cast<int>(connections.devices.size())) {
        const auto &device = connections.devices[selectedBluetooth];
        Fill({b.left, detail, b.right, detail + 120}, theme::Inset, 12);
        Text(device.name, {b.left + 12, detail + 6, b.right - 12, detail + 42}, type::Body);
        TextButton(device.connected ? FlyoutDisconnect
                   : device.paired  ? FlyoutConnect
                                    : FlyoutPair,
                   {b.right - 160, detail + 60, b.right - 12, detail + 100}, L"Bluetooth settings ↗", !device.connected,
                   !busy);
    }
    buildingBody = false;
    if (!layoutOnly) {
        target->SetTransform(saved);
        target->PopLayer();
        target->PopAxisAlignedClip();
    }
    if (!l.emergency) {
        auto f = Footer();
        TextButton(FlyoutSettings, {f.left, f.top, f.right - 104, f.bottom}, L"More settings ↗");
        SmallButton(FlyoutPrevious, {f.right - 92, f.top, f.right - 52, f.bottom}, L"\xE76B", L"Previous devices",
                    connectionPage > 0);
        SmallButton(FlyoutNext, {f.right - 40, f.top, f.right, f.bottom}, L"\xE76C", L"Next devices",
                    (connectionPage + 1) * slots < count);
    }
    if (!l.emergency && !note.empty())
        Paragraph(note, R(l.status), theme::Text, type::Caption);
    else if (l.contentHeight > l.body.Height())
        Text(L"Scroll for device details", R(l.status), type::Caption, theme::Muted);
}

void Island::RefreshConnectionsView() {
    if (!connectivity)
        return;
    std::wstring networkKey = selectedWifi >= 0 && selectedWifi < static_cast<int>(connections.networks.size())
                                  ? WifiKey(connections.networks[selectedWifi])
                                  : L"";
    std::wstring deviceKey = selectedBluetooth >= 0 && selectedBluetooth < static_cast<int>(connections.devices.size())
                                 ? BluetoothKey(connections.devices[selectedBluetooth])
                                 : L"";
    connections = connectivity->Snapshot();
    selectedWifi = selectedBluetooth = -1;
    for (size_t i = 0; i < connections.networks.size(); ++i)
        if (WifiKey(connections.networks[i]) == networkKey)
            selectedWifi = static_cast<int>(i);
    for (size_t i = 0; i < connections.devices.size(); ++i)
        if (BluetoothKey(connections.devices[i]) == deviceKey)
            selectedBluetooth = static_cast<int>(i);
    if (selectedWifi < 0 && wifiPassword)
        SetWindowTextW(wifiPassword, L"");
    system.wifi = connections.wifiAvailable ? (connections.wifiOn ? L"Not connected" : L"Off") : L"Not available";
    system.wifiConnected = false;
    for (const auto &network : connections.networks)
        if (network.connected) {
            system.wifi = network.name;
            system.wifiConnected = true;
            break;
        }
    system.bluetooth = connections.bluetoothAvailable ? (connections.bluetoothOn ? L"On" : L"Off") : L"Not available";
    for (const auto &device : connections.devices)
        if (device.connected) {
            system.bluetooth = device.name;
            break;
        }
    int selected = flyout == Flyout::Wifi ? selectedWifi : selectedBluetooth;
    size_t count = flyout == Flyout::Wifi ? connections.networks.size() : connections.devices.size(),
           slots = ConnectionSlots();
    if (selected >= 0)
        connectionPage = static_cast<size_t>(selected) / slots;
    else if (connectionPage * slots >= count)
        connectionPage = 0;
    UpdateDocks();
    RebuildLayout();
    PositionInputs();
    if (expanded) {
        ArmMotion();
        InvalidateRect(window, nullptr, FALSE);
    }
}
