# Wave-20 — Building-interior entry + room select + vegetation + combat mesh

**Agent:** W20-SCENE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the "step into a building / room" gate reachable from the frame loop
(root 0x4c09a0), plus the vegetation model-load leaf and the combat object-mesh
attach. New owned modules: `src/play/scene_interior.{h,cpp}`,
`src/sim/vegetation.{h,cpp}`, tests `tests/unit/scene_interior_test.cpp` +
`tests/unit/vegetation_test.cpp`.

---

## Reconstructed functions (1:1 from the decompile + disasm)

| addr | name | where | status |
|------|------|-------|--------|
| 0x56ec14 | VIBE_Plant_LoadVegetationModel | sim/vegetation.cpp | **full** (path-select + level + load-trigger + place + light-cache + error) |
| 0x5878b0 | VIBE_Building_MapTypeToCategory | (reused) | **REUSED** — delegates to `guild::sim::Building_MapKindToCategory` (building_type.cpp); not redefined |
| 0x587f80 | VIBE_Building_IsProductionType | (reused) | **REUSED** — delegates to `guild::sim::Building_IsProductionKind` (building_type.cpp); not redefined |
| 0x51db4c | VIBE_Building_SelectRoomToEnter | play/scene_interior.cpp | **full** (pick + transition, hooks for side effects) |
| 0x4adef4 | VIBE_Dialog_OpenBuildingForActiveChar | play/scene_interior.cpp | **gate core** (enter-eligibility predicate; orchestration documented as handoff) |
| 0x5066b8 | VIBE_Scene_EnterBuildingInterior | play/scene_interior.cpp | **decision core** (season clamp, production-decor, plant-trigger, char-show cap) |
| 0x485e88 | VIBE_Combat_AttachObjectMesh | play/scene_interior.cpp | **full** (name-build + equal/differ decision + attach sequence) |

### Decompile findings (the load-bearing constants — golden-pinned)

* **Vegetation level** (0x56ec34/0x56eecc): `level = (stagePacked >> 24) / species[+0x43]`
  via **signed `idiv`** (truncates toward zero), then `if (level < 1) level = 1`.
  The `>> 24` is an arithmetic shift, so a 0xFF high byte yields -1 → clamped to 1.
* **Vegetation path** (0x625334 / 0x625354): two format strings, selected by the
  `edx` autumn flag the original tests at 0x56ec2c:
  * plain: `"%svegetation/*pfl_%s_0%i.ogr"`
  * fall:  `"%svegetation/*pfl_%s_0%i_FALL.ogr"`
  args = `(byte_122F098 dataDir, speciesRecord+2 name, level)`. The `*` is the
  in-archive wildcard selector the object-group loader consumes.
* **Vegetation placement** (0x56ece8..0x56eeb4): if `dword_64A028` (placement-ref
  object) exists, world pos = base `ref[+0x90/0x94/0x98]` + `plotX·ref[+0xA0/A4/A8]`
  + `plotY·ref[+0xB0/B4/B8]` + `mapByte·ref[+0xC0/C4/C8]`, where
  `mapByte = *(u8*)(ref[0]·plotY + plotX + ref[+0x10])` (the SAME byte feeds all
  three +0xCx terms). Plot coords are zero-extended then read back as signed i16
  (`fild word`); for 0..255 inputs i16 == value. Then `SetPosition` + the
  `"_ID%i"` name suffix append + `node[+0x218] = 1` (vegetation flag) +
  `Light_RequestObjectCache`. Failure logs
  `"init_SetPflanzenMap(): Could not load 3D-Objekt-roup '%s'..."`.
* **MapTypeToCategory** (0x5878b0): exact 9-way switch → {0,1,2,3,4,5,6,7,8}.
* **IsProductionType** (0x587f80): category ∈ {11,12,13,16,28}.
* **SelectRoomToEnter** (0x51db4c): `a1==-1` → write `building+0x29 := (i16)a2`
  directly; else `QueryFind(building+0x5D, 1, 4, 2)` returns first match (count in
  ecx) and the iterator is walked to the count-th match (or the last reachable),
  whose id word is written to `building+0x29`. Both paths then run the transition:
  `dword_631614 = dword_631610-dword_631618+1`, camera SetPosition/SetWorldTranslation,
  clear `dword_6316CC/6316D0`, and `Audio_StartVoiceSample(bank,63,1,vol,127,0,0)`
  (the `"RaumWechseln_s"` voice).
* **OpenBuildingForActiveChar** (0x4adef4): busy-gate `dword_11BC27C`; active-char
  enter eligibility = category 6 (always) OR category 4 with `building+0x65 ==
  dword_12CE914[134*char]` (the char's owned-building id); else abort. char row
  liveness via `byte_12CEAC1[535*char]`. (The post-gate dispatch — the room walk,
  the `dword_63174C` current-building swap, `Scene_LoadObjektScene`,
  `Hud_FindModeIndex(Scene_RunMainFrameLoop)` — is global-state orchestration
  documented as a handoff below; the eligibility predicate is the reconstructed
  rule.)
* **EnterBuildingInterior** (0x5066b8): season write gate `season <= 3` →
  `byte_634484`; production buildings hide foliage decor + upgrade scaffold;
  building type 30 entered with room filter 253 → `Plant_EnsureModelsLoaded`;
  interior char-show cap = 4 if the interior mode byte (table[589*type]) == 1 else 8.
* **AttachObjectMesh** (0x485e88): `name = "ob_" + UPPER(defName)`; replace slot-2
  item iff current is empty OR differs case-insensitively (`StrCmpNoCase`); replace
  = detach-if-present then attach; no def / zero id = detach-only; no actor
  (unit+0x184==0) = no-op.

---

## Reused (extern / hooks — no ODR redefinition)

Grepped `src/**` first. The following already exist and are NOT redefined — the
new code routes through the established hook pattern instead of re-implementing:

* `guild::sim::Plant_EnsureModelsLoaded/HideAllModels/AdvanceGrowthStage`
  (`src/sim/plant.cpp`) — the growth passes; `IPlantWorld::LoadModel` previously
  **deferred** the model load. `sim/vegetation.cpp` now provides that body.
* `VIBE_Util_StrToUpper` / `StrCmpNoCase` (`src/util/string_ops.cpp`),
  `VIBE_Character_AttachItemToBone` (`src/sim/character_render3.cpp`),
  `VIBE_Scene_LoadObjectGroup` (`src/io/vfs_recon5_worldio.cpp`),
  `VIBE_Light_RequestObjectCache` / `VIBE_Object_SetPosition` /
  `VIBE_Universe_RestoreObjectStates` — referenced via hooks, not redefined.
* `guild::sim::Building_MapKindToCategory` (0x5878b0) +
  `Building_IsProductionKind` (0x587f80) in `src/sim/building_type.cpp` — the
  canonical reconstructions; `play::Building_MapTypeToCategory` /
  `IsProductionType` are thin accessors that delegate to them (no duplicate body,
  no ODR clash).
* `VIBE_Combat_FindObjectDef` decision already in `src/sim/combat_slots3.h`
  (FindObjectDef) — `AttachObjectMesh` consumes the *resolved* def name, so it does
  not duplicate the ring walk.

---

## Wiring (rule 13) — handoffs

Owned modules expose callable, tested functions. The bind sites are files owned by
other waves; per the ownership rule they are documented, not edited:

1. **vegetation → plant.** `sim/plant.cpp`'s `IPlantWorld::LoadModel(PlantRec*)`
   is the deferred seam. To go live, its backend should fill a `VegPlantView`
   from the PlantRec (`recId=rec[0]`, `plotX=rec+8`, `plotY=rec+9`,
   `stagePacked=rec+0x0A`, `typeId=HIWORD(rec[2])`), install `VegetationHooks`
   (dataDir=byte_122F098, findSpecies=Amt_FindOfficeTypeRecord, loadObjectGroup=
   Scene_LoadObjectGroup, setPosition/lightRequestCache/reportError), call
   `guild::sim::Plant_LoadVegetationModel(view)`, and store the returned node into
   `rec[5]`. `Plant_EnsureModelsLoaded` (0x56ef2c) already calls LoadModel then
   `RestoreObjectStates(model,1)`.
2. **SelectRoomToEnter** — real callers `VIBE_Building_EnterAndDispatch @0x51defc`,
   `EnterForeignShop @0x51e88c`, `EnterScriptedLocation @0x51ed98` (building-enter
   bind sites). They install `SceneInteriorHooks` (writeBuildingRoom, the camera
   resets, the voice) and call `Building_SelectRoomToEnter`.
3. **EnterBuildingInterior / OpenBuildingForActiveChar / AttachObjectMesh** —
   callers: `Hud_UpdateSelectedObjectContact @0x50ee60`, `Command_ExSysMessage`,
   `Building_HandleSelectionClick @0x50ed14`, `Hotkey_ActivateBuilding @0x4feec4`,
   `Combat_SpawnUnit @0x489430`, `Command_ExEquipCombatObject @0x49b65c`. They feed
   the decision cores the resolved table reads (category byte, char liveness, owner
   match, def name, existing item name) and run the side effects through the hooks.

---

## Tests (golden-pinned)

* `tests/unit/scene_interior_test.cpp` — suite `SCI`: full MapTypeToCategory case
  table; IsProductionType over all 256 bytes; room-pick (direct / empty / count==1 /
  walk-to-count-th / clamp); the wired transition fires only when found; the
  enter-eligibility gate (busy / no-char / dead-row / cat6 / cat4-owner / other);
  the EnterBuildingInterior decision core (season clamp 0..3, production decor,
  plant trigger type-30+253, char cap 4/8); AttachObjectMesh (no-actor / no-def /
  replace upper-name / keep-no-case / replace-differs / wired detach-then-attach).
* `tests/unit/vegetation_test.cpp` — suite `VEG`: level computation (idiv trunc,
  floor-at-1, negative high byte, 0/divisor); exact path format plain + fall +
  `_0%i`; full load orchestration (success path string + suffix + flag + light +
  position; fall suffix; failure error string; placement basis math); **guarded
  e2e** building a real path against `GUILD_GAME_DIR`.
* **Result:** 364 checks, 0 failures (with GUILD_GAME_DIR set; 362 without).

### e2e note (rule 8)
Vegetation models ship inside the packed archives (`A_Obj.dat`), not loose files;
the path-select yields the canonical `…/vegetation/*pfl_<name>_0<level>.ogr`
in-archive selector. A full model-load e2e hands off to the archive-mounted
`Scene_LoadObjectGroup` deserializer (other module); the guarded e2e here proves
the path string the loader consumes is built correctly against the real data dir.

---

## Build status

`src/sim/vegetation.cpp`, `src/play/scene_interior.cpp` and both tests compile
clean (`-std=c++17`) and all 364 checks pass when linked in isolation. The normal
`build/` target currently fails to link the whole `guild` lib due to a **pre-
existing, untracked** file from a concurrent wave (`src/sim/command_leaves.cpp`
references `RunActionOrFree` without including `sim/actionqueue.h`) — outside this
agent's ownership, not introduced here.
