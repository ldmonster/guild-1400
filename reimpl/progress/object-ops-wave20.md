# Wave-20 — Object/scene-graph transform + valuation ops (W20-OBJECT2)

**Agent:** W20-OBJECT2 · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Assigned the object/scene-graph transform + valuation cluster from the wave-18
coverage audit. Per rule "grep src/** FIRST, reuse, no ODR": **6 of the 7
assigned functions were already faithfully reconstructed** by earlier waves and
are reused, not redefined. The two genuine gaps are reconstructed here in new
modules, plus the bone-chain/parent-transform math is golden-pinned.

New modules:
- `src/sim/object_value.{h,cpp}` — VIBE_Object_ComputeMarketValue (the appraisal)
- `src/render/object_transform_ops.{h,cpp}` — VIBE_SceneGraph_RemoveMeshFromTree
  (the top-level mesh-removal driver) + transform-math golden re-exports
- `tests/unit/object_value_test.cpp` (16 checks)
- `tests/unit/object_transform_ops_test.cpp` (31 checks)

All 47 checks pass (compiled with the real build's flags + the shared
`test_main.cpp`; linked against the tree's existing scene_walk / util / mem
bodies).

---

## Reconstructed this wave (1:1 from the decompile)

### `src/sim/object_value.cpp`

| addr | name | status |
|------|------|--------|
| 0x594df0 | VIBE_Object_ComputeMarketValue | **reconstructed** — the building/estate appraisal. The exact x87 accumulation order, the 6-double constant table, the integer index math, the per-worker dedup/averaging, and the matching-owner +0.3 bonus are reproduced from the disasm (cross-checked instruction-by-instruction at 0x594df0..0x594fb7). The record-walking leaves are routed through `MarketValueHooks` (each onto an already-reconstructed body) so the appraisal math is byte-exact and golden-pinnable in isolation — the established hooks pattern (object_lifecycle5/9; gui_dialogs8 already declares this as a `objectComputeMarketValue` hook slot). |

**Formula (verified, float spills modeled where the original narrows on store):**
```
v17 = (double)SumWorkstationByCategory(obj,1,1) * 0.01 + 1.0
v14 = (kind==6||7) ? (double)activeSize * (1/252) * 0.25 : 0.0
v18 = v17 + v14
for id in ids[0..count): id!=-1 && FindRecordById(id):
    v22 += ComputeOutputRatio(rec)
    if ownerCategory(v21) == (workerCat = *(int*)(rec+354)>>24): v23 += 0.3
    ++matched
if matched: inv=1/matched; v15=v22*inv; v23=v23*inv
v19 = (AverageObjectFavorability(obj+39,count,ids) + (-0.5)) * 0.25 + v18
v20 = (double)(objLevel - 2) * 0.1 + v19
return (v15 + v23) * v20
```
where `v21 = 589 * *(char*)obj + dword_13CE294` (this object's building sale rec).

**Constants (get_global_value-verified IEEE-754 doubles @0x626B34..0x626B5C):**
`dbl_626B34=0.01`, `dbl_626B3C=0.003968253968253968 (1/252)`, `dbl_626B44=0.25`,
`dbl_626B4C=-0.5`, `dbl_626B54=0.1`, `dbl_626B5C=0.3`.

### `src/render/object_transform_ops.cpp`

| addr | name | status |
|------|------|--------|
| 0x5f0b18 | VIBE_SceneGraph_RemoveMeshFromTree | **reconstructed** — the top-level mesh-removal driver. `if (cell && node) node=WalkAndInvoke(off_649D64, node, RemoveMeshRecursive, *(u16*)(cell+68)\|0x280, cell); return (char)node;`. Drives a gated pre-order walk of `node`'s subtree via the existing `render::WalkAndInvoke`; each visited node's mesh is unlinked from the region cell via the existing `render::RemoveMeshRecursive` (0x5f0a74). The walk mask = region-cell +0x44 field `| 0x280`. This was the function `scene_walk.h` explicitly deferred ("region-node block whose layout is not yet mapped … threading the cell pools through the fixed-ABI callback would require speculation"); resolved here by passing the region mask as a parameter and threading the pools through a thread-local call context (no scene_walk.h edit; ODR-clean). |

Also re-exposes (thin reuse, no redefinition) the TransformPointToParent affine
math for golden pinning: `PointThroughBoneChain` (→`util::PointThroughBoneChain`),
`TransformPointPassThrough` (0x5b7d14 no-parent branch), `TransformPointByInverseWorld`
(0x5b7dce `point * inverse(parentWorld)` row layout).

---

## Already-reconstructed assigned targets (REUSED — did NOT redefine, no ODR)

The brief listed 7 functions; 6 already have faithful bodies. Verified each
against a fresh decompile this wave (control flow, offsets, constants all match):

| addr | name | existing body | verdict |
|------|------|---------------|---------|
| 0x5b7e54 | VIBE_Object_ReparentWithTransform | `src/sim/object_lifecycle5.cpp:294` `ObjectReparentWithTransform` | faithful (cycle test, sub-bone snapshot/restore, SetWorldTranslation ordering, relink via SetParent/LinkIntoScene) |
| 0x5b2710 | VIBE_Object_ChangeTransparency | `src/sim/object_lifecycle5.cpp:495` `ObjectChangeTransparency` | faithful (apply/restore branches, the `(color&0xFF)!=0xFF \|\| color&0x10000` gate, the 8-byte dedup map alloc, CloneIfPaletteMatch/DetachClone swap, ApplyVertexShading) |
| 0x5b7d14 | VIBE_Object_TransformPointToParent | `src/sim/object_lifecycle5.cpp:202` `ObjectTransformPointToParent` | faithful (parent-bone inverse-world affine, MatrixCopy/TransformVectors/ToEuler chain; no-parent pass-through) |
| 0x5b4274 | VIBE_Object_ToggleSuspendStateNamed | `src/sim/object_lifecycle5.cpp:642` `ObjectToggleSuspendStateNamed` | faithful (enable&&state1 restore, the '!'-prefix name-shift via 64-byte scratch, state 5/6 → RefreshAllObjects, 533/534 swap) |
| 0x506b68 | VIBE_Object_UpdateBuildingVisualState | `src/sim/object_lifecycle9.cpp:593` `ObjectUpdateBuildingVisualState` | faithful (the +7280 flag state machine over `dword_649D60`/`byte_123351A`, mip-filter level + SetLowNibbleFlag emission) |
| 0x5f0a74 | VIBE_SceneGraph_RemoveMeshRecursive | `src/render/scene_walk.cpp:253` `RemoveMeshRecursive` | faithful (list unlink, refcount--, octant recursion + emptied-child free, +531 bit2 clear) — the callee of my new 0x5f0b18 wrapper |

### Genuine leaves of ComputeMarketValue (verified present, routed via hooks)

All already reconstructed elsewhere; ComputeMarketValue reaches them through
`MarketValueHooks` (the real wiring binds these):

- 0x5904fc `VIBE_Building_SumWorkstationByCategory` → `Building_SumWorkstationByCategory` (`src/sim/building_stock.cpp:248`)
- 0x5920b0 `VIBE_Person_FindActiveByEntity` → `PersonFindActiveByEntity` (`src/sim/person_personnel2.cpp`)
- 0x58bc6c `VIBE_Person_FindRecordById` → present (`src/io/save_recon3_characters.*`)
- 0x57d384 `VIBE_Building_ComputeOutputRatio` → `Building_ComputeOutputRatio` (`src/sim/building_value.cpp`)
- 0x594928 `VIBE_Ai_AverageObjectFavorability` → `AverageObjectFavorability` (`src/ai/favorability.h`, `guild::ai`)

### Transform-math leaves of the reparent path (reused, `guild::util`)

- 0x5c8b38 `PointThroughBoneChain` (`src/util/transform.cpp`)
- 0x5cac3c `MatrixInverse`, 0x5cabf0 `MatrixCopy`, 0x5caaa4 `MatrixTransformVectors`,
  0x5cb2cc `MatrixToEuler` (all `src/util/matrix.cpp`; `MatrixToEuler` write-target
  offsets cross-checked against the disasm: euler triple at edx+0/+4/+8)
- 0x5f5701 `Atan2` (`src/util/math_trig.cpp`)

---

## Wiring (rule 13)

- **ComputeMarketValue:** the live caller is the GUI tooltip/owner-estate panel —
  `gui_dialogs8` already declares the hook slot
  `float (*objectComputeMarketValue)(const char* p, int n, const int* ids)`
  (`src/gui/gui_dialogs8.h:151`), currently bound to a `0.0f` default
  (`DefObjectComputeMarketValue`, `gui_dialogs8.cpp:126`). **Handoff (one line):**
  point that hook slot at `guild::sim::ObjectComputeMarketValueDefault` (matching
  signature) after `SetMarketValueHooks(...)` installs the record accessors;
  `gui_dialogs8.cpp` is a bind-site file this wave does not own, so the binding is
  left to its owner. The byte-exact appraisal core is now available.
- **RemoveMeshFromTree:** the live callers are the character/object teardown paths
  (`src/sim/character_render2.cpp:436/453` already describe
  `if (dword_634488) RemoveMeshFromTree(list[k+1], dword_634488)` — removal of a
  mesh from the active octree tree). `render::RemoveMeshFromTree` is now the real
  driver they can call (it composes the already-wired `WalkAndInvoke` +
  `RemoveMeshRecursive` over the region cell + pools). character_render2 is a
  bind-site file not owned here; the one-line handoff is to route its
  `RemoveMeshFromTree(node, tree)` hook through this driver.

---

## Tests

- `tests/unit/object_value_test.cpp` — 16 checks: base term (no workers), single
  matching worker (size + level + favorability + match bonus), mixed
  matching/non-matching/missing/empty-slot workers, template-fallback path (no
  active person), favorability-shifts-value, the constant-table bit patterns
  (1/252 etc.), and the float-narrowing default entry. Each value cross-checked
  against an independent double-precision reference that models the float spills.
- `tests/unit/object_transform_ops_test.cpp` — 31 checks: RemoveMeshFromTree null
  guards (no side effect), single type-1 node removal, parent→child descent
  removal, type-filtered no-op (type 3 not in mask 0x280), and the transform math
  (pass-through identity euler, inverse-world identity+translation, inverse-world
  rotation row-pick).

Both built with the real build flags (`-I include/src/shim/tests/framework
-std=c++17 -Wall -Wextra`) + the shared `test_main.cpp` and pass 0 failures.

---

## Build note

The shared `libguild` was RED at wave start due to **untracked/modified
in-progress files from OTHER agents** — `src/sim/he_messaging.cpp`
(`reinterpret_cast<i32>(const void*)` precision errors), `src/sim/command_leaves.cpp`
(`RunActionOrFree` undeclared), and `src/app/wiring.cpp`
(`scriptCharCmdResult_` undeclared). None are owned or touched by this wave. The
6 files this wave added are all new and ODR-clean: their symbols
(`guild::sim::ObjectComputeMarketValue*`, `guild::render::RemoveMeshFromTree` and
the transform re-export wrappers) collide with no existing same-namespace
definition; the matrix/transform/scene-walk/leaf bodies are reused (extern via
their headers), never redefined. Both source files compile clean against libguild's
flags, and both test executables link (against the existing scene_walk/util/mem
bodies) and run 0 failures.
