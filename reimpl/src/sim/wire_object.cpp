// See wire_object.h. Binds the object-spatial bridges that carry a genuine
// pure-logic reconstructed leaf (ObjectSceneEntityHooks, MoveUniverseHooks) to
// their real targets; the render/scene/audio bridges (MotionHooks, ObjectRecordHooks)
// and the object-lifecycle family stay inert / are wired elsewhere (see the header).
// Glue only — no module logic lives here.
#include "sim/wire_object.h"

#include "sim/object_scene_entity.h"  // ObjectSceneEntityHooks / Set/Ptr
#include "sim/character_move.h"       // MoveUniverseHooks / Set/Get / MoveUniverseState
#include "sim/entity.h"               // ResetEntityArrays (real table teardown)
#include "sim/universe.h"             // UniverseSwitchActiveSlot (real scene swap)

namespace guild::sim {

namespace {

// =========================================================================
// MoveUniverseHooks.switchActiveSlot -> the REAL central scene-slot swap.
//
// VIBE_Character_Move2UniverseActionUpdate @0x4063c8 calls SwitchActiveSlot with
// quiet==1 (SwitchActiveSlot(Data0, 1)); the quiet path runs the determinism-
// relevant slot record save/load swap and returns, deferring the present-frame
// render tail. VIBE_Universe_SwitchActiveSlot @0x5b4a24 (universe.cpp) is the 1:1
// reconstruction of that swap.
// =========================================================================
void WireSwitchActiveSlot(int universeIndex) {
    UniverseSwitchActiveSlot(universeIndex, /*quiet=*/true);   // 0x5b4a24
}

// =========================================================================
// ObjectSceneEntityHooks.releaseTables -> the REAL global-table release.
//
// VIBE_GameObject_FreeAllTables @0x583ab0 frees the four global record tables; in
// the reimpl those are static arrays, so the faithful "release" is ResetEntityArrays
// (entity.cpp) — clearing the live arrays + dropping the loaded guards (== a fresh
// unloaded game). The module's inert DEFAULT already points releaseTables here; we
// re-affirm the binding so the bridge is explicitly installed by the live wiring.
// =========================================================================
void WireReleaseTables() {
    ResetEntityArrays();                                       // 0x583ab0 table teardown
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
ObjectSceneEntityHooks g_objSceneEntity{};
MoveUniverseHooks      g_moveUniverse{};

} // namespace

void InstallRealObjectWiring() {
    // --- ObjectSceneEntityHooks (object_scene_entity.h) ----------------------
    // SEED-FROM-DEFAULTS: copy the module's current table (its releaseTables default
    // already routes to the REAL ResetEntityArrays; buildingFreeAndUnlink default is
    // null/inert). Re-affirm the real releaseTables binding; leave buildingFreeAndUnlink
    // inert (the per-person building-pointer columns are a 64-bit pointer-width gap —
    // see the header). The building SWEEP in FreeAllTables runs regardless.
    g_objSceneEntity = *ObjectSceneEntityHooksPtr();
    g_objSceneEntity.releaseTables = &WireReleaseTables;
    SetObjectSceneEntityHooks(&g_objSceneEntity);

    // --- MoveUniverseHooks (character_move.h) --------------------------------
    // SEED-FROM-DEFAULTS: copy the inert defaults (moveToUniverse/setVisible stay
    // their safe stubs — Move2UniverseActionUpdate calls every leaf WITHOUT a null
    // check, so a zero table would crash) and override only the scene-slot swap.
    g_moveUniverse = GetMoveUniverseHooks();
    g_moveUniverse.switchActiveSlot = &WireSwitchActiveSlot;
    // moveToUniverse (VIBE_Character_MoveToUniverse object reparent) and setVisible
    // (VIBE_Character_SetVisible) are render/scene leaves with no pure-logic
    // reconstruction -> kept inert.
    SetMoveUniverseHooks(&g_moveUniverse);

    // MotionHooks / ObjectRecordHooks: ZERO pure-logic fields (all render/anim/audio
    // or a foreign-slab pointer probe) -> intentionally NOT installed; their inert
    // defaults are the faithful observables. The object-lifecycle attach chain is
    // wired by InstallRealObjectAttachWiring(). See wire_object.h.
}

} // namespace guild::sim
