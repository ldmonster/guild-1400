# Wave-19 — Groundplan / blueprint window (coupled drivers)

**Agent:** W19-GROUNDPLAN · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the **coupled driver half** of the `VIBE_Groundplan_*` cluster — the 7
functions that `src/world/groundplan_recon.cpp` explicitly OMITTED (it kept only the pure
layout/eligibility math: `GetBuildingState`, `GetWappenLabelId`, `RetZero`). Together the
two files now cover the whole cluster.

New module: `src/gui/groundplan.{h,cpp}` · tests `tests/unit/groundplan_test.cpp` +
guarded real-asset `tests/e2e/groundplan_blueprint_e2e_test.cpp`.

---

## Reconstructed (1:1 from the Hex-Rays decompile, addresses verified)

| addr | name | reconstructed as | notes |
|------|------|------------------|-------|
| 0x4ae3b8 | VIBE_Groundplan_CreateWindow | `Groundplan_CreateWindow` | Coord_Push(0,0,W,H); hotspot @ (H-80,17,48,48); window(4,10,48,648); grab backdrop shape; clear 15 room slots; stash sceneId; create 110px panel surface; scroll child obj. |
| 0x4ae678 | VIBE_Groundplan_SetWidgetsVisible | `Groundplan_SetWidgetsVisible` | form + event-panel bar + every populated widget/object toggled; widAC only hidden; -1 slots skipped. |
| 0x4ae828 | VIBE_Groundplan_DestroyWidgets | `Groundplan_DestroyWidgets` | hide form, destroy each populated slot -> -1, hide scroll child. |
| 0x4aea5c | VIBE_Groundplan_LoadBlueprintBmp | `Groundplan_LoadBlueprintBmp` | **path-selection switch** + surface copy (G,R,B swap) + "_C.bmp" derivation + **per-pixel collision-map scan** spawning room hotspots. |
| 0x4af038 | VIBE_Groundplan_RenderBlueprint | `Groundplan_RenderBlueprint` | dirty-cache rebuild of room hotspots (reload bmp), blueprint blit, hover (flag 0x2000) pixel-pick -> tooltip/enter-room, else city-wappen draw. |
| 0x4af4a8 | VIBE_Groundplan_BuildInfoPanel | `Groundplan_BuildInfoPanel` | bit0 clear = full teardown+recreate widget set; set = in-place refresh; `result = 67*selectedPlot`; **clock/season math** (ConvertX truncations). |
| 0x4b0758 | VIBE_Groundplan_FadeInScene | `Groundplan_FadeInScene` | surface fill + one BuildInfoPanel(1,…) + black fade-in frame + recentre window. |

### Genuinely reconstructable LEAF logic (rule 8 — NOT faked)
- **Blueprint filename selection** (`Groundplan_PickBlueprintName`): the exact switch from
  0x4aea5c. Category 1 → wirtshaus(type 22)/parfuemerie(room 14)/tinkturei(room 8);
  category 3 → rathaus(type 15)/Arbeiterunterkunft(type 1); category 5 → always zunfthaus;
  default → geldleihe(type 5)/lagerhaus(room 9)/kirche(room 7)/stadtwache(room 19); generic
  fallback `riss_handwerksbetrieb.bmp`. **All 12 path strings get_bytes-verified** @0x61da5c..
  0x61dc38 incl. the `%sbmp\groundplans\riss_%s.bmp` format and `_C.bmp` suffix.
- **Clock-hand math** (`Groundplan_ComputeClockHands`): the float→int block of 0x4af4a8.
  - minuteCell = trunc(((q>>32)%60) * 0.8)
  - hourCell   = trunc((60*WORD1(q)+HIDWORD(q)) * (1/60) * 4.0) % 48
  - moonCell   = trunc(48.0 + fmod(phase,2π)*(1/2π)*48.0 + 0.5) % 48
  FP constants **get_bytes-verified**: dbl_61DC88=0.8, dbl_61DC90=4.0, dbl_61DC98=1/60,
  dbl_61DCA0=2π, flt_61DCA8=1/2π, dbl_61DCB0=48.0, dbl_61DCB8=0.5. ConvertX@0x5c6b08
  **truncates** (per brief) — modelled as a C truncating cast at each site.
- **Per-pixel collision-map scan**: w×h walk over the `_C.bmp` surface, marker-colour match
  → next-pixel palette index → room game-object query → hotspot widget at
  `(j + screenH-112-14, row+76)`. Verified end-to-end over a real asset (e2e below).

### Coupling model (rule 8 — engine exposed, not faked)
The originals touch ~50 BSS globals + ~30 subsystem calls. Rather than stub the engine, all
mutable globals live in **`GroundplanState`** (each field carries its `dword_631xxx` address)
and every leaf call routes through **`GroundplanBackend`** (a vtable of the real reconstructed
engine leaves, wired at the call site). The **control flow, constants, path table and pixel
scan are byte-for-byte**; only the leaf calls are indirected. Null/recording backends exercise
the reconstructable math in isolation.

### REUSED (no ODR — declared/owned elsewhere)
- `render::BmpLoadBuffer` / `render::PictureCreateSurfaceFromBmp` (src/render/bmp.cpp,
  picture_recon_bmp.cpp) — the actual BMP codec `VIBE_Picture_CreateSurfaceFromBmp`@0x422ae4
  uses. The e2e drives the real decoder; production wires `PictureCreateSurfaceFromBmp` into
  the backend.
- `VIBE_Building_MapTypeToCategory`@0x5878b0 (src/sim) — via `GroundplanBackend.MapTypeToCategory`.
- The pure math (`Groundplan_GetBuildingState`/`GetWappenLabelId`/`RetZero`) stays in
  `src/world/groundplan_recon.cpp` (unchanged, no overlap; `gui::` vs `world::` namespaces).

---

## Wiring (rule 13)
Real callers of the cluster (xrefs verified):
- `VIBE_GameLogic_RunFrameLoop`@0x4c09a0 → BuildInfoPanel@0x4af4a8 (per-frame panel refresh).
- `VIBE_GameLogic_InitOrLoadSession`@0x533a54 / `VIBE_Menu_RunOptionsGame`@0x56cc44 →
  CreateWindow@0x4ae3b8.
- `VIBE_Cutscene_ExecMainFunc`/`ProcessTurnActions`/`InitOrLoadSession`/`Transition_FadeInScene`
  → FadeInScene@0x4b0758.
- RenderBlueprint@0x4af038 is called by BuildInfoPanel and calls LoadBlueprintBmp@0x4aea5c.

**Handoff:** the frame loop + session-init are bind-site files in `src/play`/`src/app` not
owned by this wave. The module exposes the 7 functions + `GroundplanState`/`GroundplanBackend`;
the one-line handoff at the frame-loop site is: construct a `GroundplanBackend` pointing at the
already-reconstructed window/object/surface/picture leaves (all present in src/gui, src/render,
src/sim) and a process-lifetime `GroundplanState`, then call `Groundplan_BuildInfoPanel` each
frame (and `CreateWindow`/`FadeInScene` at session entry). No new engine code required — every
backend member resolves to an existing reconstructed symbol.

---

## Tests / golden vectors
- **Unit** (`groundplan_test.cpp`, 63 checks, 0 failures): full `PickBlueprintName` table
  (every category + special case + fallback); `ComputeClockHands` (minute trunc + %60 wrap,
  hour %48, moon biased-rounding %48); SetWidgetsVisible / DestroyWidgets slot churn;
  CreateWindow wiring + 15-slot clear; BuildInfoPanel full-rebuild vs in-place refresh;
  RenderBlueprint dirty-rebuild vs clean-frame.
- **Guarded e2e** (`groundplan_blueprint_e2e_test.cpp`, 6 checks): loads a shipped
  `gfx/BMP/GroundPlans/Riss_Kirche.BMP` (real game dir / `GUILD_GAME_DIR`), decodes it with
  the reconstructed `render::BmpLoadBuffer` (104×110, 11440 px), then drives
  `Groundplan_LoadBlueprintBmp` over a backend backed by the decoded pixels — asserts the
  surface-copy scan visited all w×h pixels and the `_C.bmp` collision-map name derived
  correctly. Skips cleanly when the game dir is absent.

## Build status
`groundplan.cpp.o` compiles clean; `groundplan_test` + `groundplan_blueprint_e2e_test` build
and pass. NOTE: the full `guild` lib target currently fails on an **unrelated untracked file**
`src/script/script_vm.cpp` (another in-flight wave — a `goto`-into-scope error, `?? ` in git
status, never previously built). It is outside this wave's ownership; my object built before it.

---

## Reconstructed vs leaves
- **Reconstructed (7):** CreateWindow, SetWidgetsVisible, DestroyWidgets, LoadBlueprintBmp,
  RenderBlueprint, BuildInfoPanel, FadeInScene — plus the pure leaves PickBlueprintName +
  ComputeClockHands.
- **Genuine leaves reached (already reconstructed elsewhere, reused via backend):**
  Picture_CreateSurfaceFromBmp@0x422ae4, BmpLoadBuffer@0x5f0ce4, Building_MapTypeToCategory
  @0x5878b0, plus the window/object/surface/widget/form/animation/text engine leaves (window.cpp,
  object.cpp, form*.cpp, render/*). No new sub-leaf required reconstruction — the cluster's
  novelty is entirely the path-selection table, the pixel scan, and the clock math (all done).
