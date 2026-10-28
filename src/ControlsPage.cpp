#include "Island.h"

void Island::MediaCard(bool compact) {
    auto b = Body();
    float top = compact ? 4.0f : b.top + (Layout().narrow ? 236.0f : 174.0f), left = b.left, right = b.right;
    float cardHeight =
        compact ? std::min(layout::Metrics::MediaBodyHeight, LayoutHeight() - 8) : layout::Metrics::MediaBodyHeight;
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
    if (!layoutOnly && currentMedia.artwork) {
        ComPtr<ID2D1Bitmap> artwork;
        if (SUCCEEDED(target->CreateBitmap(
                D2D1::SizeU(64, 64), currentMedia.artwork->data(), 64 * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                &artwork)))
            target->DrawBitmap(artwork.Get(), {left + 12, top + inset, left + 12 + art, top + inset + art});
    }
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
    if (options.Bool(L"MediaScrubber")) {
        Line(left + 12, top + cardHeight - 8, right - 12, top + cardHeight - 8, theme::CardHover, 2);
        Line(left + 12, top + cardHeight - 8, left + 12 + (right - left - 24) * progress, top + cardHeight - 8,
             theme::Accent, 2);
        Text(Clock(currentMedia.position), {left + 12, top + cardHeight - 27, left + 58, top + cardHeight - 8},
             type::Caption, theme::Muted);
        Text(Clock(currentMedia.duration), {right - 64, top + cardHeight - 27, right - 12, top + cardHeight - 8},
             type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        AddHit(2970, {left + 12, top + cardHeight - 14, right - 12, top + cardHeight}, L"Media playback position",
               currentMedia.available && currentMedia.duration > 0);
    }
    if (compact)
        FocusRing(Expand, {left, top, right, top + clusterHeight});
}

D2D1_RECT_F Island::VolumeRect() const {
    auto b = Body();
    float y = b.top + (Layout().narrow ? 196.0f : 132.0f);
    return {b.left, y, b.right, y + 34};
}
bool Island::HasAudio() const { return fixtureGeometry ? fixtureAudioAvailable : system.audio != nullptr; }

void Island::Controls() {
    auto b = Body();
    bool narrow = Layout().narrow;
    const int ids[]{Wifi, Bluetooth, Audio, Night, Dark};
    const wchar_t *names[]{L"Wi-Fi", L"Bluetooth", L"Audio", L"Night Light ↗", L"Dark mode"};
    const wchar_t *icons[]{L"\xE701", L"\xE702", L"\xE767", L"\xE708", L"\xE790"};
    std::wstring details[]{system.wifi, system.bluetooth, system.audioName, L"Windows Settings",
                           controlState.dark ? L"On" : L"Off"};
    for (int i = 0; i < 5; ++i) {
        int columns = narrow ? 2 : i < 3 ? 3 : 2;
        int col = narrow ? i % 2 : i < 3 ? i : i - 3;
        int row = narrow ? i / 2 : i < 3 ? 0 : 1;
        auto cell = layout::Column({b.left, b.top + row * 62.0f, b.right, b.top + row * 62.0f + 54}, col, columns, 8);
        Button(ids[i], R(cell), names[i], details[i], icons[i],
               i == 0   ? system.wifiConnected
               : i == 1 ? std::any_of(connections.devices.begin(), connections.devices.end(),
                                      [](const auto &device) { return device.connected; })
               : i == 2 ? HasAudio()
                        : i == 4 && controlState.dark);
    }
    auto slider = VolumeRect();
    Fill(slider, theme::Card, 17);
    auto filled = slider;
    filled.right = filled.left + (filled.right - filled.left) * system.volume;
    if (system.volume > 0)
        Fill(filled, theme::Accent, 17);
    AddHit(Volume, slider, L"Volume. " + Number(system.volume * 100) + L" percent", HasAudio());
    Text(L"\xE767", {slider.left + 12, slider.top, slider.left + 40, slider.bottom}, type::Icon, theme::AccentInk);
    Text(Number(system.volume * 100) + L"%", {slider.right - 64, slider.top, slider.right - 12, slider.bottom},
         type::Caption, theme::Text, DWRITE_TEXT_ALIGNMENT_TRAILING);
    FocusRing(Volume, slider, 17);
    MediaCard(false);
    if (controlState.brightnessSupported) {
        auto rect = BrightnessRect();
        TextButton(Brightness, rect, L"Brightness " + std::to_wstring(controlState.brightness) + L"%");
    }
}
