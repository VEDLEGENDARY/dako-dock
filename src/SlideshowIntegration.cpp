#include "Island.h"

void Island::ConfigureSlideshow() {
    if (renderMode || fixtureGeometry || previewMode)
        return;
    auto folder = options.Text(L"WallpaperFolder");
    if (!slideshow.Configure(folder, static_cast<unsigned>(options.Int(L"Slideshow"))))
        return;
    slideshowDeadline.reset();
    slideshowCatalog.reset();
    slideshowPictures.clear();
    slideshowIndex = 0;
    if (!slideshow.Enabled())
        return;
    slideshowDeadline = std::make_unique<OneShotDeadline>(window, SlideshowDue);
    slideshowCatalog =
        std::make_unique<DesktopCatalog>(window, SlideshowCatalogChanged, SlideshowApplied, slideshow.Generation());
    slideshowCatalog->DiscoverWallpapers(folder);
}

void Island::SlideshowReady(LPARAM generation) {
    if (!slideshowCatalog || static_cast<unsigned>(generation) != slideshow.Generation())
        return;
    std::vector<AppEntry> unused;
    std::wstring error;
    slideshowCatalog->Snapshot(unused, slideshowPictures, error, 2);
    if (!error.empty() || slideshowPictures.size() < 2) {
        note = error.empty() ? L"Slideshow needs at least two pictures in the configured folder." : error;
        return;
    }
    if (slideshowDeadline->Arm(slideshow.Delay(), slideshow.Generation()))
        slideshow.Arm();
    else
        note = L"Slideshow could not schedule its next change.";
}

void Island::RunSlideshow(WPARAM generation) {
    if (!slideshow.Due(static_cast<unsigned>(generation)))
        return;
    if (renderMode || fixtureGeometry || previewMode || !slideshowCatalog || slideshowPictures.empty() ||
        !PrepareSystemChange()) {
        slideshow.Complete(static_cast<unsigned>(generation), false);
        return;
    }
    // Browsing Recent/My folder changes neither this playlist nor the user's selection.
    auto &picture = slideshowPictures[slideshowIndex % slideshowPictures.size()];
    slideshowCatalog->ApplyWallpaperAsync(picture.path, wallpaperTarget, wallpaperFit);
}

void Island::SlideshowFinished(WPARAM result, LPARAM detail) {
    unsigned generation = static_cast<unsigned>(result >> 1);
    bool applied = (result & 1) != 0;
    if (!slideshow.Complete(generation, applied)) {
        if (generation == slideshow.Generation() && !applied)
            note = L"Slideshow stopped after a wallpaper change failed (" + std::to_wstring(detail) + L").";
        return;
    }
    ++slideshowIndex;
    // A successful bounded apply earns one next deadline; there is no periodic timer or retry loop.
    if (slideshowDeadline && slideshowDeadline->Arm(slideshow.Delay(), generation))
        slideshow.Arm();
}
