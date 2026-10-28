#include "Island.h"

void Island::MediaCard(bool compact) {
    auto l = Layout();
    auto rect = compact ? l.body : layout::ControlsColumns(l)[1];
    if (compact)
        rect.bottom = std::min(rect.bottom, rect.top + layout::Metrics::MediaBodyHeight);
    Card(R(rect));
    float cx = rect.CenterX();
    if (!currentMedia.available) {
        Text(L"\xE8D6", {rect.left, rect.top + 24, rect.right, rect.top + 72}, type::LargeIcon, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(L"Nothing playing", {rect.left + 12, rect.top + 76, rect.right - 12, rect.top + 104}, type::Body,
             theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (compact)
            AddHit(Expand, R(rect), L"Open full controls");
        return;
    }
    float artSize = compact ? 40.0f : 120.0f;
    D2D1_RECT_F art{compact ? rect.left + 12 : cx - artSize / 2, rect.top + (compact ? 4.0f : 16.0f),
                    compact ? rect.left + 12 + artSize : cx + artSize / 2,
                    rect.top + (compact ? 4.0f : 16.0f) + artSize};
    Fill(art, theme::Inset, layout::CardRadius);
    Text(L"\xE8D6", art, type::LargeIcon, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!layoutOnly && currentMedia.artwork) {
        if (mediaPixels != currentMedia.artwork || !mediaBitmap) {
            mediaBitmap.Reset();
            mediaPixels = currentMedia.artwork;
            target->CreateBitmap(
                D2D1::SizeU(64, 64), mediaPixels->data(), 64 * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                &mediaBitmap);
        }
        Photo(mediaBitmap.Get(), art);
    }
    if (compact) {
        TwoLine({rect.left + 64, rect.top, rect.right - 12, rect.top + 44}, currentMedia.title, currentMedia.artist);
        AddHit(Expand, {rect.left, rect.top, rect.right, rect.top + 44}, L"Open full controls");
    } else {
        Text(currentMedia.title, {rect.left + 16, rect.top + 140, rect.right - 16, rect.top + 164}, type::Title,
             theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(currentMedia.artist, {rect.left + 16, rect.top + 164, rect.right - 16, rect.top + 184}, type::Caption,
             theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    AddHit(2000, R(rect),
           currentMedia.title + L". " + currentMedia.artist + L". " + Clock(currentMedia.position) + L" of " +
               Clock(currentMedia.duration),
           false);
    float y = rect.top + (compact ? 48.0f : 188.0f), playSize = compact ? 32.0f : 48.0f;
    for (int i = 0; i < 3; ++i) {
        float size = i == 1 ? playSize : 32.0f;
        float x = cx + (i - 1) * (compact ? 44.0f : 58.0f);
        D2D1_RECT_F button{x - size / 2, y + (playSize - size) / 2, x + size / 2, y + (playSize + size) / 2};
        int id = Previous + i;
        bool enabled = i == 0 ? currentMedia.canPrevious : i == 1 ? currentMedia.canPlay : currentMedia.canNext;
        AddHit(id, button, i == 0 ? L"Previous track" : i == 1 ? L"Play or pause" : L"Next track", enabled);
        Fill(Tactile(id, button),
             i == 1 && enabled ? theme::Accent : dock::Mix(theme::Card, theme::CardHover, HoverAmount(id)), size / 2);
        Text(i == 0                 ? L"\xE892"
             : i == 2               ? L"\xE893"
             : currentMedia.playing ? L"\xE769"
                                    : L"\xE768",
             button, type::Icon,
             i == 1 && enabled ? theme::AccentInk
             : enabled         ? theme::Text
                               : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(id, button, size / 2);
    }
    if (options.Bool(L"MediaScrubber")) {
        float trackY = rect.bottom - 8;
        float progress = currentMedia.duration > 0
                             ? static_cast<float>(std::clamp(currentMedia.position / currentMedia.duration, 0.0, 1.0))
                             : 0;
        Line(rect.left + 16, trackY, rect.right - 16, trackY, theme::Inset, 2);
        float position = rect.left + 16 + (rect.Width() - 32) * progress;
        Line(rect.left + 16, trackY, position, trackY, theme::Accent, 2);
        if (!layoutOnly) {
            brush->SetColor(Color(theme::Text));
            target->FillEllipse(D2D1::Ellipse({position, trackY}, 3, 3), brush.Get());
        }
        Text(Clock(currentMedia.position), {rect.left + 16, rect.bottom - 32, cx, rect.bottom - 12}, type::Caption,
             theme::Muted);
        Text(Clock(currentMedia.duration), {cx, rect.bottom - 32, rect.right - 16, rect.bottom - 12}, type::Caption,
             theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        AddHit(2970, {rect.left + 16, rect.bottom - 32, rect.right - 16, rect.bottom}, L"Media playback position",
               currentMedia.duration > 0);
    }
}

D2D1_RECT_F Island::VolumeRect() const {
    auto left = layout::ControlsColumns(Layout())[0];
    return {left.left, left.bottom - 72, left.right, left.bottom};
}
D2D1_RECT_F Island::VolumeTrack() const {
    auto r = VolumeRect();
    return {r.left + 52, r.bottom - 24, r.right - 20, r.bottom - 20};
}
bool Island::HasAudio() const { return fixtureGeometry ? fixtureAudioAvailable : system.audio != nullptr; }

void Island::Controls() {
    auto l = Layout();
    auto left = layout::ControlsColumns(l)[0];
    auto grid = left;
    grid.bottom -= 84;
    auto rows = l.narrow ? layout::Rows(grid, {1, 1, 1}) : layout::Rows(grid, {1, 1});
    const int ids[]{Wifi, Bluetooth, Audio, Night, Dark, Brightness};
    const wchar_t *names[]{L"Wi-Fi", L"Bluetooth", L"Audio", L"Night Light", L"Dark mode", L"Brightness"};
    const wchar_t *icons[]{L"\xE701", L"\xE702", L"\xE767", L"\xE708", L"\xE790", L"\xE706"};
    std::wstring details[]{system.wifi,
                           system.bluetooth,
                           system.audioName,
                           L"Windows Settings",
                           controlState.dark ? L"On" : L"Off",
                           Number(controlState.brightness) + L"%"};
    int columns = l.narrow ? 2 : 3;
    for (int i = 0; i < 6; ++i) {
        PaintGroup reveal(*this, i);
        auto cells =
            l.narrow ? layout::Columns(rows[i / columns], {1, 1}) : layout::Columns(rows[i / columns], {1, 1, 1});
        bool active = i == 0   ? system.wifiConnected
                      : i == 1 ? std::any_of(connections.devices.begin(), connections.devices.end(),
                                             [](const auto &device) { return device.connected; })
                      : i == 4 ? controlState.dark
                               : false;
        if (i == 5 && !controlState.brightnessSupported)
            Button(FocusStart, R(cells[i % columns]), L"Focus", focusRunning ? L"Running" : L"Start a session",
                   L"\xE916", focusRunning);
        else
            Button(ids[i], R(cells[i % columns]), names[i], details[i], icons[i], active, i != 2 || HasAudio());
    }
    auto volume = VolumeRect(), track = VolumeTrack();
    {
        PaintGroup reveal(*this, 6);
        Card(volume);
        Text(L"\xE767", {volume.left + 16, volume.top + 16, volume.left + 40, volume.bottom - 16}, type::TileIcon,
             HasAudio() ? theme::Text : theme::Muted);
        Text(L"Volume", {track.left, volume.top + 8, volume.right - 72, volume.top + 36}, type::Label);
        Text(Number(system.volume * 100) + L"%",
             {volume.right - 68, volume.top + 8, volume.right - 16, volume.top + 36}, type::Caption, theme::Muted,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
        volumeMotion.goal = system.volume;
        if (reducedMotion || renderMode || highContrast)
            volumeMotion.Snap(volumeMotion.goal);
        else if (!layoutOnly && volumeMotion.Moving())
            ArmMotion();
        Fill(track, theme::Inset, (track.bottom - track.top) / 2);
        float position = track.left + (track.right - track.left) * std::clamp(volumeMotion.value, 0.0f, 1.0f);
        if (position > track.left)
            Fill({track.left, track.top, position, track.bottom}, theme::Accent, (track.bottom - track.top) / 2);
        if (!layoutOnly) {
            float alpha = draggingVolume || focusedKey == StableKey(Volume) ? 1.0f : HoverAmount(Volume);
            brush->SetColor(Color(theme::Text, highContrast ? 1 : alpha));
            float radius = draggingVolume && !reducedMotion ? 5.5f : 5;
            target->FillEllipse(D2D1::Ellipse({position, (track.top + track.bottom) / 2}, radius, radius), brush.Get());
        }
        AddHit(Volume, volume, L"Volume. " + Number(system.volume * 100) + L" percent", HasAudio());
        FocusRing(Volume, volume);
    }
    PaintGroup reveal(*this, 7);
    MediaCard(false);
}
