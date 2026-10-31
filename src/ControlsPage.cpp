#include "Island.h"

void Island::MediaCard(bool compact) {
    auto l = Layout();
    auto rect = compact ? l.body : layout::ControlsColumns(l)[1];
    if (compact)
        rect.bottom = std::min(rect.bottom, rect.top + layout::Metrics::MediaBodyHeight);
    bool compactLayout = compact || rect.Width() < 300;
    if (!compact)
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
    float artSize = compactLayout ? 40.0f : 48.0f;
    float artLeft = rect.left + (compact ? 0.0f : 16.0f), artTop = rect.top + (compactLayout ? 12.0f : 32.0f);
    D2D1_RECT_F art{artLeft, artTop, artLeft + artSize, artTop + artSize};
    Fill(art, theme::Inset, shape::Chip);
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
    if (compactLayout) {
        D2D1_RECT_F metadata{rect.left + 64, rect.top + 16, rect.right - 8, rect.top + 36};
        Text(currentMedia.title, metadata, type::Label, theme::Text, DWRITE_TEXT_ALIGNMENT_CENTER);
        metadata.top += 20;
        metadata.bottom += 20;
        Text(currentMedia.artist, metadata, type::Caption, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (compact)
            AddHit(Expand, {rect.left, rect.top, rect.right, rect.top + 60}, L"Open full controls");
    } else {
        Text(L"\xE767", {rect.left + 16, rect.top + 4, rect.left + 32, rect.top + 24}, type::InlineIcon, theme::Muted);
        Text(system.audioName, {rect.left + 36, rect.top + 4, rect.right - 16, rect.top + 24}, type::Caption,
             theme::Muted);
        TwoLine({rect.left + 80, rect.top + 32, rect.right - 164, rect.top + 80}, currentMedia.title,
                currentMedia.artist, theme::Text, type::Title);
    }
    AddHit(2000, R(rect),
           currentMedia.title + L". " + currentMedia.artist + L". " + Clock(currentMedia.position) + L" of " +
               Clock(currentMedia.duration),
           false);
    float y = rect.top + (compactLayout ? 88.0f : 32.0f), playSize = compactLayout ? 32.0f : 40.0f;
    float transportCenter = compactLayout ? cx : rect.right - 84;
    unsigned playFill = highContrast ? theme::Accent : dock::Mix(theme::Accent, palette::Tokens::White, .7f);
    unsigned playInk = Contrast(playFill, palette::Tokens::Black) > Contrast(playFill, palette::Tokens::White)
                           ? palette::Tokens::Black
                           : palette::Tokens::White;
    for (int i = 0; i < 3; ++i) {
        float size = i == 1 ? playSize : 32.0f;
        float x = transportCenter + (i - 1) * 44;
        D2D1_RECT_F button{x - size / 2, y + (playSize - size) / 2, x + size / 2, y + (playSize + size) / 2};
        int id = Previous + i;
        bool enabled = i == 0 ? currentMedia.canPrevious : i == 1 ? currentMedia.canPlay : currentMedia.canNext;
        AddHit(id, button, i == 0 ? L"Previous track" : i == 1 ? L"Play or pause" : L"Next track", enabled);
        if (!compact)
            Fill(Tactile(id, button),
                 i == 1 && enabled ? playFill : dock::Mix(theme::CardHover, theme::Text, HoverAmount(id) * .1f),
                 size / 2);
        Text(i == 0                 ? L"\xE892"
             : i == 2               ? L"\xE893"
             : currentMedia.playing ? L"\xE769"
                                    : L"\xE768",
             button, type::InlineIcon,
             i == 1 && enabled && !compact ? playInk
             : enabled                     ? theme::Text
                                           : theme::Muted,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        FocusRing(id, button, size / 2);
    }
    if (options.Bool(L"MediaScrubber")) {
        float trackY = compactLayout ? rect.top + 70 : rect.bottom - 16;
        float trackPad = rect.Width() < 160 ? 8.0f : 52.0f;
        float progress = currentMedia.duration > 0
                             ? static_cast<float>(std::clamp(currentMedia.position / currentMedia.duration, 0.0, 1.0))
                             : 0;
        Line(rect.left + trackPad, trackY, rect.right - trackPad, trackY, theme::Inset, 2);
        float position = rect.left + trackPad + (rect.Width() - 2 * trackPad) * progress;
        Line(rect.left + trackPad, trackY, position, trackY, theme::Accent, 2);
        if (!layoutOnly) {
            brush->SetColor(Color(theme::Text));
            target->FillEllipse(D2D1::Ellipse({position, trackY}, 3, 3), brush.Get());
        }
        Text(Clock(currentMedia.position), {rect.left + 8, trackY - 12, rect.left + 48, trackY + 12}, type::Caption,
             theme::Muted);
        Text(Clock(currentMedia.duration), {rect.right - 48, trackY - 12, rect.right - 8, trackY + 12}, type::Caption,
             theme::Muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        AddHit(2970, {rect.left + trackPad, trackY - 16, rect.right - trackPad, trackY + 16},
               L"Media playback position", currentMedia.duration > 0);
    }
}

D2D1_RECT_F Island::VolumeRect() const {
    auto left = layout::ControlsColumns(Layout())[0];
    return {left.left, left.bottom - 32, left.right, left.bottom};
}
D2D1_RECT_F Island::VolumeTrack() const {
    auto r = VolumeRect();
    return {r.left, r.top, r.right, r.bottom};
}
bool Island::HasAudio() const { return fixtureGeometry ? fixtureAudioAvailable : system.audio != nullptr; }

void Island::Controls() {
    auto l = Layout();
    auto left = layout::ControlsColumns(l)[0];
    auto grid = left;
    grid.bottom -= 44;
    auto rows = l.narrow ? layout::Rows(grid, {1, 1, 1}) : layout::Rows(grid, {1, 1});
    const int ids[]{Wifi, Bluetooth, Audio, Night, Dark};
    const wchar_t *names[]{L"Wi-Fi", L"Bluetooth", L"Audio", L"Night Light", L"Dark mode", L"Brightness"};
    const wchar_t *icons[]{L"\xE701", L"\xE702", L"\xE767", L"\xE708", L"\xE7A1", L"\xE706"};
    std::wstring details[]{system.wifi,
                           system.bluetooth,
                           system.audioName,
                           L"Windows Settings",
                           controlState.dark ? L"On" : L"Off",
                           Number(controlState.brightness) + L"%"};
    int columns = l.narrow ? 2 : 3;
    for (int i = 0; i < 5; ++i) {
        PaintGroup reveal(*this, i);
        auto cells = l.narrow ? layout::Columns(rows[i / columns], {1, 1})
                     : i < 3  ? layout::Columns(rows[0], {1, 1, 1})
                              : layout::Columns(rows[1], {1, 1});
        bool active = i == 0   ? system.wifiConnected
                      : i == 1 ? std::any_of(connections.devices.begin(), connections.devices.end(),
                                             [](const auto &device) { return device.connected; })
                      : i == 2 ? HasAudio()
                      : i == 4 ? controlState.dark
                               : false;
        size_t slot = l.narrow ? i % columns : i < 3 ? i : i - 3;
        Button(ids[i], R(cells[slot]), names[i], details[i], icons[i], active, i != 2 || HasAudio());
    }
    auto volume = VolumeRect(), track = VolumeTrack();
    {
        PaintGroup reveal(*this, 6);
        volumeMotion.goal = system.volume;
        if (reducedMotion || renderMode || highContrast)
            volumeMotion.Snap(volumeMotion.goal);
        else if (!layoutOnly && volumeMotion.Moving())
            ArmMotion();
        Fill(track, theme::Card, (track.bottom - track.top) / 2);
        float position = track.left + (track.right - track.left) * std::clamp(volumeMotion.value, 0.0f, 1.0f);
        if (position > track.left)
            Fill({track.left, track.top, position, track.bottom}, theme::Accent, (track.bottom - track.top) / 2);
        Text(L"\xE767", {volume.left + 12, volume.top, volume.left + 36, volume.bottom}, type::InlineIcon,
             system.volume > .08f ? theme::AccentInk : theme::Muted);
        Text(Number(system.volume * 100) + L"%", {volume.right - 64, volume.top, volume.right - 12, volume.bottom},
             type::Caption, theme::Text, DWRITE_TEXT_ALIGNMENT_TRAILING);
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
