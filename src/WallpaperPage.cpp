#include "Island.h"

void Island::ReplaceWallpapers(std::vector<WallpaperEntry> next) {
    std::wstring selected = selectedWallpaper < wallpapers.size() ? wallpapers[selectedWallpaper].path : L"";
    wallpapers = std::move(next);
    selectedWallpaper = 0;
    for (size_t i = 0; i < wallpapers.size(); ++i)
        if (wallpapers[i].path == selected)
            selectedWallpaper = i;
    wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));
    thumbnails.clear();
    imageBrushes.clear();
}

void Island::Photo(ID2D1Bitmap *bitmap, D2D1_RECT_F rect, float opacity, float parallax) {
    if (layoutOnly || !bitmap)
        return;
    auto &picture = imageBrushes[bitmap];
    if (!picture) {
        ComPtr<ID2D1DeviceContext> context;
        if (SUCCEEDED(target.As(&context))) {
            ComPtr<ID2D1BitmapBrush1> modern;
            context->CreateBitmapBrush(bitmap,
                                       D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                                                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC),
                                       &modern);
            picture = modern;
        } else
            target->CreateBitmapBrush(bitmap,
                                      D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                                                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
                                      &picture);
    }
    if (!picture)
        return;
    auto size = bitmap->GetSize();
    float ratio = std::max((rect.right - rect.left + 8) / size.width, (rect.bottom - rect.top + 8) / size.height);
    picture->SetTransform(D2D1::Matrix3x2F::Scale(ratio, ratio) *
                          D2D1::Matrix3x2F::Translation((rect.left + rect.right - size.width * ratio) / 2 + parallax,
                                                        (rect.top + rect.bottom - size.height * ratio) / 2));
    picture->SetOpacity(opacity);
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, layout::CardRadius, layout::CardRadius), picture.Get());
}

void Island::ChooseWallpaperFolder() {
    ModalGuard modal(modalOpen);
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return;
    DWORD folderFlags = 0;
    dialog->GetOptions(&folderFlags);
    dialog->SetOptions(folderFlags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose a wallpaper folder");
    if (FAILED(dialog->Show(window)))
        return;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)))
        return;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return;
    if (!catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    wallpaperSource = 1;
    wallpaperFolder = path;
    selectedWallpaper = 0;
    wallpaperFocus.Snap(0);
    wallpapersRequested = true;
    wallpaperLoading = true;
    catalog->DiscoverWallpapers(path);
    CoTaskMemFree(path);
    note = L"Loading pictures from your folder…";
}

size_t Island::WallpaperSlots() const { return 1; }
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
    if (!PrepareSystemChange())
        return;
    wallpaperApplying = true;
    pendingWallpaper = wallpapers[selectedWallpaper].path;
    if (!catalog)
        catalog = std::make_unique<DesktopCatalog>(window);
    catalog->ApplyWallpaperAsync(pendingWallpaper, wallpaperTarget, wallpaperFit);
}
void Island::Wallpaper() {
    auto b = Body();
    float segment = std::min(104.0f, (b.right - b.left - 12) / 2);
    TextButton(WallpaperCollection, {b.left, b.top, b.left + segment, b.top + 32}, L"Recent", wallpaperSource == 0);
    TextButton(WallpaperFolder, {b.left + segment + 4, b.top, b.left + segment * 2 + 4, b.top + 32}, L"My folder",
               wallpaperSource == 1);
    if (b.right - b.left >= 400) {
        Text(std::to_wstring(wallpapers.size()) + L" pictures", {b.left + 228, b.top, b.right - 88, b.top + 32},
             type::Caption, theme::Muted);
        TextButton(WallpaperCurrent, {b.right - 80, b.top, b.right, b.top + 32}, L"Current");
    }
    if (thumbnails.size() != wallpapers.size())
        thumbnails.resize(wallpapers.size());
    if (renderMode || reducedMotion || highContrast)
        wallpaperFocus.Snap(static_cast<float>(selectedWallpaper));
    else if (wallpaperFocus.goal != static_cast<float>(selectedWallpaper)) {
        wallpaperFocus.goal = static_cast<float>(selectedWallpaper);
        ArmMotion();
    }
    auto stage = Layout().stage;
    float focus = wallpaperFocus.value;
    std::array<int, 7> indices{};
    int count = 0, middle = static_cast<int>(std::lround(focus));
    for (int index = middle - 3; index <= middle + 3; ++index)
        if (index >= 0 && index < static_cast<int>(wallpapers.size()))
            indices[count++] = index;
    std::sort(indices.begin(), indices.begin() + count,
              [&](int left, int right) { return std::abs(left - focus) > std::abs(right - focus); });
    PaintGroup reveal(*this, 0);
    for (int slot = 0; slot < count; ++slot) {
        int index = indices[slot];
        float delta = index - focus;
        auto pose = wallpaper::Pose(delta, stage.Width());
        if (pose.opacity <= 0)
            continue;
        float w = stage.Width() * pose.scale, h = stage.Height() * pose.scale;
        float cx = stage.CenterX() + pose.x, cy = (stage.top + stage.bottom) / 2 + std::abs(delta) * 4;
        D2D1_RECT_F rect{cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2};
        AddHit(500 + index, rect, wallpapers[index].title);
        hits.back().skew = std::tan(pose.skew);
        hits.back().radius = layout::CardRadius;
        D2D1::Matrix3x2F saved;
        if (!layoutOnly) {
            target->GetTransform(&saved);
            auto layer = D2D1::LayerParameters();
            layer.opacity = highContrast ? 1.0f : pose.opacity;
            target->PushLayer(layer, nullptr);
            target->SetTransform(D2D1::Matrix3x2F::Skew(pose.skew * 57.2957795f, 0, {cx, cy}) * saved);
        }
        Card(rect, 2);
        const auto &entry = wallpapers[index];
        if (!layoutOnly && entry.pixels && !entry.pixels->empty()) {
            if (!thumbnails[index])
                target->CreateBitmap(D2D1::SizeU(entry.width, entry.height), entry.pixels->data(), entry.width * 4,
                                     D2D1::BitmapProperties(
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                     &thumbnails[index]);
            Photo(thumbnails[index].Get(), rect, 1, reducedMotion || highContrast ? 0 : -wallpaperParallax.value * 4);
        } else if (wallpaperLoading)
            Shimmer(rect);
        if (!layoutOnly && index == static_cast<int>(selectedWallpaper) && std::abs(delta) < .25f) {
            brush->SetColor(Color(theme::Accent, highContrast ? 1.0f : 1 - std::abs(delta) * 4));
            D2D1_RECT_F ring{rect.left + 3, rect.top + 3, rect.right - 3, rect.bottom - 3};
            target->DrawRoundedRectangle(D2D1::RoundedRect(ring, layout::CardRadius - 3, layout::CardRadius - 3),
                                         brush.Get(), 2);
        }
        FocusRing(500 + index, rect, layout::CardRadius);
        if (!layoutOnly) {
            target->SetTransform(saved);
            target->PopLayer();
        }
    }
    float center = stage.CenterX();
    if (wallpapers.empty()) {
        Card(R(stage));
        Shimmer(R(stage));
        float y = stage.top + stage.Height() / 2;
        Line(center - 24, y + 8, center - 8, y - 12, theme::Muted, 1.5f);
        Line(center - 8, y - 12, center + 12, y + 8, theme::Muted, 1.5f);
        Line(center + 12, y + 8, center + 24, y - 4, theme::Muted, 1.5f);
        Text(wallpaperLoading ? L"Loading pictures…" : L"Choose a folder to add pictures.",
             {b.left + 12, y + 20, b.right - 12, y + 52}, type::Body, theme::Muted, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (!layoutOnly) {
        size_t dots = std::min(size_t{9}, wallpapers.size());
        size_t first = selectedWallpaper > dots / 2 ? selectedWallpaper - dots / 2 : 0;
        first = std::min(first, wallpapers.size() - dots);
        for (size_t i = 0; i < dots; ++i) {
            brush->SetColor(Color(first + i == selectedWallpaper ? theme::Text : theme::Muted));
            float x = center + (static_cast<float>(i) - static_cast<float>(dots - 1) / 2) * 12;
            target->FillEllipse(D2D1::Ellipse({x, b.bottom - 6}, 3, 3), brush.Get());
        }
    }
}
