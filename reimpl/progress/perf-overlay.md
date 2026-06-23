# In-game performance overlay (Steam-style FPS monitor)

User request (2026-06-09): an in-game performance monitor like Steam's FPS counter
(help.steampowered.com/.../3462-CD4C-36BD-5767). ADDITIVE host diagnostic — the
original game has no FPS counter, so this is not reverse-engineered engine logic; it
uses only the existing render text + surface primitives (no new deps, no platform calls).

## Module — `src/render/perf_overlay.{h,cpp}`
`render::PerfOverlay`:
- `Frame(nowMs)` once per presented frame → keeps a 120-sample ring of frame intervals,
  a smoothed FPS + frame time, and refreshes the *displayed* number a few times/sec
  (readable, not jittering). Zero-interval frames are clamped (FPS stays finite).
- `Draw(Surface*)` / `Draw(uint32_t* px, w, h)` composites into a chosen corner:
  * detail 1: `N FPS` (colour-coded green/yellow/red by health, like Steam),
  * detail 2: + `ms` frame time,
  * detail 3: + a frame-time bar graph (green/yellow/red per-bar, 50 ms = full height),
  behind a high-contrast dark box. 16/32 bpp; 32 bpp packs raw 0xAARRGGBB via the new
  `render::Format8888()` (the 32 bpp software surfaces store XRGB but carry a 565 `fmt`).
- `CycleDetail()`: off → 1 → 2 → 3 → off (driven by a hotkey).
- `GlobalPerfOverlay()`: process-global, configured from the environment on first use —
  `GUILD_PERF_OVERLAY=0|1|2|3`, `GUILD_PERF_OVERLAY_POS=tl|tr|bl|br`.

## Wired into the live present loops (rule 13)
- `play::RunCityScreen3D` (the 3D New-Game city pick) — `Frame`+`Draw` before the blit,
  **F11** cycles the overlay.
- `play::RunSdlMenu` (the main menu) — same, via the raw-buffer `Draw` overload, **F11**.
Other screen loops can opt in with the same two calls + the global instance.

## Tests (rule 11)
- `perf_overlay_test` (unit): FPS from 16 ms / 20 ms cadences (~62.5 / 50 FPS), zero-
  interval clamp, `CycleDetail` transitions, and a render smoke test (paints the chosen
  corner, leaves the opposite corner untouched, no-op when disabled). 17 checks.
- `perf_overlay_scene_e2e` (e2e): composites the overlay (detail 3) over the real
  ChooseCity render → `/tmp/cc_overlay.bmp` (FPS + ms + graph, colour-correct).

Both presets green: portable 1153/1153, vulkan-sdl-system 1153/1153.
