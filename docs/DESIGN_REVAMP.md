# Portfolio design revamp — October 25, 2026

The implementation uses the source of [VEDLEGENDARY/portfoliowebsite](https://github.com/VEDLEGENDARY/portfoliowebsite) at `704079710858468f90be0629ccb3913d0595ad6e`. The reference checkout is local to `build/portfolio-reference` and is excluded from Dako's source and package.

The reference's `src/app/globals.css` supplies paper `#eeebe3`, ink `#0f0f0e`, signal `#ff4a1c`, thin dividers, restrained chips, serif headings and small monospace labels. `src/app/layout.tsx` identifies Instrument Serif, Geist and Geist Mono. Dako uses native Georgia, Segoe UI and Consolas to preserve that hierarchy without adding downloaded fonts or a web runtime. The code in `src/lib/motion.js`, `src/lib/ui.js` and `src/components/work-list.js` informed reduced-motion handling, restrained feedback and keeping one content view visible.

## Resulting behavior

- Paper is the default for new settings. The ink variant, explicit system appearance and high contrast remain available. Saved appearance preferences are respected. Portfolio orange is the default accent; wallpaper-derived accents remain optional. Text on orange and tinted surfaces gets a separately contrast-adjusted foreground.
- A small wordmark, section heading and underlined navigation replace the capsule-only header. All seven main views share a width, so their navigation stays under the pointer. Smaller displays shed the decorative header and retain scrolling content, fixed actions and compact navigation.
- Buttons, native text fields and keyboard/accessibility actions no longer wait for geometry or pane springs to settle. Hit testing uses the actual rounded, presented surface and logical layout bounds. Transparent parts of the envelope yield input to the desktop.
- Outgoing panes are no longer captured or composited over the incoming view. List rows take their new positions immediately, so filtering, pagination and device refreshes cannot animate rows through neighboring headings or controls.
- The volume label has its own row; the slider and media card have separate bounds. During a volume drag, the displayed thumb follows the real volume immediately. Native input backgrounds match their surrounding surfaces.
- The application manifest enables Common Controls v6 so native edit placeholders appear. The render verifier retrieves the task cue text to prevent a regression. Isolated previews do not register global shortcuts and label their footer accordingly. Preview executable recognition is case-insensitive, matching Windows filename semantics, including launchers that normalize the path's capitalization.
- Wallpaper cards form a filmstrip with 12-DIP gutters, including fractional animation positions. Their hit bounds match the rendered cards. Existing explicit application and pending/applied identity are retained.
- Geometry springs use a faster response. Display refresh queries run on monitor/display changes instead of every motion frame. Window regions and backdrop placement update only when their geometry changes. Composition readiness waits run in the frame worker instead of blocking the UI during graphics creation.

## Verification

- Release compilation uses MSVC C++20 `/W4 /WX`.
- `scripts/test-motion.ps1` covers spring reversal/settlement, proximity, monitor/DPI/text geometry, pagination, magnification, frame backpressure and async cancellation.
- `scripts/verify.ps1 -OutputName Dako.next.exe -RenderDirectory build/design-after` produces 127 fixture PNGs plus geometry, identity, catalog, MSAA, draft/caret and persistence assertions.
- `scripts/test-live.ps1 -OutputDirectory build/design-live-full` exercises the production renderer with inert data: 28 frame-independent mouse navigations, content controls and native fields during motion, filmstrip gutters, contrast, 50 lifecycle cycles, rapid reversals, large-text fields, search selection, resource bounds, a 60-second rest interval and read-only service shutdown.

Fixtures and native lifecycle checks do not validate physical wireless connections, wallpaper application, power actions or a human screen-reader journey. These actions are not needed for the design checks.

The release build, motion/async checks, all 127 fixture renders and full 50-cycle lifecycle run passed on October 25. The 120 Hz host reported 1.73 ms p95 CPU paint time, 9.32 ms p95 animation tick interval, and 0.0663% normalized CPU over the 60-second rest sample. The eligible DXGI subset reported 8.33 ms p95 presentation intervals (10,168 samples; 97 invalid/disjoint observations and 543 grouped presents excluded from that percentile). These are measurements from this session, not a guarantee for other hardware. Results are in `build/design-live-full/results.txt`; fixture images are in `build/design-after`.

Interactive desktop review also confirmed the live Controls/Today/Apps layouts and text entry in the native task field. A preview-local draft was used; production tasks were not edited.

Release validation found a collapse cleanup gap when a frame stalls for more than 500 ms. Snapping motion now releases the collapsed graphics resources, and immediate reduced-motion collapse does the same. The native lifecycle harness explicitly exercises both paths before its repeated-use checks.
