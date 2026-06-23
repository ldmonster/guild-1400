#pragma once
// wire_objscene — installs the real reconstructed leaves into the five
// object/scene-sync hook bridges this wiring file is responsible for. Glue only:
// no module logic lives here, only the binding of an already-reconstructed leaf
// to the hook field whose ORIGINAL leaf it 1:1 reproduces.
//
// The five bridges considered:
//   * SceneSyncHooks            (sim/cutscene_misc5.h)            -> PARTIALLY wired
//   * ObjectRecordHooks         (io/save_serial3.h)               -> zero-bindable
//   * UniverseHooks             (sim/character_universe.h)        -> zero-bindable
//   * UniverseHiddenToggle      (sim/world_buildingflag_util_recon2.h) -> zero-bindable
//   * MapLoadCityHooks          (gui/mission_load_run.h)          -> zero-bindable
//
// Only SceneSyncHooks exposes hook fields whose exact original leaf is already
// reconstructed in the tree with a byte-faithful, signature-compatible target:
//   mapTypeToCategory  <- guild::sim::Building_MapKindToCategory  (0x5878b0)
//   multiplyByRate     <- guild::world::AmtMoneyMultiplyByRate    (0x58f19c)
//
// Every other field of SceneSyncHooks, and ALL fields of the other four bridges,
// are render/universe/world side-effect leaves (Light_RefreshAllObjects 0x5c886c,
// SceneGraph walk/toggle, SwitchActiveSlot/SelectTextureSet/SetVisible, the
// world-reset + scene-stream-load + file-write host boundaries, the foreign-slab
// stock probe) or carry lossy hook signatures that cannot be bound 1:1 without
// state this module does not own. They stay inert (their module's safe default)
// — see InstallRealObjSceneWiring()'s body for the per-field rationale.
//
// SEED-FROM-DEFAULTS: the installer first copies the bridge's current (inert)
// hook table and overrides only the bound fields, so unbound fields keep their
// safe defaults — matching wire_charaction.cpp's pattern.

namespace guild::sim {

// Bind the reconstructed object/scene-sync leaves into their hook bridges.
// Idempotent; safe to call once at boot after the sibling hook tables exist.
void InstallRealObjSceneWiring();

}  // namespace guild::sim
