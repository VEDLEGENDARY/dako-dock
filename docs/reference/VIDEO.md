# Local reference video

Source: `reference/example.mp4`, supplied by the user; 32.67 seconds, 1558 × 826, 30 fps.

The video was decoded locally. Sampled frames are in `build/reference-frames`; no media was uploaded. `scripts/inspect_video.py` reproduces the extraction with the optional PyAV package installed under `build/video-tools`.

## Observed structure

- 0–2 s: narrow top-edge media island.
- Around 3–5 s: Controls, with three tiles, two wider tiles, volume, and media transport.
- Around 6–7 s: Today expands horizontally into clock, weather, current week, focus timer, and tasks.
- Around 8–9 s: Stats shows circular utilization indicators, storage/network/uptime, and process/network details.
- Around 10–11 s: Clipboard uses list/detail columns.
- Around 12–14 s: Apps is a searchable vertical list with section labels and small app badges.
- Around 15–24 s: Wallpaper shows a moving, center-emphasized photo carousel and circular wallpaper-change reveals.
- Around 26–29 s: a short Power panel with four equal actions.
- End: the island withdraws to a small top-edge tab.

## Resulting design choices

Use one dark material palette, one gold accent, consistent card radii and typography, and animated highlight/selection states. Resize the panel around a fixed center without stretching glyphs. Keep distinct reference-driven widths/heights for each view.

The user specifically requested that browser tabs remain usable. The dock therefore rests as a 148 × 4 DIP edge handle. It reveals after deliberate pointer proximity, and the open body starts 96 DIP below the display edge. The area between the handle and body is excluded from the native window region, rather than occupied by an invisible input surface. Pointer travel through this gap is protected by a dismissal grace period.

Today uses real local clock/calendar data, a functioning focus timer, saved tasks, and native power status. Weather is not fabricated. Stats uses real CPU/RAM/disk/network/uptime data and Dako memory counters. GPU telemetry and external weather data remain outside this revision.

The system wallpaper change itself still uses the native Windows wallpaper operation; it does not reproduce the video’s full-desktop circular reveal. The in-panel carousel is being animated independently of Windows' background transition.
