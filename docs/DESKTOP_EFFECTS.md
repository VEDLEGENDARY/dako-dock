# Reference shapes and desktop effects

The October 23 screenshots supersede the rail design. Normal panels attach to the screen edge with flared shoulders, a near-black gradient and rounded lower corners. Only the browser context detaches the popup. The context menu offers a compact media player. The wallpaper carousel uses rounded, skewed cards and a larger center selection. Apps has persistent starred/recent groups; Stats uses CPU/GPU/RAM rings and live process memory.

## Browser geometry

No horizontal strip is reserved. Work areas extend to the monitor edges. Dako subtracts its 216 × 18 DIP notch from the selected maximized browser's window region, retaining the prior region in the recovery helper's shared snapshot. It restores that region when the browser is no longer selected/maximized or Dako exits/crashes. Manually resized windows retain native behavior. Protected/elevated targets can reject changes; unresponsive targets are skipped.

The cutout controls painting, not browser layout: tabs do not flow around it. Popups detach at 112 DIP for recognized browsers, with context held during interaction. Detection uses executable names and the Firefox window class, without reading web content. Custom toolbar/vertical-tab arrangements remain review cases.

## Wallpaper and accent

Set wallpaper runs through IDesktopWallpaper on the catalog worker. One temporary old desktop snapshot is uploaded to a native layered window. Its region excludes application windows, so apps remain live, then an expanding circle reveals the new desktop. The 680 ms effect releases its window, surface and timer at completion. Focus changes or mouse interaction cancel it; a watchdog also removes stalled overlays. Capture/animation is skipped when no wallpaper is visible or reduced motion is enabled. Captures remain in memory and are never saved or transmitted.

The color worker decodes thumbnails no larger than 320 × 200. Weighted hue bins prefer chromatic colors; neutral pictures yield neutral accents. Lightness is constrained for dark UI contrast. Tiles, sliders, tabs, selection backgrounds, focus rings, buttons and chart accents follow the palette. External wallpaper/file changes are checked every five seconds and after wallpaper jobs; unchanged images are not decoded again.

## Verification

- Release MSVC `/W4 /WX` build.
- `scripts/test-motion.ps1`: spring/hover/dismissal, browser classification, hue extraction and circular-radius checks.
- `scripts/test-shell.ps1`: full work areas, notch-only browser exclusion, original-region restoration, maximize/manual resize, normal shutdown and recovery of a surviving foreign-process test window after abrupt exit.
- `scripts/verify.ps1`: 24 renders, including every attached tab, browser detachment, compact media and alternate accent palettes; native region, hit-test, Escape, accessibility and circular geometry checks. Sample app/device/stat fixtures do not change real connections or wallpapers.
- `Dako.exe --effects-test`: actual layered upload and timed circular cleanup without changing the wallpaper.
- `Dako.exe --runtime-check`: read-only palette and hardware worker check, writing `build/runtime-check.txt`.

These layouts implement the reference without claiming pixel identity at every DPI. The Today information card uses real power status while weather/calendar integration is pending. Richer media artwork remains pending. GPU counters depend on Windows/driver availability; process bars show individual working sets.
