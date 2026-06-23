# Wave-21 — the per-frame ENTITY CORE (the simulation heart), wired

**Agent:** W21-ENTITYCORE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the per-frame world/entity SCENE-UPDATE pass and its dispatcher +
the per-record renderers, following the real call tree (rule 7) down to the
genuine GPU/scene leaves. This is the subtree wave-19/20 deferred as D2 (the
object/anim per-frame core); it is now reconstructed and wired.

## TL;DR

| addr | name | what it is | disposition |
|------|------|-----------|-------------|
| 0x41ceb4 | VIBE_DecompressGameState | per-frame SCENE-UPDATE walk (NOT a codec) | **reconstructed 1:1** → `SceneUpdatePass` |
| 0x40e50c | VIBE_Decompressor_Init | the 10240/20 result-handler table dispatch | **reconstructed 1:1** → `DecompressorInit` |
| 0x4139a8 | VIBE_GameLogic_Interactions | the OTHER per-record dispatch walk (frame loop) | **reconstructed 1:1** → `Interactions` (full switch body) |
| 0x41e814 | VIBE_Gfx_CrossFadeStep | the 0x44 cross-fade step | **reconstructed 1:1** → `GfxCrossFadeStep` |
| 0x40eea0 | VIBE_Object_Update | type 0x41 renderer | core + clamp recovered; render leaves hooked |
| 0x415b78 | VIBE_Animation_Apply | type 0x43 label renderer | word-wrap core (w19) + dispatch; render leaves hooked |
| 0x418f34 | VIBE_EntityChild_Process | type 0x40 child renderer | priority/scroll/percent/border-tile cores recovered; leaves hooked |
| 0x41078c | VIBE_Entity_InteractionLogic | type 0x45 fill-bar renderer | fill-fraction + percent cores (w19); render leaves hooked |

## New modules

- `src/sim/entity_frame_update.{h,cpp}` — `SceneUpdatePass` (0x41ceb4) +
  `DecompressorInit` (0x40e50c). Models the 740-byte render node (`RenderNode`),
  the 684-byte decompressor record (`DecompRecord`), the 238-stride entity record
  (`EntityRecord`) and the 20-byte handler-table entry (`DecompHandlerEntry`) by
  their exact binary byte offsets. The DDraw lock/unlock pair (0x423500/0x4235dc)
  is the rule-3 Vulkan boundary → `SceneFrameHooks::decompressStateBlob /
  decompressionFinalize`. Every other edge (the entity-table walk, the per-record
  type switch, the 16.16 `>>16` reads, the divider broadcasts, the 512-slot
  handler scan) is verbatim.
- `tests/unit/entity_frame_update_test.cpp` — 26 checks.

## Extended modules (mine)

- `src/sim/object_update.{h,cpp}` — wave-19 pure cores PLUS the rest of the
  fully-recovered renderer-body integer logic:
  - `ObjectUpdateClampWidth` (0x40eef4 — min-48 + optional `node[86]` cap).
  - `EntityChildScrollArrows` (0x419076 — the scroll up/down enable flags; the
    second comparison overwrites the first on node-235, node-234 keyed on step).
  - `EntityChildScrollPercent` (0x4190fd — `rangeTop/thumbPos + 0.5`, ConvertX
    truncate; dbl_610F14 = 0.5 verified via get_bytes).
  - `EntityChildBorderTiles` (0x41924a/0x4192c5 — the 9-slice `(dim-16)>>3` runs).
  - `+15` checks added to `object_update_test.cpp` (now 61 total).
- `src/play/gamelogic_recon.{h,cpp}` — the `Interactions()` (0x4139a8) per-record
  dispatch switch is now a FULL reconstruction (was structure-only). Added an
  `InteractionRecord` model (the 740-byte node layout), the walk loop (510-slot
  cap, the meshHandle/ctx52 skip gate), and the complete type switch routing:
    - `<5` : type 0 (no-op) / 1 (sprite block) / 4 (Physics_Update)
    - 5/8  : the animated-entity block (overlay state, base blit, +120 label,
             focused-node checkbox arm)
    - 9    : Widget_DrawScrollBar · 17 : countdown `*(rec+26)`
    - 0x40 : EntityChild_Process · 0x41 : Object_Update · 0x42 : Building_Update
    - 0x43 : State_Finalize + Animation_Apply (door/label; flag word 2|8|0x10|4)
    - 0x44 : **Gfx_CrossFadeStep** · 0x45 : Entity_AnimationUpdate +
             Entity_InteractionLogic · 0x47 : Widget_BlitClippedRows
    - trailing checkbox flush + font `_FONT` validate/state finalize.
  - `GfxCrossFadeStep` (0x41e814): alpha advance (+8), the `<=255` SetFadeParams
    path vs the `>255` row-copy loop bound (`<rec[5]`), the `>288` teardown gate.
    GPU leaves (SetFadeParams / 16-bit row copies / debug free) hooked.
  - New dispatch hooks on `IGameLogicHooks` (+ `RecordingGameLogicHooks` traces):
    `entityAnimationUpdate`, `entityInteractionLogic`, `physicsUpdateRec`,
    `widgetDrawScrollBar/ScrollThumb/Checkbox`, `widgetBlitClippedRows`,
    `gfxCrossFadeStep`, `stateGetCurrent4`, `stateFinalize`, `stateUpdateHandle`.
  - `+25` checks added to `gamelogic_recon_test.cpp` (now 84 total).

## What is reconstructed vs what is a genuine leaf (hook)

**Reconstructed 1:1** (control flow + constants + 16.16 math + dispatch order):
- the full `SceneUpdatePass` entity-table walk + the per-child-node type switch;
- `DecompressorInit`'s 512-slot owner-match dispatch;
- the full `Interactions` interaction-list walk + the per-record type switch;
- `GfxCrossFadeStep`'s step state machine;
- every fully-recovered renderer integer core (clamp / scroll flags / percent /
  border tiles / fill fraction / amount & percent formatting / word-wrap).

**Genuine leaves routed through hooks** (rule-8 boundary — NOT faked):
- the rule-3 DDraw→Vulkan back-buffer lock/unlock (0x423500 / 0x4235dc);
- the sprite/text raster leaves: `VIBE_Animation_Basic` (0x5d85b8),
  `_Advanced` (0x5d89bc), `_Apply` (0x415b78 emit half), `VIBE_Velocity_Apply`
  (0x5d883c), `VIBE_Shape_ShowFromBank*`, `VIBE_Coord_Push/Transform` (0x5d8ae8 /
  0x5d8b00), `VIBE_State_Update/Finalize/GetCurrent`, `VIBE_Property_Get/Set`,
  `VIBE_Result_Handler_*` blit, `VIBE_Render_DrawTexturedQuad`;
- the register-spilled float fragments in 0x41078c's render body: the live
  decompile shows several `VIBE_Coord_ConvertX()` calls whose `__usercall`
  register args were lost (the same gap `misc_recon4_entitybar.h` documented).
  Per rule 8 those exact operands are NOT invented — the recoverable fill-bar
  fraction + percent cores ARE reconstructed (wave-19 `ComputeBarFraction` /
  `FormatBarPercent`), and the unrecoverable float-clip fragments stay behind the
  renderer's leaf seam.

## Wiring (rule 13)

- **`Interactions` (0x4139a8) is in the live frame loop.** `play::RunFrameLoop`
  (gamelogic_recon.cpp, the faithful 0x4c09a0) already calls `Interactions(st, h)`
  at the kGameObjects gate (0x4c0f0d). With the inert hooks the interaction list
  is empty (the walk body runs zero times = headless-faithful); a host points
  `GameLogicState::interactionList` at the live `dword_62D26C[]` and the full
  dispatch runs. The dispatch leaves resolve through `IGameLogicHooks`.
- **`GfxCrossFadeStep` is wired as the 0x44 dispatch target** inside `Interactions`
  (`h.gfxCrossFadeStep(...)`), matching the original's `VIBE_Gfx_CrossFadeStep`
  call site at 0x413e39.
- **The object_update renderer cores** are reachable for the renderer leaves the
  scene-update / Interactions walks dispatch (Object_Update / EntityChild_Process
  / Entity_InteractionLogic), as the byte-exact decision/format/layout cores.

### One-line handoff (file not owned by this agent)

`SceneUpdatePass` (0x41ceb4) is the GUI **form-redraw** variant of the walk; its
live callers are `VIBE_Form_RefreshIfVisible` / `VIBE_Book_Open` /
`VIBE_Book_RefreshVisiblePages` / `VIBE_Text_RenderRichString` (bind-site files
this agent does not own). To put it in the live tree those callers invoke
`guild::sim::SceneUpdatePass(rec, entityTable, …, sceneHooks)` where `rec` is
`dword_676A60[684*formId]`; the `SceneFrameHooks` adapter forwards to the same
render leaves the frame loop's `Interactions` uses. Documented for the form/book
module owner.

## Verification

`build/` green. `libguild.a` links with the two new modules (ODR-clean: my
740-byte `guild::sim::RenderNode` is renamed off the existing 67-byte
`guild::sim::SceneNode` in `sim/types.h`; `guild::play::GfxCrossFadeStep` is a
distinct namespace + signature from the unrelated `guild::sim::GfxCrossFadeStep`
in `command_leaves.h`). Tests:

- `entity_frame_update_test` — 26 checks (gate bail, lock/unlock bracket, body
  reinit, type dispatch 1/0x41/0x42/0x43/0x45, type-17 countdown + freeze,
  empty-mesh skip, divider broadcasts, DecompressorInit bound/unbound/cap).
- `object_update_test` — 61 checks (44 wave-19 + 15 wave-21 + 2 prior).
- `gamelogic_recon_test` — 84 checks (incl. 25 new: Interactions dispatch per
  type, skip gate, type-17 countdown, CrossFade null/advance/overflow/teardown).
- e2e goldens unchanged: `playable_flow_e2e_test` (154), `real_scene_driver_e2e_test`
  (0), `real_scene_driver_test` (17), `playable_flow` — all 0 failures, no shift.

## Build note (not my files)

`build/libguild.a` was RED at wave start from a **stale/truncated object file**
(`ai_eval.cpp.o: file truncated`) left by a prior interrupted build — unrelated to
this wave. Deleting the stale `.o` + the partial archive and rebuilding restored a
clean link; no source change was needed. The six files this wave touched are
ODR-clean (verified: combined-include of `sim/types.h` + `sim/entity_frame_update.h`
compiles; `RenderNode`/`SceneNode` coexist).
