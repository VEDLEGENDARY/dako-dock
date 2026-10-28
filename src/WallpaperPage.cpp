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
    TextButton(WallpaperCollection, {b.left, b.top, b.left + 88, b.top + 28}, L"Recent", wallpaperSource == 0);
    TextButton(WallpaperFolder, {b.left + 96, b.top, b.left + 204, b.top + 28}, L"My folder", wallpaperSource == 1);
    Text(std::to_wstring(wallpapers.size()) + L" pictures", {b.left + 216, b.top, b.right - 80, b.top + 28},
         type::Caption, theme::Muted);
    TextButton(WallpaperCurrent, {b.right - 72, b.top, b.right, b.top + 28}, L"Current");
    if (thumbnails.size() != wallpapers.size())
        thumbnails.resize(wallpapers.size());
    float available = b.right - b.left, center = (b.left + b.right) / 2, top = b.top + 48;
    float mainWidth = std::min(300.0f, available * .46f),
          pictureHeight = std::min(188.0f, std::max(48.0f, b.bottom - top - 18));
    for (int distance = 2; distance >= 0; --distance)
        for (int side = -1; side <= 1; side += 2) {
            if (!distance && side == 1)
                continue;
            int index = static_cast<int>(selectedWallpaper) + distance * side;
            if (index < 0 || index >= static_cast<int>(wallpapers.size()))
                continue;
            float cardWidth = distance == 0 ? mainWidth : 52,
                  offset = distance == 0 ? 0 : side * (mainWidth / 2 + 40 + (distance - 1) * 66);
            D2D1_RECT_F rect{center + offset - cardWidth / 2, top + (distance ? 8 : 0), center + offset + cardWidth / 2,
                             top + pictureHeight - (distance ? 8 : 0)};
            AddHit(500 + index, rect, wallpapers[index].title);
            hits.back().skew = -0.15838444f;
            Fill(rect, theme::CardHover, 14);
            if (!layoutOnly && wallpapers[index].pixels && !wallpapers[index].pixels->empty()) {
                auto &entry = wallpapers[index];
                if (!thumbnails[index])
                    target->CreateBitmap(D2D1::SizeU(entry.width, entry.height), entry.pixels->data(), entry.width * 4,
                                         D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                                  D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                         &thumbnails[index]);
                if (thumbnails[index]) {
                    D2D1::Matrix3x2F transform;
                    target->GetTransform(&transform);
                    target->SetTransform(
                        D2D1::Matrix3x2F::Skew(-9, 0, {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}) *
                        transform);
                    auto size = thumbnails[index]->GetSize();
                    float ratio =
                              std::max((rect.right - rect.left) / size.width, (rect.bottom - rect.top) / size.height),
                          sw = (rect.right - rect.left) / ratio, sh = (rect.bottom - rect.top) / ratio;
                    target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    target->DrawBitmap(
                        thumbnails[index].Get(), rect, distance ? 0.6f : 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                        {(size.width - sw) / 2, (size.height - sh) / 2, (size.width + sw) / 2, (size.height + sh) / 2});
                    target->PopAxisAlignedClip();
                    target->SetTransform(transform);
                }
            }
            FocusRing(500 + index, rect, 14);
        }
    if (wallpapers.empty())
        Paragraph(wallpaperLoading ? L"Loading pictures…" : L"Choose a folder to add pictures.",
                  {b.left + 16, top, b.right - 16, top + 80});
}
