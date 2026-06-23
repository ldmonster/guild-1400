#pragma once
// wire_object — wires the object-record / scene-entity / move-universe / motion /
// object-record(save) bridge tables into their REAL reconstructed leaves (rule 13).
//
// SCOPE — five candidate bridges were surveyed (the "object spatial" cluster):
//   * ObjectSceneEntityHooks  (sim/object_scene_entity.h) — GameObject_FreeAllTables.
//   * MoveUniverseHooks       (sim/character_move.h)      — Move2UniverseActionUpdate.
//   * MotionHooks             (sim/charaction_motion.h)   — WalkOnPath motion executor.
//   * ObjectRecordHooks       (io/save_serial3.h)         — Save_WriteObjectRecord.
//   * the object-lifecycle family (sim/object_lifecycle*.h, the "LifecycleHooks"
//     referent) — the universe scene-node factory / attach chain.
//
// Of those, only TWO carry a leaf field with a genuine *pure-logic* reconstructed
// target (the rest are GPU/scene/audio leaves — rules 3/5 — or already wired):
//
//   ObjectSceneEntityHooks.releaseTables  ->  ResetEntityArrays (entity.h)
//       VIBE_GameObject_FreeAllTables @0x583ab0 releases the four global record
//       tables (g_persons/g_objects/scene-typedef/g_sceneNodes). The module's
//       inert DEFAULT already routes releaseTables to the REAL ResetEntityArrays
//       (the reimpl's table-release == a fresh unloaded game). This installer keeps
//       that real binding (seed-from-defaults) so FreeAllTables runs its real table
//       teardown; we re-affirm it rather than leave the bridge "uninstalled".
//
//   MoveUniverseHooks.switchActiveSlot    ->  UniverseSwitchActiveSlot (universe.h)
//       VIBE_Character_Move2UniverseActionUpdate @0x4063c8 calls the central scene
//       swap SwitchActiveSlot(Data0, /*quiet=*/1) twice (to the destination slot,
//       then back to the saved slot). VIBE_Universe_SwitchActiveSlot @0x5b4a24 is
//       reconstructed 1:1 in sim/universe.cpp (the slot record save/load swap; its
//       render leaves sit behind UniverseRenderHooks). With quiet==1 it performs
//       exactly the determinism-relevant slot swap and returns — the faithful target
//       of the action's switchActiveSlot leaf. Bound here.
//
// EVERYTHING ELSE stays inert / out of scope (documented in wire_object.cpp):
//   - MoveUniverseHooks.moveToUniverse / .setVisible — VIBE_Character_MoveToUniverse
//     (object reparent) / VIBE_Character_SetVisible are render/scene leaves with no
//     pure-logic reconstruction (the only SetVisible body in src/ is DefSetVisible,
//     an empty render stub). Inert.
//   - MotionHooks (attachAni/detachMorphAni/drawSubMeshes/freeWaypoints/footstep) —
//     ALL render/anim (rule 3) + audio (rule 5) leaves; the freeWaypoints inert
//     default (null the host-owned +244 buffer) is itself the faithful observable.
//     ZERO pure-logic fields -> NOT installed.
//   - ObjectRecordHooks.resolveStock — a raw pointer-deref probe (*((DWORD*)rec+34),
//     gated on the stock sub-record's +981 flag byte) into a slab owned by another
//     module; no reconstructed leaf resolves it. The inert default (hasStock=false ->
//     zero templates) is the deterministic byte-exact record. ZERO pure-logic fields
//     -> NOT installed.
//   - ObjectSceneEntityHooks.buildingFreeAndUnlink — the real VIBE_Building_FreeAndUnlink
//     @0x586d6c (sim/building3.cpp, Building3_FreeAndUnlink) unlinks the freed building
//     *pointer* from the per-person home/work building columns (dword_12CEA7C @+364 /
//     dword_12CEA80 @+368, 768 person slots, 134-dword stride). Those columns store
//     32-bit record POINTERS; on the 64-bit reimpl a building pointer cannot be stored
//     in / compared against the int32 columns faithfully (pointer-width gap), and the
//     FreeAllTables call site has no int32 index-array globals to hand it. Left inert
//     (the building SWEEP + the real table release in FreeAllTables still run).
//   - the object-lifecycle family — VIBE_Object_AttachToUniverseNode @0x5b3e30 and its
//     load->attach chain are ALREADY wired live by sim/object_attach_wiring.cpp
//     (InstallRealObjectAttachWiring binds CharacterFactoryHooks.attachToUniverseNode +
//     the ObjLife10Hooks chain). The remaining ObjLife2..9 hook surfaces are render /
//     scene-graph / anim / light / shadow / sound / memory-allocator leaves (rule 3)
//     owned by the lifecycle-wiring track; not re-wired here (no duplication).
namespace guild::sim {

// Install the real bindings into the object-spatial bridges that have a genuine
// pure-logic target (ObjectSceneEntityHooks, MoveUniverseHooks). SEED-FROM-DEFAULTS:
// each table is first copied from its module's current (inert/default) state so the
// fields with no clean target keep their safe behaviour (and the already-real
// releaseTables default is preserved); only the wireable leaves are overridden.
// Idempotent; the tables are process-lifetime storage the global hook pointer
// references. Composes with InstallRealObjectAttachWiring (the lifecycle/attach chain).
void InstallRealObjectWiring();

} // namespace guild::sim
