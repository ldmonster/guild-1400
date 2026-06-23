# Wave-20 — Drag-select + drag-cursor + edge-scroll + map drag-scroll

**Agent:** W20-DRAGSELECT · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Owned modules (new):
- `src/gui/dragselect.{h,cpp}`
- `src/gui/edgescroll.{h,cpp}`
- `tests/unit/dragselect_wave20_test.cpp`
- `tests/unit/edgescroll_wave20_test.cpp`

Status: **all assigned functions reconstructed 1:1.** 24 tests / 127 checks pass
(standalone link + inside the `guild` library — both my .o compile clean, no ODR).

---

## What was reconstructed (vs reused vs boundary)

### Reused (no redefinition — extern via `play/picksel_recon.h`, no ODR)
The PURE drag-select geometry kernels already existed in `src/play/picksel_recon.{h,cpp}`
from an earlier wave and are reused directly:
- `play::DragBox`, `play::Viewport`, `play::ProjectParams`
- `play::DragClampX/Y` (the exact `clamp(v, lo, hi-1)` decompiled idiom)
- `play::DragSelectNormalize` (the v4/v23/v24/v22 min/max)
- `play::DragUnitCentroidHit` (8-corner centroid * 0.125 -> project -> contain)
- `play::picksel_const::kEighth` (0.125 == dbl_61E208/dbl_61E210)

`picksel_recon.h` had explicitly marked `0x4ba2bc UpdateUnitList` as **OMITTED** (rule-8
deferred, "command-queue driver, deeply coupled"). Wave-20 completes it (and the
apply/draw/cursor orchestration) by modeling the coupled leaves as mockable hooks.

### Reconstructed 1:1 (this wave)

| addr | name | module | notes |
|------|------|--------|-------|
| 0x4be154 | VIBE_DragSelect_DrawBox | dragselect | 4-edge rect geometry; disasm-exact endpoint wiring incl. the `inc ecx`→`by+1` on edge 2 |
| 0x4bdc3c | VIBE_DragSelect_ApplyToUnits | dragselect | corner-clamp + normalize + 32-slot box hit-test; `(flag&0x800)` gate; +392 writes; weight dbl_61E208 |
| 0x4bdecc | VIBE_DragSelect_ApplyToSelection | dragselect | byte-identical apply loop, weight dbl_61E210; differs only by which selection-flag fn the host runs (modeled as `unit.selFlag`) |
| 0x4ba2bc | VIBE_DragSelect_UpdateUnitList | dragselect | follow-command state machine: 67('C')-kind gate, batch open (v22), >4 early-out (v30→v1>3), +196 follow-list collapse, two tail flushes (Entity29 then SlotReset28) |
| 0x41fcbc | VIBE_DragCursor_SetSprite | dragselect | shape-anim slot lifecycle: set (free-old+register), clear (free+mode 0); slot init -1 |
| 0x41fa1c | VIBE_DragCursor_Render | dragselect | count-badge child-table walk geometry (stride-3 over 6 slots, 26px grid, v10 row/col); `!dword_62D314` gate |
| 0x543994 | VIBE_MapView_UpdateScrollState | edgescroll | 3-block map drag-pan: press latch+snapshot+band-reprogram, held delta-slide, release restore+unlatch |
| 0x4bc07c | VIBE_Hud_UpdateEdgeScroll | edgescroll | edge-snap RE-ATTACH finisher: detach latch (62D4E8/E4), 0.1-tolerance settle gate, free-anim/repose/listener/selection-reset |

### Genuine leaves routed through hooks (rule-3/5/8 boundaries, NOT reconstructed here)
These are coupled subsystems (command queue, character/AI, building-flags, shape/anim
render, 3D listener) reached from the assigned functions; each is exposed as a
mockable hook so the control flow and side-effect ORDER are faithful and testable:

- `VIBE_Render_DrawLineLocked @0x4353dc` — the line blit is the Vulkan/SDL boundary
  (rule 3). We reconstruct the 4-segment geometry/state; only the GPU draw is the hook.
- `VIBE_Shape_ShowFromBankScaled / VIBE_Animation_Apply / VIBE_Coord_Push / VIBE_Velocity_Apply`
  (DragCursor_Render blits) — Vulkan/SDL boundary; the badge-grid geometry is reconstructed.
- `VIBE_Command_QueueRequestSingle49/NamedObject53/Entity29/SlotReset28` (0x494e4c/494f0c/4949c4/4948c8),
  `VIBE_Character_ChangePlayerAction @0x4b09c8`, `VIBE_GameObject_QueryFind @0x5857fc`
  (UpdateUnitList) — command/AI cluster; the state machine driving them is 1:1.
- `VIBE_Building_ComputeSelectionFlags @0x588dec` / `VIBE_Combat_GetSelectionFlag @0x486460`
  (Apply) — the host supplies the per-unit flag; the `&0x800` gate + geometry is 1:1.
- `VIBE_ShapeAnim_GetSlot/RegisterSlot @0x5d8f1c/5d8d54`, `VIBE_State_Update @0x40e9e8`
  (SetSprite) — shape-anim slot allocator; the slot lifecycle is 1:1.
- `VIBE_Anim_FreeObjAnimData @0x5cec00`, `VIBE_Object_SetPosition/SetWorldTranslation
  @0x5af38c/5af50c`, `VIBE_Sound3d_UpdateListener_b5d5c @0x4b5d5c`, `VIBE_Selection_Reset
  @0x4b9444` (UpdateEdgeScroll) — node/listener/selection edges; the latch+tolerance
  branch logic is 1:1.

---

## Critical fidelity details verified against the disasm

- **ConvertX truncation (brief requirement).** `VIBE_Coord_ConvertX @0x5c6b08`
  (`fstcw`/RC=trunc/`frndint`) truncates st0 toward zero. In `UpdateScrollState` the
  held-drag block is `fild (cursorDelta) -> ConvertX -> fistp`. The deltas are
  differences of two `(>>16)` ints (integer-valued), so the truncation is the identity
  here — but the recon keeps the `util::ConvertX` call so a non-integer input would
  still truncate identically. Verified the two `fild/ConvertX/fistp` pairs at
  0x543aeb..0x543b0d map to `var_20 = deltaX`, `var_24 = deltaY`.
- **Coord truncation sites in Apply/DrawBox/Render.** The `>>16` cursor reads
  (`unk_67220E`, `dword_672210`) are arithmetic shifts (`sar`, sign-preserving) —
  reproduced as `>>16` on signed int. No float→int rounding on these paths.
- **DrawBox edge 2 `+1`.** The `inc ecx` at 0x4be18e makes edge-2's y1 == `by+1`
  (not `by`). Pinned in `DragSelectW20_DrawBox.FourEdgesExactEndpoints`.
- **Band reprogram constants.** `512 (0x200)` / `360 (0x168)` viewport, dead-band init
  `0x316,0x257,0,0,1` (get_global_value verified). Pinned in
  `EdgeScrollW20_MapScroll.PressLatchesAndSnapshots`.
- **DragCursor slot init = -1, hidden flag init = 0** (get_global_value: dword_62D30C
  = 0xffffffff, dword_62D314 = 0).

---

## Wiring / handoff (rule 13)

The assigned functions are called from bind-site files I do **not** own (per ownership
rule) — documented here as the handoff:

- **DrawBox (0x4be154), ApplyToUnits (0x4bdc3c), ApplyToSelection (0x4bdecc)** — direct
  callees of the frame loop `VIBE_GameLogic_RunFrameLoop @0x4c09a0` (xrefs confirmed:
  0x4c0dd1 / 0x4c1598 / 0x4c0dcc). The reconstructed frame loop in
  `src/app/frameloop.cpp` is a hook-dispatcher: these belong inside the
  `sub_.renderMainViewFrame()` / `sub_.hudSelectionAndTargets()` subsystem hooks the
  host wires. **Handoff:** the live host calls `gui::DragSelect_DrawBox` /
  `gui::DragSelect_ApplyToUnits` / `gui::DragSelect_ApplyToSelection` from the present /
  HUD-selection subsystem with the real unit array + scissor rect + projection state.
- **UpdateUnitList (0x4ba2bc)** — called from `VIBE_Hud_UpdateSelectionAndTargets
  @0x4ba614` (the `hudSelectionAndTargets` hook). **Handoff:** that subsystem calls
  `gui::DragSelect_UpdateUnitList` with the live staged-cell array + command-queue hooks.
- **DragCursor_Render (0x41fa1c)** — called from `VIBE_DragCursor_RenderMouse @0x41fc58`.
  **Handoff:** the cursor-render path calls `gui::DragCursor_BadgeLayout` and routes the
  shape/label blits through the Vulkan cursor render hook.
- **DragCursor_SetSprite (0x41fcbc)** — called on cursor-mode changes; `gui::DragCursor_SetSprite`
  wired with the shape-anim slot hooks.
- **MapView_UpdateScrollState (0x543994)** — called from `VIBE_MapView_PanelDispatcher
  @0x5441d0` (the city/world map panel). **Handoff:** the map panel calls
  `gui::MapView_UpdateScrollState` with its scroll record (offX +600 / offY +584).
- **Hud_UpdateEdgeScroll (0x4bc07c)** — called from many panel/dialog dispatchers and
  the city HUD click router. Note `src/sim/wire_apply_input.cpp` already documents this
  as "different ABI, needs live CameraState" -> inert; `gui::Hud_UpdateEdgeScroll` is the
  reconstructed body the host binds when the CameraState/node hooks are available.

No bind-site files were edited (ownership rule). `progress/INDEX.md` not edited (per brief).

---

## Tests (rule 11)

`tests/unit/dragselect_wave20_test.cpp` (15 tests):
- DrawBox: inactive→0 segments, 4-edge exact endpoints (incl. `by+1`), hook routes 4×.
- Apply: corner clamp to scissor `x1-1`, inside/outside selection, `0x800` flag gate,
  dead-slot / inactive-slot untouched.
- UpdateUnitList: non-player batch open + follow-list collapse + tail flushes;
  player-path queue sequence; non-'C' kind skipped (still clears pending).
- SetSprite: set registers / re-set frees-then-registers / clear frees+mode-0 /
  clear-with-no-slot just resets mode.
- Render: hidden→0 badges, grid skips `-1` slots, rows wrap every 3.

`tests/unit/edgescroll_wave20_test.cpp` (9 tests):
- MapScroll: press latch+snapshot+band reprogram (688/540/200/100 golden), press-frame
  delta-0 no-change, held +delta slide, negative-delta truncate, release restore+unlatch,
  idle no-op.
- Reattach: detached+settled (free+repose+listener+selreset), detached+not-settled
  (free, no repose), not-detached+settled (selreset only), not-detached+not-settled
  (listener only).

Run (standalone): `g++ -std=c++17 -Isrc -Iinclude -Ishim -I. -Itests/framework
src/gui/dragselect.cpp src/gui/edgescroll.cpp src/play/picksel_recon.cpp
src/util/coord.cpp tests/unit/dragselect_wave20_test.cpp
tests/unit/edgescroll_wave20_test.cpp tests/framework/test_main.cpp -o w20 && ./w20`
→ **127 checks, 0 failures.**

## Build note
My two source files compile clean both standalone and inside the globbed `guild` static
library (objects produced: `CMakeFiles/guild.dir/src/gui/dragselect.cpp.o`,
`.../edgescroll.cpp.o`). The `build/` tree currently fails on two **untracked** files
from other concurrent waves (`src/sim/command_leaves.cpp` `RunActionOrFree` undeclared;
`src/sim/he_messaging.cpp` 64-bit ptr→i32 casts) — outside my ownership and unrelated to
this wave's modules.
