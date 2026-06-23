#pragma once
// wire_inventory — wires the inventory / build-plot / command-target / command-emit
// bridge tables into their REAL reconstructed cross-cluster leaves (rule 13). Before
// this, all five of these bridges were fully inert at runtime: nothing in the live
// call tree ever called their Set*Hooks installers (only the per-module .cpp default
// definitions and the unit/e2e tests did), so every function those bridges drive ran
// against the inert defaults (resolve "absent", count 0, capacity 0, every emit a
// no-op).
//
// This installer binds, for each bridge, exactly the leaf fields that have a genuine
// reconstructed callable target with a byte-faithful adapter — the established
// real-wiring pattern (cf. sim/wire_charaction.cpp / sim/real_hooks3.cpp). It is a
// SEED-FROM-DEFAULTS installer: each table is first copied from its module inert
// defaults (Get*Hooks()) and only the wireable fields are overridden, so the unbound
// fields keep their safe inert stubs (several apply/check call sites invoke hooks
// without a null-check).
//
//   * VIBE_GameObject_ResolveEntityById @0x583b44 -> GameObjectResolveEntityById (entity.h)
//   * VIBE_Person_FindRecordById        @0x58bc6c -> PersonFindRecordById      (entity.h)
//   * VIBE_GameObject_CountAtLocation   @0x58f1f4 -> GameObjectCountAtLocation  (object.h)
//   * VIBE_BuildingValue_ComputeRoomWorth @0x59116c -> BuildingValue_ComputeRoomWorth
//   * VIBE_Map_RasterizeBauplatzEdge    @0x5776d8 -> MapRasterizeBauplatzEdge   (pathfind_map.h)
//   * VIBE_Util_RandNext                @0x5cb8bc -> guild::crt::RandNext       (crt/rand.h)
//   * dword_631288/63128C/631290 special-target globals -> g_lastObjectId/Scene/Trade
//     (command_apply.h — the SAME globals the apply dispatcher fills)
//
// Bridges + what binds (verified against the originals via IDA):
//   - ApplyTargetHooks  (command_apply10): resolveEntityById, specialTarget,
//     personFindRecordById, countAtLocation, roomWorth, recordEntityId bind real.
//   - CommandApply8Hooks (command_apply8): randNext binds real (crt::RandNext).
//   - BauplatzMapHooks   (buildingtype_recon): RasterizeBauplatzEdge binds real.
//   - Inventory2Hooks    (inventory2): fully inert — see below.
//   - BuilderHooks       (command_apply9): fully inert — see below.
//
// ResolvedEntity mapping (command_apply10 ResolvedEntity vs the real 0x583b44 out
// params, recovered from the call site VIBE_Command_CheckSourceTargetReachable
// @0x495cf8): the original ResolveEntityById(&objOut, &sceneOut, id, &personOut)
// fills three slots; the recon's triple aliases them as
//     e.immediate <- objOut   (objOut+93 == the recon's e.immediate+93 read)
//     e.parent    <- sceneOut  (sceneOut+10 words == e.parent+20 bytes)
//     e.object    <- personOut (personOut+188 words == e.object+376 bytes)
// All three out-pointers are non-null in the originals, so the real leaf's
// person-first search order (outPerson != null) matches the binary exactly.
// Person/ObjectRec/SceneNode are raw POD blobs over the record-base byte layout
// (id @+4), so the void* <-> typed-record casts are byte-faithful (the same casts
// real_hooks3 / wire_charaction already perform).
//
// Leaves with NO clean reconstructed target stay at their inert default and are
// documented per bridge in wire_inventory.cpp:
//   - Inventory2Hooks: the whole table is the inventory GRID-UI seam — window /
//     object / widget / scene-graph / surface / text / HUD / drag-slot leaves, all
//     in unreconstructed modules (rules 3-5 / rule 8). The two engine-math fields
//     (buildingGroup / buildingOutputRatio) read a raw opaque void* building's byte
//     layout that lives only in the unreconstructed object world; the matching
//     recon leaves (BuildingType_GroupFromPairCode / Building_ComputeOutputRatio)
//     take a typed BuildingRec*, so materialising one from a bare void* would be a
//     cheap analogue (rule 8). Left fully inert (seeded).
//   - BuilderHooks: op80SnapshotDword (*dword_6315C0), combatUnitField9 (needs the
//     global combat-unit pool, not a process-global singleton here), errorLog (the
//     game's error log): all process-global / cross-module with no clean callable
//     leaf. Left fully inert.
//   - ApplyTargetHooks: queryFind/queryIterNext + personQueryBegin/personIterNext
//     are the engine varargs GameObject_QueryFind / Person_QueryBegin — the real
//     leaves (entity.h) take a typed (op,value) filter list, but the hook surface
//     collapses the filter into bare ints whose per-call-site selector encoding the
//     decompile does not unambiguously expose; binding would require GUESSING the
//     pairing (rule 8, matching the wire_charaction precedent). invFreeCapacity /
//     invCarryCapacity / resolveOwnerOrParentB take raw scene/person void* whose
//     StockChild/ContainerView extraction lives in the live object world (the recon
//     leaves want typed views); selectionMatch walks the global word_12CE910
//     selection table. All inert.
//   - CommandApply8Hooks: localPlayerMoney (dword_12CE914 table), copyState23/24Blob
//     (dword_11AA3E0/360), objectTemplate34 (dword_632240 spawn template),
//     gesetzGetRecord/gesetzRequestApply (law table dword_631EB0): process-global
//     game-state tables not modeled as standalone callable leaves. Inert.
//
// The real shared command queue / entity arrays these adapters touch are the SAME
// singletons the rest of the real wiring owns (entity.h's g_persons/g_objects/scene
// tree, command_apply.h's last-created globals), so the wired apply/check/build-plot
// paths operate on the one live game state.
namespace guild::sim {

// Install the real bindings into the four command/build bridges (ApplyTargetHooks,
// CommandApply8Hooks, BauplatzMapHooks) and seed-from-defaults the inventory-UI /
// packet-builder bridges (Inventory2Hooks, BuilderHooks). Idempotent; each table is
// process-lifetime storage the global hook pointer references.
void InstallRealInventoryWiring();

} // namespace guild::sim
