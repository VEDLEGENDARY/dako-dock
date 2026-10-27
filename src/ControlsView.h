#pragma once
// Included by Island.cpp after the Island declaration.
void Island::MediaCard(bool compact) {
    auto b = Body();
    float top = compact
                    ? 4.0f
                    : b.top + (Layout().narrow ? 402.0f : 274.0f) + (controlState.brightnessSupported ? 58.0f : 0.0f),
          left = b.left, right = b.right;
    float cardHeight = compact ? std::min(116.0f, LayoutHeight() - 8) : 116.0f;
    float clusterHeight = std::clamp(cardHeight - 52, 28.0f, 60.0f), controlsTop = top + clusterHeight + 4,
          controlHeight = std::min(36.0f, cardHeight - clusterHeight - 16);
    Fill({left, top, right, top + cardHeight}, theme::Inset, 14);
    if (compact)
        AddHit(Expand, {left, top, right, top + clusterHeight}, L"Open full controls");
    AddHit(2000, {left, top, right, top + clusterHeight},
           currentMedia.title + L". " + currentMedia.artist + L". " + Clock(currentMedia.position) + L" of " +
               Clock(currentMedia.duration),
           false);
    D2D1::Matrix3x2F cluster;
    if (!layoutOnly) {
        target->GetTransform(&cluster);
        auto layer = D2D1::LayerParameters();
        layer.opacity = contentBlend.value;
        target->PushLayer(layer, nullptr);
        target->SetTransform(D2D1::Matrix3x2F::Translation(8 * (1 - contentBlend.value), 0) * cluster);
    }
    float art = std::min(42.0f, clusterHeight - 8), inset = clusterHeight < 44 ? 4.0f : 12.0f;
    Fill({left + 12, top + inset, left + 12 + art, top + inset + art}, theme::AccentSoft, 10);
    Text(L"\xE8D6", {left + 12, top + inset, left + 12 + art, top + inset + art}, type::Icon, theme::Accent,
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(currentMedia.title, {left + 24 + art, top, right - 12, top + (clusterHeight < 44 ? clusterHeight : 34)},
         type::Control);
    if (clusterHeight >= 44)
        Text(currentMedia.artist, {left + 24 + art, top + 34, right - 12, top + 54}, type::Secondary, theme::Muted);
    if (!layoutOnly) {
        target->SetTransform(cluster);
        target->PopLayer();
    }
    float center = (left + right) / 2;
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F rect{center - 66 + i * 48, controlsTop, center - 30 + i * 48, controlsTop + controlHeight};
        int id = Previous + i;
        bool enabled = currentMedia.available && (i == 0   ? currentMedia.canPrevious
                                                  : i == 1 ? currentMedia.canPlay
                                                           : currentMedia.canNext);
        AddHit(id, rect, i == 0 ? L"Previous track" : i == 1 ? L"Play or pause" : L"Next track", enabled);
        Fill(Tactile(id, rect), i == 1 && enabled ? theme::Accent : theme::CardHover, 18);
        Text(i == 0                 ? L"\xE892"
             : i == 2               ? L"\xE893"
             : currentMedia.playing ? L"\xE769"
                                    : L"\xE768",
             rect, type::Icon,
             i == 1 && enabled ? theme::AccentInk
             : enabled         ? theme::Text
                               : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(id, rect, 18);
    }
    float progress = static_cast<float>(
        currentMedia.duration > 0 ? std::clamp(currentMedia.position / currentMedia.duration, 0.0, 1.0) : 0);
    Line(left + 12, top + cardHeight - 8, right - 12, top + cardHeight - 8, theme::CardHover, 2);
    Line(left + 12, top + cardHeight - 8, left + 12 + (right - left - 24) * progress, top + cardHeight - 8,
         theme::Accent, 2);
    if (compact)
        FocusRing(Expand, {left, top, right, top + clusterHeight});
}

D2D1_RECT_F Island::VolumeRect() const {
    auto b = Body();
    float y = b.top + (Layout().narrow ? 310.0f : 182.0f);
    return {b.left, y, b.right, y + 36};
}

bool Island::HasAudio() const {
    return fixtureGeometry ? fixtureAudioAvailable : system.audio != nullptr;
}

void Island::Controls() {
    auto b = Layout().body;
    bool narrow = Layout().narrow;
    int columns = narrow ? 2 : 3;
    const int ids[]{Wifi, Bluetooth, Audio, Night, Dark, Airplane};
    const wchar_t *titles[]{L"Wi-Fi", L"Bluetooth", L"Audio outputs", L"Night light ↗", L"Dark mode", L"Airplane mode"};
    std::wstring subtitles[]{system.wifi,
                             system.bluetooth,
                             system.audioName,
                             L"Open Windows Settings",
                             controlState.dark ? L"On" : L"Off",
                             controlState.radiosOff ? L"Supported radios off" : L"Wi-Fi / Bluetooth radios"};
    const wchar_t *icons[]{L"\xE701", L"\xE702", L"\xE767", L"\xE708", L"\xE790", L"\xE709"};
    for (int i = 0; i < 6; ++i) {
        if (i == 5 && !controlState.radiosAvailable)
            continue;
        int col = i % columns, row = i / columns, count = columns;

        auto tile = layout::Column(b, col, count);
        tile.top = b.top + row * (narrow ? 88.0f : 74.0f);
        tile.bottom = tile.top + (narrow ? 76 : 62);
        Button(ids[i], R(tile), titles[i], subtitles[i], icons[i],
               (i == 0 && system.wifiConnected) ||
                   (i == 1 && std::any_of(connections.devices.begin(), connections.devices.end(),
                                          [](const BluetoothEntry &device) { return device.connected; })) ||
                   (i == 4 && controlState.dark) || (i == 5 && controlState.radiosOff));
        if (i == 5 && !controlState.radiosAvailable)
            hits.back().enabled = false;
    }
    auto slider = VolumeRect();
    AddHit(Volume, slider, L"System volume. " + Number(system.volume * 100) + L" percent", HasAudio());
    Fill(slider, theme::Inset, 18);
    float inset = 12, track = slider.right - slider.left - inset * 2;
    volumeMotion.goal = system.volume;
    if (reducedMotion || renderMode || draggingVolume)
        volumeMotion.Snap(volumeMotion.goal);
    float x = slider.left + inset + track * volumeMotion.value;
    Line(slider.left + inset, slider.top + 18, slider.right - inset, slider.top + 18, theme::Muted, 3);
    Line(slider.left + inset, slider.top + 18, x, slider.top + 18, theme::Accent, 3);
    if (!layoutOnly) {
        brush->SetColor(Color(theme::Text));
        target->FillEllipse(D2D1::Ellipse({x, slider.top + 18}, 6, 6), brush.Get());
    }
    Text(L"OUTPUT VOLUME", {slider.left, slider.top - 28, slider.right - 140, slider.top - 4}, type::Mono,
         theme::Muted);
    Text(system.muted ? L"Muted"
         : HasAudio() ? Number(system.volume * 100) + L"%"
                      : L"No audio device",
         {slider.right - 135, slider.top - 28, slider.right, slider.top - 4}, type::Mono, theme::Text,
         DWRITE_TEXT_ALIGNMENT_TRAILING);
    FocusRing(Volume, slider, 18);
    if (controlState.brightnessSupported) {
        auto brightness = BrightnessRect();
        Text(L"DISPLAY BRIGHTNESS", {brightness.left, brightness.top - 26, brightness.right - 60, brightness.top - 4},
             type::Mono, theme::Muted);
        Text(std::to_wstring(controlState.brightness) + L"%",
             {brightness.right - 60, brightness.top - 26, brightness.right, brightness.top - 4}, type::Mono,
             theme::Text, DWRITE_TEXT_ALIGNMENT_TRAILING);
        AddHit(Brightness, brightness,
               L"Selected display brightness. " + std::to_wstring(controlState.brightness) + L" percent");
        Fill(brightness, theme::Inset, 18);
        float knob = brightness.left + 12 + (brightness.right - brightness.left - 24) * controlState.brightness / 100;
        Line(brightness.left + 12, brightness.top + 18, brightness.right - 12, brightness.top + 18, theme::Muted, 3);
        Line(brightness.left + 12, brightness.top + 18, knob, brightness.top + 18, theme::Accent, 3);
        if (!layoutOnly) {
            brush->SetColor(Color(theme::Text));
            target->FillEllipse(D2D1::Ellipse({knob, brightness.top + 18}, 6, 6), brush.Get());
        }
        FocusRing(Brightness, brightness, 18);
    }
    float shortcuts = b.top + (narrow ? 358.0f : 230.0f) + (controlState.brightnessSupported ? 58.0f : 0.0f);
    auto area = layout::Rect{b.left, shortcuts, b.right, shortcuts + 32};
    const int actions[]{ProjectDisplay, ScreenSnip, AirplaneSettings};
    const wchar_t *labels[]{L"Project ↗", L"Screen snip ↗", L"Radio settings ↗"};
    for (int i = 0; i < 3; ++i)
        TextButton(actions[i], R(layout::Column(area, i, 3)), labels[i]);
    MediaCard(false);
}
