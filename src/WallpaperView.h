#pragma once
// Included by Island.cpp after the Island declaration.
size_t Island::WallpaperSlots() const {
    auto b = Body();
    int columns = b.right - b.left >= 600 ? 3 : b.right - b.left >= 380 ? 2 : 1;
    int rows = std::max(1, static_cast<int>((b.bottom - b.top - 88) / 112));
    return static_cast<size_t>(columns * rows);
}
void Island::WallpaperSource(int source) {
    wallpaperSource = source;
    selectedWallpaper = 0;
    wallpaperFocus.Snap(0);
    scroll = 0;
    if (renderMode || fixtureGeometry)
        return;
    if (!catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    if (source == 1 && wallpaperFolder.empty()) {
        ChooseWallpaperFolder();
        return;
    }
    wallpapersRequested = true;
    wallpaperLoading = true;
    if (source == 2)
        catalog->DiscoverCurrentWallpapers();
    else
        catalog->DiscoverWallpapers(source == 1 ? wallpaperFolder : L"");
}
void Island::WallpaperAction(int id) {
    if (id == WallpaperCollection) {
        WallpaperSource(0);
        return;
    }
    if (id == WallpaperCurrent) {
        WallpaperSource(2);
        return;
    }
    if (id == WallpaperPrevious) {
        size_t slots = WallpaperSlots();
        if (selectedWallpaper >= slots)
            selectedWallpaper = (selectedWallpaper / slots - 1) * slots;
        return;
    }
    if (id == WallpaperNext) {
        size_t next = (selectedWallpaper / WallpaperSlots() + 1) * WallpaperSlots();
        if (next < wallpapers.size())
            selectedWallpaper = next;
        return;
    }
    if (id == WallpaperTarget) {
        auto found = std::find_if(wallpaperDisplays.begin(), wallpaperDisplays.end(),
                                  [&](const auto &item) { return item.id == wallpaperTarget; });
        if (found == wallpaperDisplays.end())
            wallpaperTarget = wallpaperDisplays.empty() ? L"" : wallpaperDisplays.front().id;
        else if (++found == wallpaperDisplays.end())
            wallpaperTarget.clear();
        else
            wallpaperTarget = found->id;
        wallpaperTargetChosen = true;
        return;
    }
    if (id == WallpaperFit) {
        wallpaperFit = static_cast<DESKTOP_WALLPAPER_POSITION>((static_cast<int>(wallpaperFit) + 1) % 6);
        wallpaperFitChosen = true;
        return;
    }
    if (id != WallpaperApply || selectedWallpaper >= wallpapers.size() || wallpaperApplying)
        return;
    if (!controls::MayWrite(renderMode, fixtureGeometry, previewMode)) {
        note = L"Preview: wallpaper and Windows fit remain unchanged.";
        return;
    }
    if (!wallpaperTarget.empty() && std::none_of(wallpaperDisplays.begin(), wallpaperDisplays.end(),
                                                 [&](const auto &item) { return item.id == wallpaperTarget; })) {
        note = L"The selected display is no longer connected. Choose another display.";
        return;
    }
    if(!PrepareSystemChange())return;
    wallpaperApplying = true;
    pendingWallpaper = wallpapers[selectedWallpaper].path;
    if (!catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    catalog->ApplyWallpaperAsync(pendingWallpaper, wallpaperTarget, wallpaperFit);
}
void Island::Wallpaper() {
    auto b = Body();
    auto area = layout::Rect{b.left, b.top, b.right, b.top + 32};
    const int filters[]{WallpaperCollection, WallpaperFolder, WallpaperCurrent};
    const wchar_t *names[]{L"Collection", L"My folder…", L"Current"};
    for (int i = 0; i < 3; ++i)
        TextButton(filters[i], R(layout::Column(area, i, 3)), names[i], wallpaperSource == i);
    std::wstring targetLabel = wallpaperTarget.empty() ? L"All displays" : L"Disconnected display";
    for (const auto &display : wallpaperDisplays)
        if (display.id == wallpaperTarget)
            targetLabel = display.label;
    constexpr const wchar_t *fits[]{L"Center", L"Tile", L"Stretch", L"Fit", L"Fill", L"Span"};
    int fit = std::clamp(static_cast<int>(wallpaperFit), 0, 5);
    auto selectors = layout::Rect{b.left, b.top + 40, b.right, b.top + 72};
    TextButton(WallpaperTarget, R(layout::Column(selectors, 0, 2)), L"Apply to: " + targetLabel);
    TextButton(WallpaperFit, R(layout::Column(selectors, 1, 2)), std::wstring(L"Fit (all): ") + fits[fit]);
    if (thumbnails.size() != wallpapers.size())
        thumbnails.resize(wallpapers.size());
    size_t slots = WallpaperSlots(), first = selectedWallpaper / slots * slots;
    int columns = b.right - b.left >= 600 ? 3 : b.right - b.left >= 380 ? 2 : 1;
    float cardHeight = std::min(100.0f, std::max(36.0f, b.bottom - b.top - 4));
    for (size_t i = 0; i < thumbnails.size(); ++i)
        if (i < first || i >= first + slots)
            thumbnails[i].Reset();
    for (size_t slot = 0; slot < slots && first + slot < wallpapers.size(); ++slot) {
        size_t index = first + slot;
        const auto &entry = wallpapers[index];
        auto cell = layout::Column({b.left, b.top + 88, b.right, b.top + 88 + cardHeight},
                                   static_cast<int>(slot % columns), columns);
        cell.top += static_cast<float>(slot / columns) * 112;
        cell.bottom = cell.top + cardHeight;
        auto rect = R(cell);
        int id = 500 + static_cast<int>(index);
        AddHit(id, rect,
               entry.title + L", " + std::to_wstring(index + 1) + L" of " + std::to_wstring(wallpapers.size()) +
                   (index == selectedWallpaper ? L", selected" : L"") +
                   (entry.path == appliedWallpaper ? L", last applied" : L""));
        Fill(rect, index == selectedWallpaper ? theme::AccentSoft : theme::Card, 12);
        if (!layoutOnly) {
            auto pixels = entry.pixels;
            UINT iw = entry.width, ih = entry.height;
            if (!thumbnails[index] && pixels && !pixels->empty())
                target->CreateBitmap(D2D1::SizeU(iw, ih), pixels->data(), iw * 4,
                                     D2D1::BitmapProperties(
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                     &thumbnails[index]);
            auto image = thumbnails[index];
            if (image && cardHeight > 52) {
                auto size = image->GetSize();
                D2D1_RECT_F picture{rect.left + 6, rect.top + 6, rect.right - 6, rect.bottom - 26};
                float ratio =
                    std::max((picture.right - picture.left) / size.width, (picture.bottom - picture.top) / size.height);
                float sw = (picture.right - picture.left) / ratio, sh = (picture.bottom - picture.top) / ratio;
                target->DrawBitmap(image.Get(), picture, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                   D2D1::RectF((size.width - sw) / 2, (size.height - sh) / 2, (size.width + sw) / 2,
                                               (size.height + sh) / 2));
            }
        }
        Text(entry.title, {rect.left + 8, cardHeight > 52 ? rect.bottom - 26 : rect.top, rect.right - 8, rect.bottom},
             type::Caption, highContrast && index == selectedWallpaper ? theme::SelectedText : theme::Text);
        FocusRing(id, rect, 12);
    }
    if (wallpapers.empty())
        Paragraph(wallpaperLoading ? L"Loading wallpapers…" : L"No images. Choose another source or folder.",
                  {b.left + 12, b.top + 92, b.right - 12, b.top + 160});
}
