# Unified surfaces execution

Baseline: local `6d72f1c`, compared with the brief's `083f8c1`, October 31, 2026.

The earlier polish already removed Fill's radius clamp, unified Rubik, fixed backdrop/paint
outline correspondence, replaced island prompts/menus, and floated the dock without reserving
work area. The brief's old numeric radius map is therefore partly stale.

Still confirmed locally: `OpenFromDock` unconditionally releases graphics and moves the envelope;
status buttons route to different tabs/flyouts; native dock app/overflow menu fallbacks, tooltip
HWNDs and a separate thumbnail HWND remain. Place uses a four-DIP overlap. The thumbnail uses
320 DIP width and a six-pixel gap. These paths require replacement, not just token substitution.
The exact live join artifact and DWM thumbnails above a composition swapchain remain rendering
questions; code inspection alone is not treated as visual proof.

`verify.ps1 -RenderDirectory build/tests/unified-baseline` passed 1,087 PNGs and the existing
geometry, identity, text/DPI and MSAA assertions. Representative baseline captures and the raw
manifest are preserved here. The full baseline is retained under ignored build outputs.

The referenced DAKO_UPSCALE_AND_TASKBAR.md is absent locally; the available earlier polish
brief and the new addendum provide sufficient explicit requirements to proceed.

Implementation order: shape foundation, consumer migration, union geometry, shared motion and
stable envelope, Quick Panel/interaction routing, in-surface app content/previews/tooltips,
then expanded fixtures, join/radius/navigation tests and performance acceptance.
