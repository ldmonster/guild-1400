# Wave-12 hardening — W12-SCREENS cluster (menu + screen flow)

MCP was DOWN for this wave: NO new 1:1 reconstruction. This pass HARDENS the
src/play/ menu + screen-flow modules (NOT the 3D bind sites) with an ASAN+UBSAN
build and malformed/degenerate-input tests, and FIXES the memory-safety / UB it
surfaced. Every golden value (scroll model, slot placement, option ranges, the
new-game commit record writes, the FSM transition history) stays BYTE-IDENTICAL;
only out-of-envelope (degenerate / malformed / oversized) inputs are made
fail-safe.

## Cluster owned

Sources (edited only these + the test files + this doc):
- `src/play/native_main_menu.{h,cpp}`, `sdl_menu.{h,cpp}`
- `src/play/sdl_city_screen.{h,cpp}`, `sdl_charcreate_screen.{h,cpp}`,
  `sdl_charintro_screen.{h,cpp}`, `sdl_choosehistory_screen.{h,cpp}`,
  `sdl_chooseplayer_screen.{h,cpp}`, `sdl_credits_screen.{h,cpp}`,
  `sdl_loadgame_screen.{h,cpp}`, `sdl_options_screen.{h,cpp}`
- `src/play/menu_recon_network_screens.{h,cpp}`, `menu_recon_transition.{h,cpp}`,
  `menu_assets.{h,cpp}`, `mode_fsm.{h,cpp}`, `newgame_apply.{h,cpp}`

NOT edited (per the brief — bind sites / other clusters): city_view3d,
universe_render, scene_view, sdl_session, terrain_render, session_persons3d,
sdl_city_screen3d, and anything under src/gui, src/io, src/render.

## ASAN+UBSAN build

```
cmake -S . -B build-asan-screens -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-screens --target <cluster test targets> -j$(nproc)
```

Cluster test targets run clean under ASAN+UBSAN (also re-verified in the normal
`build/`):

| target | checks |
|---|---|
| sdl_credits_screen_test      | 47  |
| sdl_loadgame_screen_test     | 275 |
| sdl_options_screen_test      | 114 |
| sdl_city_screen_test         | 38  |
| mode_fsm_test                | 66  |
| newgame_apply_test           | 142 |
| chooseplayer_textedit_test   | 19  |
| sdl_menu_test                | 28  |
| sdl_charcreate_screen_test   | 42  |
| menu_recon_transition_test   | 75  |
| menu_assets_test             | 14  |
| native_main_menu_itest       | 10  |
| menu_loadgame_flow_itest     | 26  |
| chooseplayer_screen_e2e_test | 9   |
| newgame_result_itest         | 23  |
| charintro/choosehistory e2e  | 1,1 |

The `build-asan-screens/` dir is removed at the end of the pass.

## Bug FIXED — credits-crawl negative-Y heap overflow (REAL)

`src/play/sdl_credits_screen.cpp` — `RunCreditsScreen` crawl loop.

ASAN caught a heap-buffer-overflow (WRITE, 220 bytes BEFORE the W*H scratch) in
`render::DrawGlyph` reached from the credits row label. Root cause: the crawl
sweeps each line's `y` from `-fbH` (text starts below) toward 0; the old skip
guard was `if (ly < -lineH || ly >= H) continue;`, which let a line at
`ly ∈ (-lineH, 0)` be drawn. `render::DrawGlyph` (the faithful reconstruction of
`VIBE_Render_DrawGlyph`) clips the RIGHT (`x+5 > dibStride`) and BOTTOM
(`y+7 > screenHeight`) edges, but has NO `y < 0` / `x < 0` guard — for a negative
`y` whose `y+7` is still positive (1..6) the bottom check passes and the 7-row
raster writes starting one or more rows BEFORE the buffer. The original relied on
the credits form/window clipping those pixels; in the reconstruction the screen
draws straight into the scratch, so the credits crawl was the unique caller that
ever fed `DrawGlyph` a negative coordinate.

FIX (faithful — the original never corrupted memory, and off-surface glyphs were
never correctly visible):
- The crawl now draws a line only when its full 7-row glyph band lies inside
  `[0, H)`: `if (ly < 0 || ly + kGlyphH > H) continue;` (`kGlyphH = 7`, the
  `render::DrawGlyph` row span).
- Added `DrawCrawlLine`, which truncates the drawn text so the unclipped 5x7
  raster (6px advance, 5 cols/glyph) never writes past `[0, W)` — equivalent to
  `DrawGlyph`'s own per-glyph right-edge drop, defensive against narrow buffers.

Goldens unchanged: the scroll model (`Credits_ScrollStep` / `InitialOffset` /
`AdvanceOffset` / `ScrollComplete`) and `r.scrollOffset` / `framesPresented` are
untouched; the deterministic-offset golden tests still pass byte-for-byte.

Pinned by: `CreditsScreenUnit.CrawlNegativeYNoOob`,
`OverlongLinesNarrowBufferNoOob`, `ZeroAndOneLineNoOob`, `ManyLinesNoOob`.

## Audited-clean (no fix needed)

- **Text-entry buffer (`ApplyTextEdit`, sdl_chooseplayer_screen.cpp)** —
  `std::string`, hard-capped at `maxLen` BEFORE append; control bytes (`< 0x20`,
  incl. embedded NUL) filtered; backspace guarded by `!empty()`. No OOB / no NUL
  injection. New tests: overlong single paste, `maxLen==0`, embedded NUL, high
  Latin-1 bytes, backspace storm.
- **mode_fsm** — `ModeForSessionFlags` is total (priority NewGame>Load>Network>
  MainMenu, unknown high bits → MainMenu); `GameModeName` has a return-after-
  switch fallthrough (never null, handles out-of-enum); `enter`/`stepMenu` use a
  `std::vector` history; the lifecycle edges (`enterInGame`/`endSession`) are
  no-ops outside their source state. New tests cover all of these.
- **sdl_loadgame_screen** — slot table (`16 * 544`) and `out[16]` views indexed
  by `kLoadGameSlotCount`; `n` clamped to 16 (`0x569d64`); `nameZ[33]` from a
  32-byte field with explicit terminator; `FindSaveSlot` result re-checked
  `[0,16)` before placement; the confirmed-slot result re-checks `[0,16)`. New
  tests: corrupt/empty/1-byte/truncated headers skipped, full-width name
  terminates, overlong name on a tiny buffer, no-saves confirm stays in bounds.
- **sdl_options_screen** — `ActuateValue` clamps into `[minV,maxV]`; value text
  via `snprintf` into `buf[16]`; the internal `RowsToConfig` fixed indices match
  the row count `OptionsRowsFor` always produces. New tests: max-wrap, wildly
  out-of-range seed values render + actuate into range, tiny-buffer render for all
  three pages.
- **sdl_city_screen** — every `cfg.cities[i]` access bounded by `n =
  cfg.cities.size()`; `selected` only set from valid hit rows. New tests: single
  city, 64 cities (overflowing the panel), overlong names on a 64x48 buffer.
- **newgame_apply** — name copies are bounded: `StrNCopyPad(fa+0x40, …, 16)`,
  `StrCopyCapped(+0x30, …, 16)`, `StrCopyCapped(+0x1F0, …, 40)`;
  `professionVariant` clamped (`<0 ? 0`) and `LookupTypeRecordA` itself falls
  back to record 0 for out-of-range → all-zero talents. New tests: oversized
  first/family names cap to field width (and the adjacent +0x50 word is intact),
  empty names, negative + huge profession variant → record-0 talents, oversized
  avatar model name caps at +0x1F0.
- **menu_recon_transition** — hotspot table `recs[65]` (`v6 ∈ 1..64` guarded),
  `Hotspot_Remove` clamps `index ∈ [0,65)` before `memset`, the fade-slot loops
  (`slots[32]`) keep `i < kFadeSlotCount` on every `base[i]` access. (The
  `t.count` drift on rejected register / asymmetric decrement is FAITHFUL to the
  original and never used as an index — semantic, not a memory bug.)
- **menu_assets / sdl_menu / sdl_charcreate / native_main_menu** — all sprite/
  background/button blits and grid hit-tests are bounded by their declared
  counts; the 5x7 raster's right/bottom clip handles oversized text on small
  buffers (verified via the tiny-buffer tests on the sibling screens).

## DOCUMENTED for other owners (root in a non-owned cluster)

- **`io::SaveBrowserEnumerateSaveFiles` (`src/io/save_browser.cpp:65`) — unbounded
  output write.** It emits one `SaveBrowserRecord` per matching `.SAV` file into
  `outRecords[emitted++]` with NO upper bound. `sdl_loadgame_screen.cpp`'s
  `LoadGameBuildSlotViews` calls it with a fixed `static SaveBrowserRecord
  recs[256]`; a saves directory with > 256 `.SAV` files overflows that buffer
  (heap/BSS OOB write). The enumerate function has no capacity parameter, so the
  bound belongs in the IO owner's signature (add a `maxRecords` cap and return
  `min(emitted, cap)`), after which the loadgame caller would pass
  `256`/`kLoadGameSlotCount`-derived capacity. NOT fixed here (io/ is out of this
  cluster's ownership). De-facto today the cap is the 256-element buffer; the
  16-slot return cap (`0x569d64`) is applied only AFTER the full enumeration.
- **`render::DrawGlyph` (`src/render/text_raster.cpp:127`) — no negative-coord
  guard.** Clips right/bottom but not `x < 0` / `y < 0`. Faithful to
  `VIBE_Render_DrawGlyph` (callers were expected to be in-window). The W12-SCREENS
  credits fix removes the only negative-coordinate caller in this cluster; if the
  render owner wants belt-and-suspenders, adding `x >= 0 && y >= 0` (matching the
  `DrawTextCp1251` guard at text_cp1251.cpp:75) would make it robust for any
  future caller. Render is out of this cluster's ownership.

## BEHAVIORAL — needs MCP (not changed)

None found in this cluster: every fix above is a memory-safety bound on degenerate
input that leaves observable output on valid input byte-identical. No 1:1
control-flow / constant / table question arose.
