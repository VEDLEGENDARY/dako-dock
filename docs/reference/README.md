# Visual reference notes

Source: https://hitodama.in/#island, inspected on October 23, 2026.

The public desktop section plays an animated UI demonstration. In browser inspection it is not exposed as an HTML video with playback controls. We study its visible frames and interactions rather than assume its implementation or download its application.

Observed states:

| State | Visible structure |
| --- | --- |
| Compact media | Black top-edge panel, square artwork, centered title/artist, thin timeline, three transport controls |
| Controls | Seven-tab strip; 3 + 2 gold/charcoal tiles; full-width volume; inset media card |
| Clipboard | Wide island, search, type pills, two-column list/detail view, bottom keyboard hint bar |
| Wallpaper | Wide island, Recent / My folder / pictures filters, center-emphasized landscape carousel |

Colors and metrics in the implementation are approximations derived from displayed screenshots, not measured original design tokens. Initial panel size is 600 × 368 device-independent pixels; the compact state is 300 × 100. More precise matching will be iterative.

Local screenshots are reference material only and are ignored by version control. The product does not bundle the site's logos, mascot, page code, or photographs.
