# Wallpaper collection

Dako ships 18 curated Pexels photos at 3,840 pixels wide, preserving each photo's aspect ratio. The default Collection shows these first, followed by Your wallpaper. Its original file path is saved as SourcePath in %LOCALAPPDATA%\Dako\Wallpapers\original-wallpaper.ini and used directly while available. A validated local copy remains as a fallback if that original file is moved or deleted. Personal images stay out of the project.

The native app reads local images only. My folder browses personal images; Collection returns to these photos and the preserved personal background. Click Set wallpaper to apply the selected photo.

Applying a wallpaper validates the file before calling Windows' IDesktopWallpaper API. Invalid or missing files leave the desktop unchanged. For recovery, Dako.exe --restore-original-wallpaper restores the saved original and verifies Windows' reported path before exiting.

The build copies assets/wallpapers beside Dako.exe. Keep that asset directory with the executable. To restore missing bundled photos, run scripts/fetch-wallpapers.ps1, then rebuild. The source catalog is [assets/wallpapers/catalog.json](../../assets/wallpapers/catalog.json).

| # | Theme | Photo source |
| --- | --- | --- |
| 01 | Minimalist | [Golden ridges cascade into distance](https://www.pexels.com/photo/the-desert-is-covered-in-sand-dunes-at-sunset-16124508/) |
| 02 | Minimalist | [Solitary tree emerges through mist](https://www.pexels.com/photo/tree-on-the-lake-6630320/) |
| 03 | Nature | [Alpine peaks frame emerald reflections](https://www.pexels.com/photo/scenic-view-of-lake-and-mountains-in-dolomites-33244766/) |
| 04 | Nature | [Winding streams lead toward mountains](https://www.pexels.com/photo/mountain-landscape-at-dawn-5645470/) |
| 05 | Dark-mode | [Storm clouds conceal shadowed peaks](https://www.pexels.com/photo/mountains-under-dark-clouds-in-evening-5592630/) |
| 06 | Galaxy/Nebula | [Blue nebula opens between clouds](https://www.pexels.com/photo/breathtaking-space-galaxy-10054239/) |
| 07 | Galaxy/Nebula | [Jagged peaks frame cosmic glow](https://www.pexels.com/photo/milky-way-galaxy-in-the-night-sky-over-the-mountains-25854065/) |
| 08 | City/Cyberpunk | [Rainy intersection reflects electric blues](https://www.pexels.com/photo/a-rainy-evening-with-moving-cars-on-the-road-14556059/) |
| 09 | Abstract/Gradient | [Glass ribbons float through gradients](https://www.pexels.com/photo/glass-in-front-of-gradient-wallpaper-11382581/) |
| 10 | Abstract/Gradient | [Sculpted curves catch electric blue](https://www.pexels.com/photo/abstract-blue-and-white-waves-background-subtle-gradients-flow-liquid-lines-cinema-4d-design-element-13791908/) |
| 11 | Aurora | [Emerald curtains crown jagged mountains](https://www.pexels.com/photo/stunning-aurora-borealis-over-mountain-landscape-36012507/) |
| 12 | Ocean | [Sea stacks frame blushing horizon](https://www.pexels.com/photo/cliffs-in-sea-near-coast-at-sunset-5137664/) |
| 13 | Minimalist | [Bare branches frame alpine stillness](https://www.pexels.com/photo/scenic-landscape-with-a-tree-amid-lake-17824116/) |
| 14 | Twilight/Forest | [Fog drifts beneath shadowed trees](https://www.pexels.com/photo/fog-clouding-view-of-forest-at-night-25872380/) |
| 15 | Ocean | [Sunlit cliffs descend toward ocean](https://www.pexels.com/photo/aerial-photography-of-mountain-and-ocean-view-1671326/) |
| 16 | Aurora | [Purple curtains sweep open skies](https://www.pexels.com/photo/mesmerizing-aurora-borealis-in-night-sky-34983674/) |
| 17 | City/Cyberpunk | [Neon towers overlook golden waterfront](https://www.pexels.com/photo/chongqing-city-neon-skyline-at-night-32660204/) |
| 18 | Dark-mode | [Starlight traces a shadowed summit](https://www.pexels.com/photo/starry-night-sky-over-majestic-mountain-silhouette-37454912/) |
