# Wave-19 — Object / Character / Anim per-frame core (W19-OBJANIM)

**Agent:** W19-OBJANIM · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the object/entity/animation per-frame core from the coverage-audit
"Object/Character/Anim per-frame core" cluster, following the real call tree
(rule 7) down to genuine leaves. New modules:

- `src/render/anim_object.{h,cpp}` — the object/skeletal anim attach + stock cluster
- `src/sim/object_update.{h,cpp}` — the per-frame entity renderers' pure logic
- `tests/unit/anim_object_test.cpp` (53 checks) + `tests/unit/object_update_test.cpp` (44 checks)

All 97 checks pass through the real cmake build (`anim_object_test`,
`object_update_test` both build exit 0 and run 0 failures).

---

## Reconstructed (1:1 from the decompile)

### Anim cluster — `src/render/anim_object.{h,cpp}`

| addr | name | status |
|------|------|--------|
| 0x5cef14 | VIBE_Anim_CreateObjectAnim | **reconstructed** — class-3/4 gate, free-old, keyframe qmemcpy, per-frame `*3` duration fixup + last-frame[0]=prev-frame[0] fixup, pose snapshot (node+76..140), looped vs non-looped frame-index init (AdvanceFrameIndex), BuildFrameTangents call, frame-counter stamp |
| 0x5d0b64 | VIBE_Anim_AttachToBone | **reconstructed** — free-channel scan (<3), stream lookup + mesh-type match gate, channel init (blend=1.0f bits, weight=100, markFrame=-1, flag bytes 109/110), refcount++, one-time CalculateAnimNormals, loop/non-loop frame-index, AssignSubMeshBones + ComputeBoneMatrices |
| 0x5d0d38 | VIBE_Anim_PruneExpiredAttachments | **reconstructed** — case-SENSITIVE name match drop, refcount--, boneActive recompute (clear when all 3 empty), AssignSubMeshBones + ComputeBoneMatrices |
| 0x5cec00 | VIBE_Anim_FreeObjAnimData | **reconstructed** — free rec+56 then rec, clear node+464 |
| 0x5cf114 | VIBE_Anim_FindFreeMeshSlot | **reconstructed** — stock-list walk, empty-list→null, first case-INSENSITIVE name match (StrCmpNoCase thunk) |
| 0x5d3858 | VIBE_Anim_LoadStreamToStock | **reconstructed** — "animations/" prefix build, in-stock dedup (logs dup), .baf load via model-IO hook, list-prepend |

### Per-frame entity renderers — `src/sim/object_update.{h,cpp}`

The four big functions (0x40eea0 / 0x415b78 / 0x418f34 / 0x41078c) are deeply
render-state-coupled (the 740-byte scene-node array `dword_69FFB4`, the clip-rect
globals `dword_64A1B4..C0`, the Coord_Push/Transform/ConvertX + Animation_Basic/
Advanced/Apply + Property_Get/Set + State_* render leaves). The **pure logic** of
each — the parts that are NOT GPU/render-leaf calls — is reconstructed 1:1 and
golden-pinned; the render-leaf emission is the renderer's job (the established
hooks pattern used across the tree, e.g. object_throwbomb / gamelogic_recon):

| addr | name | pure logic reconstructed 1:1 |
|------|------|------------------------------|
| 0x40eea0 | VIBE_Object_Update | `CountDigits` (0x40f044 digit loop, cap 10), `FormatAmount` (g/kg/t via dbl_610D74=0.001 / dbl_610D6C=1e-6, zero-pad `%%0%ii `, plain `%i`), `FormatBuildPercent` (dbl_610D7C=100·fill, clamp >1.0, ConvertX truncate, `%i%%`) |
| 0x415b78 | VIBE_Animation_Apply | `WrapText` — the word-wrap layout: greedy line packing into the 96-byte-strided line buffer, space breaks, single-word `-`/`~` break-back, `~` hard break (dropped, not drawn), accumulated content height return |
| 0x418f34 | VIBE_EntityChild_Process | `ChildPriorityClass` (0x418fe4 modulo 8/6/4/2 → +608) |
| 0x41078c | VIBE_Entity_InteractionLogic | `ComputeBarFraction` (0x41089e fill fraction `max/(full-empty)` with the >0 && <1.0-bits snap-to-1px), `FormatBarPercent` (0x41186d `cur·100/full + 0.5`, ConvertX truncate, `%i%%`) |

### Genuine leaves (verified + REUSED — no ODR redefinition)

- **0x5cb8f0 VIBE_Util_StrCmpNoCase** — already reconstructed in `src/util/string_ops.cpp` (`guild::util::StrCmpNoCase`); reused via header alias.
- **0x5ccf18 VIBE_Anim_AdvanceFrameIndex** — already reconstructed in `src/render/skeleton.cpp` (same `guild::render` namespace); reused via `render/skeleton.h` (the frame loop/ping/clamp state machine — pinned by AnimObject.AdvanceFrameIndex* tests).
- **0x5c6b08 VIBE_Coord_ConvertX** — already reconstructed in `src/util/coord.cpp` (`guild::util::ConvertX` = `std::trunc`, truncate-toward-zero); reused (`ConvertXTrunc` is an inline alias). Verified the x87 chop semantics from the disasm (`fstcw`/HIBYTE=31→RC=11 chop/`frndint`/restore).

### Float/double tables (get_bytes-verified, in `anim_const`)

dbl_610DFC=2.0, dbl_610E04=-1.0, dbl_610E0C=-193.0, dbl_610E14=219.0,
dbl_610E1C=-135.5, flt_610E24=24.0, flt_610E28=196.0, flt_610E2C=220.0,
flt_610E30=100.0, dbl_610E34/610D84/610F14=0.5, dbl_610D6C=1e-6 (t),
dbl_610D74=0.001 (kg), dbl_610D7C=100.0.

---

## Already-reconstructed assigned targets (REUSE, did not redefine)

The brief also listed these; the coverage check found they already have bodies —
**not touched** (rule: grep first, reuse, no ODR):

- **0x5b2c70 VIBE_Object_AssignMeshData** → `ObjectAssignMeshData` in `src/sim/object_lifecycle5.cpp:408`.
- **0x40e818 VIBE_Object_Reinitialize** → `ObjectReinitialize` in `src/sim/object_lifecycle7.cpp:71`.

---

## Wiring (rule 13)

The live caller is **gilde.exe 0x4139a8 VIBE_GameLogic_Interactions** (the frame
loop's interaction walk), reconstructed as `Interactions()` in
`src/play/gamelogic_recon.cpp`. Its per-record type-switch dispatches:

```
case 0x40 -> EntityChild_Process       (0x418f34)
case 0x41 -> Object_Update             (0x40eea0)
case 0x43 -> State_Finalize + Animation_Apply (0x415b78)
case 0x44 -> Entity_AnimationUpdate + Entity_InteractionLogic (0x41078c)
```

`gamelogic_recon.h`'s `IGameLogicHooks` already exposes the dispatch hook points
`objectUpdate()` @0x40eea0 and `entityChildProcess()` @0x418f34 (currently inert
defaults). **Handoff (one line):** wire those hook overrides — and the case-0x43/
0x44 branches — to call `guild::sim::FormatAmount/FormatBuildPercent/WrapText/...`
+ the render leaves; `gamelogic_recon.{h,cpp}` is a bind-site file this agent does
not own, so the integration is left to its owner. The pure-logic units this wave
provides are the byte-exact decision/format/layout cores those bodies need.

The anim cluster is wired to its live callers through the existing scene/anim hook
points: `VIBE_Anim_CreateObjectAnim` is the install path used by
`object_throwbomb` (already references it via `createObjectAnim` hook) and the
camera-flight / object MoveObject tracks; `LoadStreamToStock`/`AttachToBone`/
`PruneExpiredAttachments` are the per-character bone-animation lifecycle the
`character_render*` modules describe. `SetAnimObjHooks` injects the mesh/IO edges.

---

## Tests

- `tests/unit/anim_object_test.cpp` — 53 checks: StrCmpNoCase fold, AdvanceFrameIndex (normal/ping/clamp/bounce/wrap), FindFreeMeshSlot empty+match, LoadStreamToStock prefix+dedup, CreateObjectAnim class-gate + `*3` + last-frame fixup + pose snapshot + frame-index + stamp, AttachToBone+Prune refcount/boneActive, 3-channel slot limit.
- `tests/unit/object_update_test.cpp` — 44 checks: ConvertX truncation (incl. negative toward-zero), CountDigits cap, FormatAmount g/kg/t/pad/plain, FormatBuildPercent clamp+trunc, ComputeBarFraction + <1px snap, FormatBarPercent +0.5 trunc, ChildPriorityClass 8/6/4/2 order, WrapText basic/hard-break/single-line.

Both pass via the real cmake build (`build/anim_object_test`, `build/object_update_test`).

---

## Build note

The shared `libguild` was already RED at wave start due to two **untracked,
in-progress files from other agents** — `src/play/session_npc_daily.cpp`
(`CityView3D::BoundPerson` mismatch) and `src/script/script_vm.cpp` (`kOff_*` /
`Dword` undeclared). Neither is owned or touched by this wave. With those two
unrelated WIP files set aside, libguild + both of this wave's test targets build
clean (exit 0). The six files this wave added are all new and ODR-clean
(verified: no symbol of theirs collides with an existing same-namespace
definition; StrCmpNoCase/AdvanceFrameIndex/ConvertX are reused, not redefined).
