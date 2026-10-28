# Dako 4.0

A native Windows app island built with C++20, Win32, Direct2D, DirectWrite and DirectComposition.
The resting strip merges pinned and running apps. Hover opens media Peek when a playing or paused session
has metadata; otherwise it opens the full island. Click, Ctrl+Alt+Space, or scrolling toward the screen
centre expands it. Leaving collapses after a short grace period. Esc closes one level.

Midnight is the default, with Paper/System themes, a sampled gold accent and bundled Rubik. The seven pages
are Controls, Today, Stats, Clipboard, Apps, Wallpaper and Power. Top/bottom geometry shares one mirrored
shape. There is no mascot, browser engine, AI service, voice feature or required account.

The [V4 foundation plan](dako-v4-foundation/DAKO_V4_PLAN.md), particularly its locked decisions in section
0.5, governs this release. [Verification](docs/VERIFICATION.md) records automated results and the remaining
physical acceptance checks. Section 10's larger ideas remain a prioritized roadmap.

## Run

Extract the portable ZIP and run `Dako.exe`. Run `DakoSettings.exe` for the independent settings application.
Windows 11 x64 is the target; Windows 10 is best effort. Builds are unsigned and do not require uiAccess.

```powershell
.\build\Dako.exe
.\build\DakoSettings.exe
.\build\Review.cmd
```

Review opens an isolated preview without saving preferences or changing Explorer, Windows preferences,
wallpaper, power state or the real clipboard. Explicit app launches and file selection remain available.
Normal startup displays the app island without reserving work area. Replacing the taskbar is a separate
opt-in with a recovery guard. Ctrl+Alt+Shift+T restores Explorer; Ctrl+Alt+Shift+Q exits and restores managed
state. Ctrl+Alt+K opens the command palette. The settings application can rebind the opening shortcut.

Offline defaults On. Weather needs a city and Online; speed testing also needs its own opt-in and an
explicit Run action. Optional clipboard history starts only after consent and keeps bounded records in
memory by default. Enter pastes into the previously focused standard Windows Edit/RichEdit field; protected,
read-only, elevated or unsupported targets are rejected. Use Copy then your editor's paste for other apps.

## Build and verify

Install x64 MSVC C++ tools and a Windows 10/11 SDK through Visual Studio Build Tools or Community.

```powershell
.\scripts\build.ps1
.\scripts\test-island.ps1
.\scripts\test-v4.ps1
.\scripts\test-motion.ps1
.\scripts\verify.ps1
.\scripts\test-live.ps1
.\scripts\test-shell.ps1
.\scripts\package.ps1
```

Builds use C++20 and `/W4 /WX`, discover production sources automatically, and produce `Dako.exe`,
`DakoSettings.exe` and `dako_core.lib`. Fonts and wallpapers accompany the executables. Compiler outputs,
linker map, captures and test reports stay under `build/`. Packaging creates
`dist/Dako-4.0.0-windows-x64.zip` and `dist/SHA256SUMS.txt`.

Read [architecture](docs/ARCHITECTURE.md), [settings](docs/SETTINGS.md), [privacy](docs/PRIVACY.md),
[verification](docs/VERIFICATION.md) and [changelog](CHANGELOG.md). Prior briefs and API experiments are
preserved in [docs/archive](docs/archive).
