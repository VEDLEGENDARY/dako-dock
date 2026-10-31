# Owner reference audit

The owner reaffirmed the [earlier eight images](../../../v4/references/README.md) on October 30.
The explicit bottom navigation, floating gap and full browser work area override their top attachment.
The locked no-mascot decision still excludes image 1. Native content remains upright at either edge.

| Reference | Corrected composition | After render |
|---|---|---|
| 2: rest | Content-fit app pill, pinned/running icons, 8 DIP floating edge gap; production overlay reserves no work area | [Dock](v4-dock-1-150.png) |
| 3: Peek | Artwork and centered metadata above progress; plain transport glyphs below; redundant inner card and gold play fill removed | [Peek](v4-peek.png) |
| 4: Controls | Three connection/output tiles, two mode tiles, centered compact labels, gold active fills, full-width volume, horizontal media; brightness moved into Menu | [Controls](v4-Controls.png) |
| 5: Wallpaper | Source chips, center photo with four separate 64-DIP neighboring strips and 12-DIP gutters, common skew, rounded photos; extra selection ring/dots and global fit/apply footer removed; commands retained in Menu | [Wallpaper](v4-Wallpaper.png) |
| 6: Clipboard | Search and filters share a row; grouped list and preview columns, separated metadata, compact contextual actions; Refresh/Clear live in Menu | [Disabled](v4-Clipboard.png), [enabled fixture](v4-Clipboard-enabled.png) |
| 7: Today | Narrow stacked clock with three dots, weather, wider week/event/focus column, full-height tasks; focus readout inside ring, Start/Reset below presets | [Today](v4-Today.png) |
| 8: Stats | Distinct gauge colors, clock/process/VRAM/free-memory badges, ranked app markers and proportional bars, download and upload readouts | [Stats](v4-Stats.png) |

Primary tabs and Menu share one bottom row; top placement mirrors that row. Narrow/large-text layouts
reflow rather than reducing hit targets below 32 DIP. Apps and Power use the shared appearance.
The owner's pixel-accuracy refinement supersedes the earlier common-size pass: pages now retain their
reference proportions and animate smoothly between them, with the attachment edge held in place.

| Page | Preferred panel (DIP) | Content canvas (DIP) |
|---|---|---|
| Controls | 580 × 368 | 548 × 288 |
| Today | 800 × 416 | 768 × 336 |
| Stats | 800 × 320 | 768 × 240 |
| Clipboard | 800 × 424 | 768 × 344 |
| Wallpaper | 944 × 304 | 912 × 224 |

The reference content proportions are retained; the relocated navigation keeps 32 DIP minimum targets.
Panels clamp and reflow within the monitor at larger DPI/text scales. The wallpaper center card remains
16:9 and fits below its source row, with neighboring cards extending behind it.

## Data and accessibility differences

- Media, photos, events, applications and system readings vary with the machine. Fixtures use labeled
  synthetic content. CPU GHz is the registry-reported base clock, read once alongside the CPU name;
  it is identified as base clock in the accessible description. GPU memory shows the available total.
- Offline and explicit consent remain enforced. Clipboard capture is not enabled to resemble a populated
  mockup: the disabled state exposes Enable history. Speed results remain unavailable until an authorized run.
- Brightness opens an in-island percentage editor from Menu. Wallpaper fit, target, Apply and paging remain
  in Menu. Today settings also move into Menu. Native accessibility checks cover the reachable Menu button
  and its item states instead of asserting removed, redundant controls on the main canvas.
- Dark mode uses the Contrast glyph from Microsoft's
  [Segoe Fluent Icons mapping](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font).
- Geometry, clipping, hit sizes and MSAA are tested across the software DPI/text matrix. Physical monitor
  transitions, backdrop edge quality and a complete screen-reader journey remain separate manual checks.
