# Performance

## Design constraints

- One native UI process and a small native recovery helper that blocks on process/event handles without polling; no browser engine or managed UI runtime.
- Block in the Win32 message loop when idle.
- Reuse drawing resources while open, release the Direct2D surface after withdrawal, and paint the resting handle with GDI.
- Invalidate on interaction or native state notification, not on every frame.
- A 16 ms timer exists only while a spring, hover highlight, pane cross-fade or carousel is moving; settled animations stop.
- Pointer proximity uses a coalescable 100 ms timer. There is no global mouse hook.
- No top rail is reserved. Browser classification is cached on foreground-window changes and held during interaction; no continuous accessibility-tree or browser-content scanning is used. A notch-only native region is updated when the selected maximized browser's geometry changes.
- Resource sampling runs only while Stats is expanded (2 s).
- The media progress display repaints once per second during active playback only.
- Media-session initialization is lazy, and metadata/timeline refreshes pause while hidden or outside Controls.
- Visible timers are updated only when their enabled state changes; media notifications do not repeatedly reset the focus/countdown timers.
- Media session work happens on an MTA worker, not the window thread.
- No continuous wallpaper animation, microphone listener, network service, or clipboard history collection.
- Small window bounds; do not create an invisible full-screen input surface.
- Respect the Windows animation preference.
- Discover apps and decode wallpaper thumbnails lazily on an MTA worker.
- Derive accents on the catalog worker, checking file changes every five seconds and decoding only changed images. Hardware/process sampling starts on a separate worker only when Stats is opened and runs on requests while visible.
- Circular wallpaper transitions upload one temporary desktop surface and change its region for 680 ms. App windows are excluded; focus/mouse interaction cancels the transition. The overlay is omitted if no desktop is exposed and all overlay resources are released afterward.
- Bundle 18 curated wallpapers locally and append the current Windows wallpaper when available; perform no runtime network requests. Bound folder discovery to 64 images and 2,048 visited entries, with a maximum retained thumbnail size of 320 × 200; share decoded buffers between the worker and renderer.
- Prevent duplicate interactive instances with a per-session named mutex.

## Initial goals

These are budgets to validate, not achieved results: below 50 MiB private bytes at idle; near-zero sustained idle CPU; no allocations or redraws per frame while static. Working set, private bytes, GPU allocations, startup peaks, and CPU during transitions are separate measurements.

## Measurement

```powershell
.\scripts\measure.ps1 -Seconds 10
```

The script measures aggregate Dako working set, private memory, and CPU delta using Get-Process, including the recovery helper. Summed working sets may count shared pages twice. CPU percent is normalized to the logical processor count. It does not include GPU cost or DWM attribution. Compare compact idle and expanded idle separately. Use Windows Performance Recorder/Analyzer for frame timing and GPU/power work before describing the app as fully optimized. Earlier measurements below predate the recovery helper and bottom-dock revision.

`scripts\verify.ps1` uses a hidden native window and a WIC-backed software Direct2D target to generate twelve UI captures in `docs\renders`. It also checks expansion/collapse, browser-chrome input-region exclusion, hit testing, Escape handling, and MSAA metadata. These captures verify scene rendering, not live GPU presentation, animation smoothness, or real hardware actions.

## Measurements collected before testing was deferred

An early native Controls build measured 44.73 MiB private memory and 80.06 MiB working set. Its first 10.02-second sample consumed 296.88 ms of CPU (0.1347% normalized across logical processors). This sample included startup/settling and is not a steady-idle result. It predates the catalog, task persistence, and single-instance changes. GPU memory, GPU time, frame timing, and power have not been measured.

Both early preview processes were closed when the user deferred testing. The user has since reauthorized testing; revised-build render/state checks passed and an interactive preview was opened. The newer measurements below distinguish the cold resting state from the footprint after using larger views.

## Revised dock: first resting sample

Before first reveal or wireless use, a 10.02-second sample of the revised dock measured **15.79 MiB working set**, **2.53 MiB private memory**, and **15.62 ms CPU** (**0.0071%**, normalized across logical processors). This is a cold resting baseline, not the memory footprint after loading media, app catalogs, photos, or wireless support. It excludes GPU/DWM allocations and is not a universal hardware guarantee.

The revised checks cover 12 native renders, the 148 × 4 DIP resting handle, exclusion of the browser-chrome gap from the native input region, hit testing, Escape, accessibility metadata, and spring/hover/dismissal behavior. The wireless view captures use fixtures; actual connection and pairing behavior requires hardware testing.

## Revised dock: after media and wireless use

The running `--inspect` preview measured **132.38 MiB working set**, **97.25 MiB private memory**, and **15.62 ms CPU** over **10.03 seconds** (**0.0071% normalized CPU**), with 462 handles and 15 threads. Media and wireless support had been initialized. This is an observation during interactive review, not a controlled steady-idle benchmark. GPU/DWM memory and presentation costs are excluded. The post-use private footprint exceeds the initial 50 MiB goal and needs profiling; the cold resting figure must not be presented as the footprint after feature use.

## Bottom dock and recovery helper

After launch during interactive review, a complete two-process sample measured **126.41 MiB aggregate working set**, **86.89 MiB aggregate private memory**, and **15.62 ms CPU** over **10.05 seconds** (**0.0071% normalized CPU**), with 546 handles and 12 threads. This includes the UI and recovery helper. View/interaction state was not controlled; it is not a cold resting or animation benchmark. An earlier sample collected immediately at launch missed the not-yet-created helper and was discarded. The measurement script now rejects samples whose process membership changes during the interval.
