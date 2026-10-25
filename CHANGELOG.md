# Changelog

## 2.0.0 — 2026-10-25

- Introduce a paper, ink and orange native interface with shared navigation across seven views.
- Add DirectComposition rendering with an opaque fallback, bounded frame scheduling and interruptible motion.
- Improve responsive layout, DPI/text scaling, native text fields, keyboard navigation and accessibility metadata.
- Keep normal startup independent of desktop replacement; taskbar suppression remains explicitly opt-in.
- Expand geometry, motion, async, fixture and native lifecycle verification.
- Release graphics resources when collapse completes after a stalled frame or with reduced motion.
- Add portable Windows x64 packaging with the offline wallpaper collection and SHA-256 checksums.

This is an early unsigned Windows build. Physical Wi-Fi/Bluetooth pairing and reconnection, mixed-monitor changes, a full screen-reader journey and complete GPU/DWM tracing remain acceptance work. The fixture suite does not certify those paths. See `docs/DESIGN_REVAMP.md` and `docs/VERIFICATION.md` for evidence and limits.
